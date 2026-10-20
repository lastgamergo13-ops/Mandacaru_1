#pragma once

#include "ConfigValue.h"
#include "XMLConfig.h"

enum class NetworkService;

struct GameEntry
{
	GameEntry() = default;

	std::wstring rpx_file;
	std::wstring legacy_name;
	std::string product_code;
	std::string company_code;

	std::string custom_name;

	sint32 icon = -1;
	sint32 icon_small = -1;
	sint32 legacy_version = 0;
	sint32 legacy_update_version = -1;
	sint32 legacy_dlc_version = -1;
	uint64 title_id = 0;
	uint32 legacy_region = 0;

	std::wstring save_folder;
	std::wstring update_folder;
	std::wstring dlc_folder;

	uint64 legacy_time_played = 0;
	uint64 legacy_last_played = 0;

	bool favorite = false;

	bool is_update = false;
};

struct GraphicPackEntry
{
	GraphicPackEntry(std::wstring_view filename,std::string_view category, std::string_view preset)
		: filename(filename)
	{
		presets[std::string{category}] = preset;
	}

	GraphicPackEntry(std::wstring_view filename, std::string_view preset)
		: filename(filename)
	{
		presets[""] = preset;
	}

	GraphicPackEntry(std::wstring filename)
		: filename(std::move(filename))
	{}

	struct Preset
	{
		std::string name;
		std::string category;
	};
	std::unordered_map<std::string, std::string> presets; // category, active_preset

	std::wstring filename;
	bool enabled = true;
};

enum GraphicAPI
{
	kOpenGL = 0,
	kVulkan,
	kMetal,
	COUNT
};

#if defined(ENABLE_VULKAN)
constexpr GraphicAPI kDefaultGraphicsAPI = kVulkan;
#elif defined(ENABLE_METAL)
constexpr GraphicAPI kDefaultGraphicsAPI = kMetal;
#elif defined(ENABLE_OPENGL)
constexpr GraphicAPI kDefaultGraphicsAPI = kOpenGL;
#endif

enum AudioChannels
{
	kMono = 0,
	kStereo,
	kSurround,
};

enum UpscalingFilter
{
	kLinearFilter,
	kBicubicFilter,
	kBicubicHermiteFilter,
	kNearestNeighborFilter,
	kFsrFilter, // AMD FidelityFX Super Resolution 1.0 (EASU+RCAS), see RendererOutputShader::s_fsr_*_shader
};

enum FullscreenScaling
{
	kKeepAspectRatio,
	kStretch,
};

enum class ScreenPosition
{
	kDisabled = 0,
	kTopLeft,
	kTopCenter,
	kTopRight,
	kBottomLeft,
	kBottomCenter,
	kBottomRight,
};

enum class PrecompiledShaderOption
{
	Auto,
	Enable,
	Disable,
};
ENABLE_ENUM_ITERATORS(PrecompiledShaderOption, PrecompiledShaderOption::Auto, PrecompiledShaderOption::Disable);

enum class AccurateShaderMulOption
{
	False = 0, // always use standard multiplication
	True = 1 // fully emulate non-ieee MUL special cases (0*anything = 0)
};
ENABLE_ENUM_ITERATORS(AccurateShaderMulOption, AccurateShaderMulOption::False, AccurateShaderMulOption::True);

enum class MetalBufferCacheMode
{
    Auto,
    DevicePrivate,
    DeviceShared,
    Host,
};
ENABLE_ENUM_ITERATORS(MetalBufferCacheMode, MetalBufferCacheMode::Auto, MetalBufferCacheMode::Host);

enum class PositionInvariance
{
    Auto,
    False,
    True,
};
ENABLE_ENUM_ITERATORS(PositionInvariance, PositionInvariance::False, PositionInvariance::True);

enum class CPUMode
{
	SinglecoreInterpreter = 0,
	SinglecoreRecompiler = 1,
	DualcoreRecompiler = 2, // deprecated and not used anymore
	MulticoreRecompiler = 3,
	Auto = 4,
};
ENABLE_ENUM_ITERATORS(CPUMode, CPUMode::SinglecoreInterpreter, CPUMode::Auto);


enum class CPUModeLegacy
{
	SinglecoreInterpreter = 0,
	SinglecoreRecompiler = 1,
	DualcoreRecompiler = 2,
	TriplecoreRecompiler = 3,
	Auto = 4
};
ENABLE_ENUM_ITERATORS(CPUModeLegacy, CPUModeLegacy::SinglecoreInterpreter, CPUModeLegacy::Auto);

// values match info.cemu.cemu.nativeinterface.NativeGameTitles.DriverSettingMode on the Android/Kotlin side
enum class DriverSettingMode
{
	Global = 0,
	System = 1,
	Custom = 2,
};
ENABLE_ENUM_ITERATORS(DriverSettingMode, DriverSettingMode::Global, DriverSettingMode::Custom);

enum class CafeConsoleRegion
{
	JPN = 0x1,
	USA = 0x2,
	EUR = 0x4,
	AUS_DEPR = 0x8,
	CHN = 0x10,
	KOR = 0x20,
	TWN = 0x40,
	Auto = 0xFF,
};
ENABLE_BITMASK_OPERATORS(CafeConsoleRegion);

enum class CafeConsoleLanguage
{
	JA = 0,
	EN = 1,
	FR = 2,
	DE = 3,
	IT = 4,
	ES = 5,
	ZH = 6,
	KO = 7,
	NL = 8,
	PT = 9,
	RU = 10,
	TW = 11,
};
ENABLE_ENUM_ITERATORS(CafeConsoleLanguage, CafeConsoleLanguage::JA, CafeConsoleLanguage::TW);

#if BOOST_OS_WINDOWS
enum class CrashDump
{
	Disabled,
	Lite,
	Full
};
ENABLE_ENUM_ITERATORS(CrashDump, CrashDump::Disabled, CrashDump::Full);
#elif BOOST_OS_UNIX
enum class CrashDump
{
	Disabled,
	Enabled
};
ENABLE_ENUM_ITERATORS(CrashDump, CrashDump::Disabled, CrashDump::Enabled);
#endif

template <>
struct fmt::formatter<PrecompiledShaderOption> : formatter<string_view> {
	template <typename FormatContext>
	auto format(const PrecompiledShaderOption c, FormatContext &ctx) const {
		string_view name;
		switch (c)
		{
		case PrecompiledShaderOption::Auto: name = "auto"; break;
		case PrecompiledShaderOption::Enable: name = "true"; break;
		case PrecompiledShaderOption::Disable: name = "false"; break;
		default: name = "unknown"; break;
		}
		return formatter<string_view>::format(name, ctx);
	}
};
template <>
struct fmt::formatter<AccurateShaderMulOption> : formatter<string_view> {
	template <typename FormatContext>
	auto format(const AccurateShaderMulOption c, FormatContext &ctx) const {
		string_view name;
		switch (c)
		{
		case AccurateShaderMulOption::True: name = "true"; break;
		case AccurateShaderMulOption::False: name = "false"; break;
		default: name = "unknown"; break;
		}
		return formatter<string_view>::format(name, ctx);
	}
};
template <>
struct fmt::formatter<MetalBufferCacheMode> : formatter<string_view> {
	template <typename FormatContext>
	auto format(const MetalBufferCacheMode c, FormatContext &ctx) const {
		string_view name;
		switch (c)
		{
		case MetalBufferCacheMode::Auto: name = "auto"; break;
		case MetalBufferCacheMode::DevicePrivate: name = "device private"; break;
		case MetalBufferCacheMode::DeviceShared: name = "device shared"; break;
		case MetalBufferCacheMode::Host: name = "host"; break;
		default: name = "unknown"; break;
		}
		return formatter<string_view>::format(name, ctx);
	}
};
template <>
struct fmt::formatter<PositionInvariance> : formatter<string_view> {
	template <typename FormatContext>
	auto format(const PositionInvariance c, FormatContext &ctx) const {
		string_view name;
		switch (c)
		{
		case PositionInvariance::Auto: name = "auto"; break;
		case PositionInvariance::False: name = "false"; break;
		case PositionInvariance::True: name = "true"; break;
		default: name = "unknown"; break;
		}
		return formatter<string_view>::format(name, ctx);
	}
};
template <>
struct fmt::formatter<CPUMode> : formatter<string_view> {
	template <typename FormatContext>
	auto format(const CPUMode c, FormatContext &ctx) const {
		string_view name;
		switch (c)
		{
		case CPUMode::SinglecoreInterpreter: name = "Single-core interpreter"; break;
		case CPUMode::SinglecoreRecompiler: name = "Single-core recompiler"; break;
		case CPUMode::DualcoreRecompiler: name = "Dual-core recompiler"; break;
		case CPUMode::MulticoreRecompiler: name = "Multi-core recompiler"; break;
		case CPUMode::Auto: name = "Auto"; break;
		default: name = "unknown"; break;
		}
		return formatter<string_view>::format(name, ctx);
	}
};
template <>
struct fmt::formatter<CPUModeLegacy> : formatter<string_view> {
	template <typename FormatContext>
	auto format(const CPUModeLegacy c, FormatContext &ctx) const {
		string_view name;
		switch (c)
		{
		case CPUModeLegacy::SinglecoreInterpreter: name = "Singlecore-Interpreter"; break;
		case CPUModeLegacy::SinglecoreRecompiler: name = "Singlecore-Recompiler"; break;
		case CPUModeLegacy::DualcoreRecompiler: name = "Dualcore-Recompiler"; break;
		case CPUModeLegacy::TriplecoreRecompiler: name = "Triplecore-Recompiler"; break;
		case CPUModeLegacy::Auto: name = "Auto"; break;
		default: name = "unknown"; break;
		}
		return formatter<string_view>::format(name, ctx);
	}
};
template <>
struct fmt::formatter<CafeConsoleRegion> : formatter<string_view> {
	template <typename FormatContext>
	auto format(const CafeConsoleRegion v, FormatContext &ctx) const {
		string_view name;
		switch (v)
		{
		case CafeConsoleRegion::JPN: name = TR_NOOP("Japan"); break;
		case CafeConsoleRegion::USA: name = TR_NOOP("USA"); break;
		case CafeConsoleRegion::EUR: name = TR_NOOP("Europe"); break;
		case CafeConsoleRegion::AUS_DEPR: name = TR_NOOP("Australia"); break;
		case CafeConsoleRegion::CHN: name = TR_NOOP("China"); break;
		case CafeConsoleRegion::KOR: name = TR_NOOP("Korea"); break;
		case CafeConsoleRegion::TWN: name = TR_NOOP("Taiwan"); break;
		case CafeConsoleRegion::Auto: name = TR_NOOP("Auto"); break;
		default: name = TR_NOOP("many"); break;

		}
		return formatter<string_view>::format(name, ctx);
	}
};
template <>
struct fmt::formatter<CafeConsoleLanguage> : formatter<string_view> {
	template <typename FormatContext>
	auto format(const CafeConsoleLanguage v, FormatContext &ctx) {
		string_view name;
		switch (v)
		{
		case CafeConsoleLanguage::JA: name = "Japanese"; break;
		case CafeConsoleLanguage::EN: name = "English"; break;
		case CafeConsoleLanguage::FR: name = "French"; break;
		case CafeConsoleLanguage::DE: name = "German"; break;
		case CafeConsoleLanguage::IT: name = "Italian"; break;
		case CafeConsoleLanguage::ES: name = "Spanish"; break;
		case CafeConsoleLanguage::ZH: name = "Chinese"; break;
		case CafeConsoleLanguage::KO: name = "Korean"; break;
		case CafeConsoleLanguage::NL: name = "Dutch"; break;
		case CafeConsoleLanguage::PT: name = "Portugese"; break;
		case CafeConsoleLanguage::RU: name = "Russian"; break;
		case CafeConsoleLanguage::TW: name = "Taiwanese"; break;
		default: name = "unknown"; break;
		}
		return formatter<string_view>::format(name, ctx);
	}
};

#if BOOST_OS_WINDOWS
template <>
struct fmt::formatter<CrashDump> : formatter<string_view> {
	template <typename FormatContext>
	auto format(const CrashDump v, FormatContext &ctx) {
		string_view name;
		switch (v)
		{
		case CrashDump::Disabled: name = "Disabled"; break;
		case CrashDump::Lite: name = "Lite"; break;
		case CrashDump::Full: name = "Full"; break;
		default: name = "unknown"; break;

		}
		return formatter<string_view>::format(name, ctx);
	}
};
#elif BOOST_OS_UNIX
template <>
struct fmt::formatter<CrashDump> : formatter<string_view> {
	template <typename FormatContext>
	auto format(const CrashDump v, FormatContext &ctx) {
		string_view name;
		switch (v)
		{
		case CrashDump::Disabled: name = "Disabled"; break;
		case CrashDump::Enabled: name = "Enabled"; break;
		default: name = "unknown"; break;

		}
		return formatter<string_view>::format(name, ctx);
	}
};
#endif


struct CemuConfig
{
	CemuConfig()
	{

	};

	CemuConfig(const CemuConfig&) = delete;

	// sets mlc path, updates permanent config value, saves config
	void SetMLCPath(fs::path path, bool save = true);

	ConfigValue<uint64> log_flag{ 0 };
	ConfigValue<bool> advanced_ppc_logging{ false };
	// Persists compiled PPC recompiler functions to a per-title disk cache (mirrors
	// LatteShaderCache) so a later launch of the same title can reuse them instead of
	// recompiling from scratch - see PPCRecompilerCache.h for the safety details around
	// why this is more delicate than the shader cache (raw machine code isn't position
	// independent). Off by default: still new, and any bug here means running
	// miscompiled native code instead of just a visual glitch.
	ConfigValue<bool> enable_recompiler_aot_cache{ false };

	// Limitar threads do compilador de shaders: caps both shader/pipeline compiler thread pools
	// (VulkanPipelineCompiler's organic, in-game pool and VulkanPipelineStableCache's burst
	// reload-at-boot pool) at 2 threads instead of up to 8, to avoid RAM spikes that can get the
	// app OOM-killed on Android devices with many logical cores but little available RAM. Only
	// ever reduces the thread count, never increases it. Off by default; takes effect on the
	// next title boot (the thread pools are only created then).
	ConfigValue<bool> limit_shader_compiler_threads{ false };

	// FSR3 Frame Generation (Vulkan, Experimental/Hackers). Off by default. Read every frame by
	// SwapchainFrameGenVk, so it can be toggled while a game is running.
	ConfigValue<bool> fsr3_frame_gen{ false };
	// Opcoes do Frame Gen (todas podem ser sobrescritas por jogo - ver FrameGenSettings.h):
	ConfigValue<sint32> fsr3_fg_multiplier{ 2 };      // 2..4 (total de frames por frame real)
	ConfigValue<bool> fsr3_fg_pyramid{ false };       // Optical Flow em piramide multi-nivel (experimental; cai no modo simples se falhar)
	ConfigValue<sint32> fsr3_fg_flow_scale{ 50 };     // % da resolucao usada pelo Optical Flow: 50, 75 ou 100 (padrao 50: menos carga de GPU, evita device lost no Adreno)
	ConfigValue<bool> fsr3_fg_performance_mode{ false }; // Optical Flow mais leve (forca flow scale <= 50%)
	ConfigValue<sint32> fsr3_fg_base_fps_limit{ 0 };  // 0 = sem limite; 30/40/60 = limita o FPS REAL antes de gerar
	// "FSR Upscale": usa o filtro FSR 1.0 (EASU+RCAS) do Cemu no lugar do filtro de upscale escolhido
	ConfigValue<bool> fsr3_upscale{ false };
	// Overlay do Frame Gen (linha "FrameGen: .. real + .. gerado = .."), so global:
	ConfigValue<bool> fsr3_ov_show{ true };        // mostra a linha do Frame Gen no overlay de stats
	ConfigValue<sint32> fsr3_ov_decimals{ 1 };     // casas decimais: 0, 1 ou 2
	ConfigValue<sint32> fsr3_ov_style{ 0 };        // 0 = detalhado, 1 = compacto
	ConfigValue<bool> fsr3_ov_gpu_ms{ false };     // mostra o custo de GPU (ms) do frame gerado

	ConfigValue<bool> permanent_storage{ true };

	ConfigValue<std::string> mlc_path{};
	ConfigValue<std::string> proxy_server{};
	ConfigValue<std::string> custom_driver_path{};

	// temporary workaround because feature crashes on macOS
#if BOOST_OS_MACOS
#define DISABLE_SCREENSAVER_DEFAULT false
#else
#define DISABLE_SCREENSAVER_DEFAULT true
#endif
	ConfigValue<bool> disable_screensaver{DISABLE_SCREENSAVER_DEFAULT};
#undef DISABLE_SCREENSAVER_DEFAULT
	ConfigValue<bool> play_boot_sound{false};

	// Internal (emulated Wii U) real-time clock. When rtc_use_system_time is true (default),
	// the emulated console's clock is read from the device's real clock, exactly as before this
	// option existed. When false, rtc_custom_timestamp (Unix epoch seconds, UTC) is used instead
	// as the wall-clock time read once at the start of the next launched title - see
	// cemu_initForGame() in CafeSystem.cpp for where this is actually consumed. Default of
	// 946684800 (2000-01-01 00:00:00 UTC) is the Wii U clock's own epoch - the earliest timestamp
	// it can represent - so a never-touched value is already valid instead of falling back to the
	// Unix epoch (1970), which underflows the Wii U's cycle-since-2000 math.
	ConfigValue<bool> rtc_use_system_time{ true };
	ConfigValue<sint64> rtc_custom_timestamp{ 946684800 };

	// Quality of Life (Experimental/Hackers): each master switch below gates one feature from
	// that tab that lives outside the CemuConfig-driven Toggle pattern most options here use
	// (an in-game action button, a background hotkey handler, etc.) - the switch is what the QoL
	// screen's Toggle actually controls; the feature itself checks this before doing anything.
	// All off by default, like the rest of Experimental/Hackers.
	ConfigValue<bool> qol_quick_screenshot_enabled{ true }; // ligado por padrao
	ConfigValue<bool> qol_quick_controller_profile_switcher_enabled{ false };
	ConfigValue<bool> qol_in_game_cheats_enabled{ false };

	std::vector<std::string> game_paths;
	std::mutex game_cache_entries_mutex;
	std::vector<GameEntry> game_cache_entries;

	// optimized access
	std::set<uint64> game_cache_favorites; // per titleId

	struct _path_hash {
		std::size_t operator()(const fs::path& path) const {
			return fs::hash_value(path);
		}
	};
	// <filename, <category, preset>>
	std::unordered_map<fs::path, std::unordered_map<std::string, std::string>, _path_hash> graphic_pack_entries;

	ConfigValueBounds<CPUMode> cpu_mode{ CPUMode::Auto };
	ConfigValueBounds<CafeConsoleLanguage> console_language{ CafeConsoleLanguage::EN };

	// Recompiler IR optimizations (dead code elimination fixpoint loop + redundant segment
	// merging, see IMLOptimizer.cpp). Off by default, matching the other Experimental/Hackers
	// options that touch the recompiler (texture compression, AOT cache) - exposed as an opt-in
	// (Android: Experimental / Hackers -> Deep IR Optimizations) since a bug in these passes
	// could affect any game.
	ConfigValue<bool> advanced_iml_optimizations{ false };

	// "Memory Usage Brake" (Android: Experimental / Hackers). When enabled, once this process's
	// own RSS crosses memory_ceiling_mb, the app immediately reclaims any texture it already
	// considers safe to discard instead of trickling that cleanup in over several seconds - see
	// AndroidMemoryManager and LatteTC_RequestAggressiveCleanup(). memory_ceiling_mb is only
	// consulted while the toggle is on. Off by default.
	ConfigValue<bool> memory_usage_brake{ false };
	ConfigValue<sint32> memory_ceiling_mb{ 4096 };

	// graphics
	ConfigValue<GraphicAPI> graphic_api{ kDefaultGraphicsAPI };
	std::array<uint8, 16> legacy_graphic_device_uuid{}; // placeholder option for backwards compatibility with settings from 2.6 and before (renamed to "vkDevice")
	std::array<uint8, 16> vk_graphic_device_uuid;
	uint64 mtl_graphic_device_uuid{ 0 };

	// Android-only render API picker (Experimental / Hackers -> "Render API"). Deliberately
	// kept separate from graphic_api/GraphicAPI above instead of adding a 4th enum value
	// there: that enum's numeric values are already relied upon 1:1 by the desktop wx UI
	// (dropdown index <-> enum value) and are persisted to disk, so inserting a value would
	// either break both or force renumbering kMetal, which isn't worth the risk for an
	// Android-only feature. kAuto currently always resolves to Vulkan (see
	// NativeEmulation_initializeRenderer) - Vulkan is this project's mature, tested backend;
	// the OpenGL ES backend is new/experimental (see LatteTextureGL.cpp, OpenGLRenderer.cpp
	// Android EGL path) and only used when the user explicitly picks it.
	enum AndroidRendererMode : sint32
	{
		kAndroidRendererAuto = 0,
		kAndroidRendererVulkan = 1,
		kAndroidRendererOpenGL = 2,
	};
	ConfigValue<sint32> android_renderer_mode{ (sint32)kAndroidRendererAuto };

	ConfigValue<int> vsync{ 0 }; // 0 = off, 1+ = depending on render backend
	ConfigValue<bool> gx2drawdone_sync { true };
	// V11.46: pula desenhos so de profundidade (mapas de sombra / z-prepass) no OpenGL. Mais fps, sombras somem. Desligado por padrao.
	ConfigValue<bool> skip_depth_only_draws { false };
	ConfigValue<bool> render_upside_down{ false };
	ConfigValue<bool> async_compile{ true };

	// Forces the emulated console to run at half its native vsync rate (e.g. 60 -> 30).
	// Useful on devices that can't consistently sustain the game's native rate — trades
	// peak framerate for a steadier one. Has no effect on titles where a graphic pack
	// already defines its own custom vsync frequency (that takes priority — see
	// GraphicPack2::ActivateForCurrentTitle / CafeSystem's title launch). Mutually
	// exclusive with emulation_speed_percent (this takes priority when both are set).
	ConfigValue<bool> limit_to_half_fps{ false };
	// Scales the emulated console's own sense of elapsed time (CPU timer + vsync period),
	// not just how the frames are displayed — 150 genuinely runs the game at 1.5x. 100 is
	// normal speed. Generalizes limit_to_half_fps to any percentage; ignored while that
	// option is active (which takes priority), and while a graphic pack owns the vsync
	// timing for this title.
	ConfigValue<uint32> emulation_speed_percent{ 100 };
#ifdef ENABLE_METAL
	ConfigValue<bool> force_mesh_shaders{ false };
#endif

	// Gamma
	ConfigValue<bool> overrideAppGammaPreference{ false };
	ConfigValue<float> overrideGammaValue{ 2.2f };
	ConfigValue<float> userDisplayGamma { 2.2f }; // 0 = sRGB, >0 gamma

	ConfigValue<bool> vk_accurate_barriers{ true };

	// Experimental: recompress Wii U BC1-BC5 textures to ETC2/EAC on GPUs that support
	// ETC2 but not BC/S3TC natively (the common case on Android), instead of decoding
	// them to uncompressed RGBA. One toggle per format so the user can pick exactly
	// which (untested-on-real-hardware) transcoders to opt into. Off by default.
	ConfigValue<bool> texture_compression_bc1{ false };
	ConfigValue<bool> texture_compression_bc2{ false };
	ConfigValue<bool> texture_compression_bc3{ false };
	ConfigValue<bool> texture_compression_bc4{ false };
	ConfigValue<bool> texture_compression_bc5{ false };

	struct
	{
		ScreenPosition position = ScreenPosition::kDisabled;
		uint32 text_color = 0xFFFFFFFF;
		sint32 text_scale = 100;
		bool fps = true;
		bool drawcalls = false;
		bool cpu_usage = false;
		bool cpu_per_core_usage = false;
		bool ram_usage = false;
		bool vram_usage = false;
		bool debug = false;
		// Quality of Life additions below - all off by default / unchanged default value, so a
		// config file from before these existed behaves identically to one with these explicitly
		// set to their defaults.
		bool frame_time = false;
		bool device_thermal_status = false;
		bool horizontal_layout = false;
		// Was a fixed kBackgroundAlpha constant in LatteOverlay.cpp; now configurable (still used
		// for both the stats/debug HUD and notifications, same as the constant it replaces).
		float background_opacity = 0.65f;
	} overlay{};

	struct
	{
		ScreenPosition position = ScreenPosition::kTopLeft;
		uint32 text_color = 0xFFFFFFFF;
		sint32 text_scale = 100;
		bool controller_profiles = true;
		bool controller_battery = false;
		bool shader_compiling = true;
		bool friends = true;
	} notification{};

	ConfigValue<sint32> upscale_filter{kBicubicFilter};
	ConfigValue<sint32> downscale_filter{kLinearFilter};
	// FSR RCAS sharpening amount, in "stops" (see AMD's FsrRcasCon() in ffx_fsr1.h):
	// 0.0 = sharpest, higher values = softer/less sharpening. Only meaningful when
	// upscale_filter == kFsrFilter - see RendererOutputShader::FillUniformBlockBuffer().
	ConfigValue<float> fsr_sharpness{0.2f};
	ConfigValue<sint32> fullscreen_scaling{kKeepAspectRatio};

	// audio
	sint32 audio_api = 0;
	sint32 audio_delay = 2;
	// Experimental: continuously trims a small amount (at most 1 block) of the oldest queued
	// audio once the buffer drifts meaningfully past the configured latency, instead of letting
	// it grow until the hard buffer limit forces one large, audible drop - see CubebAPI::FeedBlock.
	// Off by default: this has not been confirmed inaudible across long play sessions yet - it
	// trades one rare, larger pop for small, un-crossfaded waveform cuts that could themselves be
	// audible as occasional clicks.
	ConfigValue<bool> audio_video_sync{ false };
	AudioChannels tv_channels = kStereo, pad_channels = kStereo, input_channels = kMono;
	sint32 tv_volume = 50, pad_volume = 0, input_volume = 50, portal_volume = 50;
	std::wstring tv_device{ L"default" }, pad_device, input_device, portal_device;
	// Experimental: forces every audio voice to be resampled with nearest-neighbor sample
	// fetching (the AX engine's cheapest decode path) instead of whatever mode the game itself
	// requested for that voice (Tap or Linear) - trades resampling fidelity for CPU headroom on
	// the audio thread. Off by default (the exact previous behavior - each voice decoded the way
	// the game asked for it).
	ConfigValue<bool> fast_audio_resampling{ false };
	// Experimental: when the TV output mode (see tv_channels above) is Stereo or Mono, Cemu's
	// stock behavior is to simply drop the Center/LFE/Surround channels instead of mixing them
	// in - this performs a real downmix instead, folding those channels into the front L/R pair
	// using the standard broadcast "Lo/Ro" coefficients (see AXOut_SubmitTVFrame/
	// AXOut_SubmitDRCFrame in ax_out.cpp). Off by default (exact previous behavior).
	ConfigValue<bool> force_audio_downmix{ false };
	// Experimental: while the audio buffer is critically low (about to run dry), polls the
	// audio backend more aggressively to try to top it up before it starts playing silence -
	// see g_enableAudioBufferSync in audio/IAudioAPI.h and AXOut_update() in ax_out.cpp. Off by
	// default.
	ConfigValue<bool> audio_buffer_sync{ false };

	// account
	struct
	{
		ConfigValueBounds<uint32> m_persistent_id{0x80000001, 0x80000001, 0xFFFFFFFF};
		ConfigValue<bool> legacy_online_enabled{false};
		ConfigValue<int> legacy_active_service{0};
		std::unordered_map<uint32, NetworkService> service_select; // per-account service index. Key is persistentId
	}account{};

	// input
	struct
	{
		ConfigValue<std::string> host{"127.0.0.1"};
		ConfigValue<uint16> port{ 26760 };
	}dsu_client{};

	// debug
	ConfigValueBounds<CrashDump> crash_dump{ CrashDump::Disabled };
	ConfigValue<uint16> gdb_port{ 1337 };
#ifdef ENABLE_METAL
	ConfigValue<std::string> gpu_capture_dir{ "" };
	ConfigValue<bool> framebuffer_fetch{ true };
#endif

	XMLConfigParser Load(XMLConfigParser& parser);
	XMLConfigParser Save(XMLConfigParser& parser);

	bool IsGameListFavorite(uint64 titleId);
	void SetGameListFavorite(uint64 titleId, bool isFavorite);
	bool GetGameListCustomName(uint64 titleId, std::string& customName);
	void SetGameListCustomName(uint64 titleId, std::string customName);

	NetworkService GetAccountNetworkService(uint32 persistentId);
	void SetAccountSelectedService(uint32 persistentId, NetworkService serviceIndex);

	// emulated usb devices
	struct
	{
		ConfigValue<bool> emulate_skylander_portal{false};
		ConfigValue<bool> emulate_infinity_base{false};
		ConfigValue<bool> emulate_dimensions_toypad{false};
	}emulated_usb_devices{};

	static int AudioChannelsToNChannels(AudioChannels kStereo)
	{
		switch (kStereo)
		{
		case 0:
			return 1; // will mix mono sound on both output channels
		case 2:
			return 6;
		default: // stereo
			return 2;
		}
	}

  private:
	GameEntry* GetGameEntryByTitleId(uint64 titleId);
	GameEntry* CreateGameEntry(uint64 titleId);
};

typedef XMLDataConfig<CemuConfig> XMLCemuConfig_t;

inline XMLCemuConfig_t& GetConfigHandle()
{
	static XMLCemuConfig_t config;
	return config;
}

inline CemuConfig& GetConfig()
{
	return GetConfigHandle().data();
}
