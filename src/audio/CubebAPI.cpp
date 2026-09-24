#include "CubebAPI.h"
#include "config/CemuConfig.h"

#if BOOST_OS_WINDOWS
#include <combaseapi.h>
#include <mmreg.h>
#include <mmsystem.h>
#pragma comment(lib, "Avrt.lib")
#pragma comment(lib, "ksuser.lib")
#endif

#if defined(__ANDROID__)
#include "Common/ExceptionHandler/ExceptionHandler.h"
#endif


static void state_cb(cubeb_stream* stream, void* user, cubeb_state state)
{
	if (!stream)
		return;

	/*switch (state)
	{
	case CUBEB_STATE_STARTED:
		fprintf(stderr, "stream started\n");
		break;
	case CUBEB_STATE_STOPPED:
		fprintf(stderr, "stream stopped\n");
		break;
	case CUBEB_STATE_DRAINED:
		fprintf(stderr, "stream drained\n");
		break;
	default:
		fprintf(stderr, "unknown stream state %d\n", state);
	}*/
}

long CubebAPI::data_cb(cubeb_stream* stream, void* user, const void* inputbuffer, void* outputbuffer, long nframes)
{
	auto* thisptr = (CubebAPI*)user;
	//const auto size = (size_t)thisptr->m_bytesPerBlock; // (size_t)nframes* thisptr->m_channels;

#if defined(__ANDROID__)
	// this callback runs on cubeb's own real-time audio thread and fires often (every
	// ~10-20ms), so we only touch the watchdog roughly once a second to avoid adding
	// mutex contention to a real-time-sensitive callback.
	static thread_local uint32 s_callbackCounter = 0;
	if ((++s_callbackCounter % 64) == 0)
		Watchdog_SetThreadStatus("audio thread - cubeb data callback running");
#endif

	// m_bytesPerBlock = samples_per_block * channels * (bits_per_sample / 8);
	const auto size = (size_t)nframes * thisptr->m_channels * (thisptr->m_bitsPerSample/8);

	std::unique_lock lock(thisptr->m_mutex);
	if (thisptr->m_buffer.empty())
	{
		// we got no data, just write silence
		memset(outputbuffer, 0x00, size);
	}
	else
	{
		const auto copied = std::min(thisptr->m_buffer.size(), size);
		memcpy(outputbuffer, thisptr->m_buffer.data(), copied);
		thisptr->m_buffer.erase(thisptr->m_buffer.begin(), std::next(thisptr->m_buffer.begin(), copied));
		lock.unlock();
		// fill rest with silence
		if (copied != size)
			memset((uint8*)outputbuffer + copied, 0x00, size - copied);
	}

	return nframes;
}

CubebAPI::CubebAPI(cubeb_devid devid, uint32 samplerate, uint32 channels, uint32 samples_per_block,
                   uint32 bits_per_sample)
	: IAudioAPI(samplerate, channels, samples_per_block, bits_per_sample)
{
	cubeb_stream_params output_params;

	output_params.format = CUBEB_SAMPLE_S16LE;
	output_params.rate = samplerate;
	output_params.channels = channels;
	output_params.prefs = CUBEB_STREAM_PREF_NONE;

	switch (channels)
	{
	case 8:
		output_params.layout = CUBEB_LAYOUT_3F4_LFE;
		break;
	case 6:
		output_params.layout = CUBEB_LAYOUT_3F2_LFE_BACK;
		break;
	case 4:
		output_params.layout = CUBEB_LAYOUT_QUAD;
		break;
	case 2:
		output_params.layout = CUBEB_LAYOUT_STEREO;
		break;
	default:
		output_params.layout = CUBEB_LAYOUT_MONO;
		break;
	}

	uint32 latency = 1;
	cubeb_get_min_latency(s_context, &output_params, &latency);

	m_buffer.reserve((size_t)m_bytesPerBlock * kBlockCount);

	if (cubeb_stream_init(s_context, &m_stream, "Cemu Cubeb output",
	                      nullptr, nullptr,
	                      devid, &output_params,
	                      latency, data_cb, state_cb, this) != CUBEB_OK)
	{
		throw std::runtime_error("can't initialize cubeb device");
	}
}

CubebAPI::~CubebAPI()
{
	if (m_stream)
	{
		Stop();
		cubeb_stream_destroy(m_stream);
	}
}

bool CubebAPI::NeedAdditionalBlocks() const
{
	std::shared_lock lock(m_mutex);
	return m_buffer.size() < GetAudioDelay() * m_bytesPerBlock;
}

bool CubebAPI::IsBufferCriticallyLow() const
{
	std::shared_lock lock(m_mutex);
	// less than ~2 blocks queued means the platform audio callback (data_cb, running on its own
	// real-time OS thread) could start reading silence - an audible glitch - within the next
	// couple of AXOut_update() cycles if nothing feeds the buffer soon. This is just a read of
	// the current occupancy, taken with the same shared_lock as NeedAdditionalBlocks(); it
	// never blocks the audio callback thread.
	constexpr uint32 kCriticalBlockThreshold = 2;
	return m_buffer.size() < (size_t)m_bytesPerBlock * kCriticalBlockThreshold;
}

bool CubebAPI::FeedBlock(sint16* data)
{
	std::unique_lock lock(m_mutex);

	if (GetConfig().audio_video_sync)
	{
		// Audio/Video sync: the queued audio buffer is drained by the real audio device clock,
		// while it's filled at the pace of the emulated video/game clock. Those two clocks are
		// independent and drift apart over long play sessions, so without correction the buffer
		// slowly grows past the configured latency target until the hard capacity limit below
		// is hit and a large, audible chunk of audio has to be dropped all at once.
		// Instead, once the buffer grows meaningfully beyond the target latency, trim a small
		// amount of the oldest queued audio here on every block so the audio clock is continuously
		// pulled back in line with the video clock in small, inaudible steps.
		const size_t targetSize = (size_t)GetAudioDelay() * m_bytesPerBlock;
		const size_t driftTolerance = (size_t)2 * m_bytesPerBlock; // allow minor jitter before correcting
		if (targetSize > 0 && m_buffer.size() > targetSize + driftTolerance)
		{
			const size_t excess = m_buffer.size() - targetSize;
			const size_t trim = std::min(excess, (size_t)m_bytesPerBlock);
			m_buffer.erase(m_buffer.begin(), std::next(m_buffer.begin(), trim));
			cemuLog_logDebug(LogType::Force, "audio/video sync: trimmed {} byte(s) of queued audio to correct drift", trim);
		}
	}

	if (m_buffer.capacity() <= m_buffer.size() + m_bytesPerBlock)
	{
		cemuLog_logDebug(LogType::Force, "dropped direct sound block since too many buffers are queued");
		return false;
	}

	m_buffer.insert(m_buffer.end(), (uint8*)data, (uint8*)data + m_bytesPerBlock);
	return true;
}

bool CubebAPI::Play()
{
	if (m_is_playing)
		return true;

	if (cubeb_stream_start(m_stream) == CUBEB_OK)
	{
		m_is_playing = true;
		return true;
	}

	return false;
}

bool CubebAPI::Stop()
{
	if (!m_is_playing)
		return true;

	if (cubeb_stream_stop(m_stream) == CUBEB_OK)
	{
		m_is_playing = false;
		return true;
	}

	return false;
}

void CubebAPI::SetVolume(sint32 volume)
{
	IAudioAPI::SetVolume(volume);
	cubeb_stream_set_volume(m_stream, (float)volume / 100.0f);
}


bool CubebAPI::InitializeStatic()
{
	if (cubeb_init(&s_context, "Cemu Cubeb", nullptr))
	{
		cemuLog_log(LogType::Force, "can't create cubeb audio api");
		return false;
	}
	return true;
}

void CubebAPI::Destroy()
{
	if (s_context)
		cubeb_destroy(s_context);
}

std::vector<IAudioAPI::DeviceDescriptionPtr> CubebAPI::GetDevices()
{
	std::vector<DeviceDescriptionPtr> result;
	// Add the default device to the list
	auto defaultDevice = std::make_shared<CubebDeviceDescription>(nullptr, "default", L"Default Device");
	result.emplace_back(defaultDevice);

	cubeb_device_collection devices;
	if (cubeb_enumerate_devices(s_context, CUBEB_DEVICE_TYPE_OUTPUT, &devices) != CUBEB_OK)
		return result;

	result.reserve(devices.count + 1); // The default device already occupies one element

	for (size_t i = 0; i < devices.count; ++i)
	{
		// const auto& device = devices.device[i];
		if (devices.device[i].state == CUBEB_DEVICE_STATE_ENABLED)
		{
			auto device = std::make_shared<CubebDeviceDescription>(devices.device[i].devid, devices.device[i].device_id,
																   boost::nowide::widen(
																	   devices.device[i].friendly_name));
			result.emplace_back(device);
		}
	}

	cubeb_device_collection_destroy(s_context, &devices);

	return result;
}
