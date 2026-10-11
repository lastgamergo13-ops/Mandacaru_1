#include <algorithm>
#include "JNIUtils.h"
#include "AndroidMemoryManager.h"
#include "audio/IAudioAPI.h"
#include "config/CemuConfig.h"
#include "config/NetworkSettings.h"

extern "C" [[maybe_unused]] JNIEXPORT jint JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_getOverlayPosition([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return static_cast<jint>(GetConfig().overlay.position);
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setOverlayPosition([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jint position)
{
	GetConfig().overlay.position = static_cast<ScreenPosition>(position);
}

extern "C" [[maybe_unused]] JNIEXPORT jint JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_getOverlayTextScalePercentage([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return GetConfig().overlay.text_scale;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setOverlayTextScalePercentage([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jint scalePercentage)
{
	GetConfig().overlay.text_scale = scalePercentage;
}

extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_isOverlayFPSEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return GetConfig().overlay.fps;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setOverlayFPSEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean enabled)
{
	GetConfig().overlay.fps = enabled;
}

extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_isOverlayDrawCallsPerFrameEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return GetConfig().overlay.drawcalls;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setOverlayDrawCallsPerFrameEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean enabled)
{
	GetConfig().overlay.drawcalls = enabled;
}

extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_isOverlayCPUUsageEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return GetConfig().overlay.cpu_usage;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setOverlayCPUUsageEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean enabled)
{
	GetConfig().overlay.cpu_usage = enabled;
}

extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_isOverlayCPUPerCoreUsageEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return GetConfig().overlay.cpu_per_core_usage;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setOverlayCPUPerCoreUsageEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean enabled)
{
	GetConfig().overlay.cpu_per_core_usage = enabled;
}

extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_isOverlayRAMUsageEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return GetConfig().overlay.ram_usage;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setOverlayRAMUsageEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean enabled)
{
	GetConfig().overlay.ram_usage = enabled;
}

extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_isOverlayVRAMUsageEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return GetConfig().overlay.vram_usage;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setOverlayVRAMUsageEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean enabled)
{
	GetConfig().overlay.vram_usage = enabled;
}

extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_isOverlayDebugEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return GetConfig().overlay.debug;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setOverlayDebugEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean enabled)
{
	GetConfig().overlay.debug = enabled;
}

extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_isOverlayFrameTimeEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return GetConfig().overlay.frame_time;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setOverlayFrameTimeEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean enabled)
{
	GetConfig().overlay.frame_time = enabled;
}

extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_isOverlayDeviceThermalStatusEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return GetConfig().overlay.device_thermal_status;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setOverlayDeviceThermalStatusEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean enabled)
{
	GetConfig().overlay.device_thermal_status = enabled;
}

extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_isOverlayHorizontalLayoutEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return GetConfig().overlay.horizontal_layout;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setOverlayHorizontalLayoutEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean enabled)
{
	GetConfig().overlay.horizontal_layout = enabled;
}

// Stored internally as 0.0-1.0 (see CemuConfig::overlay.background_opacity); converted to/from a
// 0-100 percentage only at this JNI boundary, same convention as the other overlay percentages
// in this file (e.g. getOverlayTextScalePercentage).
extern "C" [[maybe_unused]] JNIEXPORT jint JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_getOverlayBackgroundOpacityPercentage([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return static_cast<jint>(GetConfig().overlay.background_opacity * 100.0f + 0.5f);
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setOverlayBackgroundOpacityPercentage([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jint opacityPercentage)
{
	GetConfig().overlay.background_opacity = static_cast<float>(opacityPercentage) / 100.0f;
}

extern "C" [[maybe_unused]] JNIEXPORT jint JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_getNotificationsPosition([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return static_cast<jint>(GetConfig().notification.position);
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setNotificationsPosition([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jint position)
{
	GetConfig().notification.position = static_cast<ScreenPosition>(position);
}

extern "C" [[maybe_unused]] JNIEXPORT jint JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_getNotificationsTextScalePercentage([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return GetConfig().notification.text_scale;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setNotificationsTextScalePercentage([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jint scalePercentage)
{
	GetConfig().notification.text_scale = scalePercentage;
}

extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_isNotificationControllerProfilesEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return GetConfig().notification.controller_profiles;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setNotificationControllerProfilesEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean enabled)
{
	GetConfig().notification.controller_profiles = enabled;
}

extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_isNotificationShaderCompilerEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return GetConfig().notification.shader_compiling;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setNotificationShaderCompilerEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean enabled)
{
	GetConfig().notification.shader_compiling = enabled;
}

extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_isNotificationFriendListEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return GetConfig().notification.friends;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setNotificationFriendListEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean enabled)
{
	GetConfig().notification.friends = enabled;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_addGamesPath(JNIEnv* env, [[maybe_unused]] jclass clazz, jstring uri)
{
	auto& gamePaths = GetConfig().game_paths;
	auto gamePath = JNIUtils::FromJString(env, uri);
	if (std::any_of(gamePaths.begin(), gamePaths.end(), [&](const auto& path) { return path == gamePath; }))
		return;
	gamePaths.push_back(gamePath);
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_removeGamesPath(JNIEnv* env, [[maybe_unused]] jclass clazz, jstring uri)
{
	auto gamePath = JNIUtils::FromJString(env, uri);
	auto& gamePaths = GetConfig().game_paths;
	std::erase_if(gamePaths, [&](const auto& path) { return path == gamePath; });
}

extern "C" [[maybe_unused]] JNIEXPORT jobjectArray JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_getGamesPaths(JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return JNIUtils::CreateStringObjectArray(env, GetConfig().game_paths);
}

extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_getAsyncShaderCompile([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return GetConfig().async_compile;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setAsyncShaderCompile([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean enabled)
{
	GetConfig().async_compile = enabled;
}

// Recompiler AOT (ahead-of-time) disk cache: persists compiled PPC functions per title so
// a later launch can reuse them instead of recompiling from scratch - see
// PPCRecompilerCache.h for the full explanation and safety details.
extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_getRecompilerAOTCache([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return GetConfig().enable_recompiler_aot_cache;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setRecompilerAOTCache([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean enabled)
{
	GetConfig().enable_recompiler_aot_cache = enabled;
}

// Limitar threads do compilador de shaders: caps both shader/pipeline compiler thread pools at
// 2 threads instead of up to 8 - see VulkanPipelineCompiler.cpp and VulkanPipelineStableCache.cpp.
// Only takes effect on the next title boot, since the thread pools are only created then.
extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_getFsr3FrameGen([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return GetConfig().fsr3_frame_gen;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setFsr3FrameGen([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean enabled)
{
	GetConfig().fsr3_frame_gen = enabled;
}

// Opcoes do FSR3 Frame Gen / FSR Upscale (globais). Chaves (iguais as do perfil por jogo):
//   0 = frame gen on/off (0/1)   1 = multiplicador (2..4)   2 = flow scale % (50/75/100)
//   3 = modo performance (0/1)   4 = limite de FPS base (0,30,40,60)   5 = FSR upscale (0/1)
extern "C" [[maybe_unused]] JNIEXPORT jint JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_getFsr3Option([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jint key)
{
	auto& c = GetConfig();
	switch (key)
	{
	case 0: return c.fsr3_frame_gen.GetValue() ? 1 : 0;
	case 1: return c.fsr3_fg_multiplier.GetValue();
	case 2: return c.fsr3_fg_flow_scale.GetValue();
	case 3: return c.fsr3_fg_performance_mode.GetValue() ? 1 : 0;
	case 4: return c.fsr3_fg_base_fps_limit.GetValue();
	case 5: return c.fsr3_upscale.GetValue() ? 1 : 0;
	}
	return 0;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setFsr3Option([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jint key, jint value)
{
	auto& c = GetConfig();
	switch (key)
	{
	case 0: c.fsr3_frame_gen = value != 0; break;
	case 1: c.fsr3_fg_multiplier = std::clamp<sint32>(value, 2, 4); break;
	case 2: c.fsr3_fg_flow_scale = std::clamp<sint32>(value, 25, 100); break;
	case 3: c.fsr3_fg_performance_mode = value != 0; break;
	case 4: c.fsr3_fg_base_fps_limit = std::clamp<sint32>(value, 0, 240); break;
	case 5: c.fsr3_upscale = value != 0; break;
	}
}

extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_getLimitShaderCompilerThreads([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return GetConfig().limit_shader_compiler_threads;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setLimitShaderCompilerThreads([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean enabled)
{
	GetConfig().limit_shader_compiler_threads = enabled;
}

extern "C" [[maybe_unused]] JNIEXPORT jint JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_getVsyncMode([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return GetConfig().vsync;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setVsyncMode([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jint vsync_mode)
{
	GetConfig().vsync = vsync_mode;
}

// Render API (Experimental/Hackers -> "Render API"): Auto (o emulador escolhe Vulkan ou
// OpenGL ES por jogo - see NativeEmulation_initializeRenderer), Vulkan (somente) ou
// OpenGL ES (somente). Only takes
// effect the next time the renderer is (re)initialized, i.e. next app/emulation launch -
// same restart caveat as "Limit Shader Compiler Threads" above.
extern "C" [[maybe_unused]] JNIEXPORT jint JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_getRendererMode([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return GetConfig().android_renderer_mode;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setRendererMode([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jint mode)
{
	GetConfig().android_renderer_mode = mode;
}

extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_getAccurateBarriers([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return GetConfig().vk_accurate_barriers;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setUpscalingFilter([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jint upscaling_filter)
{
	GetConfig().upscale_filter = upscaling_filter;
}

extern "C" [[maybe_unused]] JNIEXPORT jint JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_getUpscalingFilter([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return GetConfig().upscale_filter;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setDownscalingFilter([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jint downscaling_filter)
{
	GetConfig().downscale_filter = downscaling_filter;
}

extern "C" [[maybe_unused]] JNIEXPORT jint JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_getDownscalingFilter([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return GetConfig().downscale_filter;
}

extern "C" [[maybe_unused]] JNIEXPORT jfloat JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_getFsrSharpness([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return GetConfig().fsr_sharpness;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setFsrSharpness([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jfloat sharpness)
{
	GetConfig().fsr_sharpness = sharpness;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setFullscreenScaling([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jint fullscreen_scaling)
{
	GetConfig().fullscreen_scaling = fullscreen_scaling;
}

extern "C" [[maybe_unused]] JNIEXPORT jint JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_getFullscreenScaling([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return GetConfig().fullscreen_scaling;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setAccurateBarriers([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean enabled)
{
	GetConfig().vk_accurate_barriers = enabled;
}

// Experimental per-format texture compression: transcodes Wii U BC1-BC5 textures to
// ETC2/EAC (Vulkan and OpenGL) when the host GPU lacks native BC/S3TC support - see
// VulkanRenderer::GetTextureFormatInfoVK / OpenGLRenderer::texture_chooseDecodedFormat.
// One toggle per format so the user can pick exactly which (untested-on-real-hardware)
// transcoders to opt into, rather than one all-or-nothing switch.
extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setTextureCompressionBC1([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean value)
{
	GetConfig().texture_compression_bc1 = (bool)value;
}

extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_getTextureCompressionBC1([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return GetConfig().texture_compression_bc1;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setTextureCompressionBC2([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean value)
{
	GetConfig().texture_compression_bc2 = (bool)value;
}

extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_getTextureCompressionBC2([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return GetConfig().texture_compression_bc2;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setTextureCompressionBC3([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean value)
{
	GetConfig().texture_compression_bc3 = (bool)value;
}

extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_getTextureCompressionBC3([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return GetConfig().texture_compression_bc3;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setTextureCompressionBC4([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean value)
{
	GetConfig().texture_compression_bc4 = (bool)value;
}

extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_getTextureCompressionBC4([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return GetConfig().texture_compression_bc4;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setTextureCompressionBC5([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean value)
{
	GetConfig().texture_compression_bc5 = (bool)value;
}

extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_getTextureCompressionBC5([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return GetConfig().texture_compression_bc5;
}

extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_getAudioDeviceEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean tv)
{
	const auto& device = tv ? GetConfig().tv_device : GetConfig().pad_device;
	return !device.empty();
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setAudioDeviceEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean enabled, jboolean tv)
{
	auto& device = tv ? GetConfig().tv_device : GetConfig().pad_device;
	if (enabled)
		device = L"Default";
	else
		device.clear();
}

extern "C" [[maybe_unused]] JNIEXPORT jint JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_getAudioDeviceChannels([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean tv)
{
	const auto& deviceChannels = tv ? GetConfig().tv_channels : GetConfig().pad_channels;
	return deviceChannels;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setAudioDeviceChannels([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jint channels, jboolean tv)
{
	auto& deviceChannels = tv ? GetConfig().tv_channels : GetConfig().pad_channels;
	deviceChannels = static_cast<AudioChannels>(channels);
}

extern "C" [[maybe_unused]] JNIEXPORT jint JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_getAudioDeviceVolume([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean tv)
{
	const auto& deviceVolume = tv ? GetConfig().tv_volume : GetConfig().pad_volume;
	return deviceVolume;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setAudioDeviceVolume([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jint volume, jboolean tv)
{
	auto& deviceVolume = tv ? GetConfig().tv_volume : GetConfig().pad_volume;
	deviceVolume = volume;
}

extern "C" [[maybe_unused]] JNIEXPORT jint JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_getAudioLatency([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return GetConfig().audio_delay * 12;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setAudioLatency([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jint latency)
{
	sint32 audioDelay = latency / 12;
	GetConfig().audio_delay = audioDelay;
	IAudioAPI::SetAudioDelay(audioDelay);
}

// Audio/video sync: continuously trims a small amount of queued audio when it drifts past the
// configured latency, instead of one large drop when the hard buffer limit is hit - see
// CubebAPI::FeedBlock. Off by default (unconfirmed to be inaudible across long sessions yet).
extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_getAudioVideoSync([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return GetConfig().audio_video_sync.GetValue();
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setAudioVideoSync([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean value)
{
	GetConfig().audio_video_sync = (bool)value;
}

extern "C" [[maybe_unused]] JNIEXPORT jint JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_getConsoleLanguage([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return static_cast<jint>(GetConfig().console_language.GetValue());
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setConsoleLanguage([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jint console_language)
{
	GetConfig().console_language = static_cast<CafeConsoleLanguage>(console_language);
}

extern "C" [[maybe_unused]] JNIEXPORT jstring JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_getCustomDriverPath(JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	std::string customDriverPath = GetConfig().custom_driver_path;
	if (customDriverPath.empty())
		return nullptr;
	return JNIUtils::ToJString(env, customDriverPath);
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setCustomDriverPath(JNIEnv* env, [[maybe_unused]] jclass clazz, jstring custom_driver_path)
{
	GetConfig().custom_driver_path = JNIUtils::FromJString(env, custom_driver_path);
}

extern "C" [[maybe_unused]] JNIEXPORT jint JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_getAccountNetworkService([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jint persistent_id)
{
	return static_cast<jint>(GetConfig().GetAccountNetworkService(persistent_id));
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setAccountNetworkService([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jint persistent_id, jint network_service)
{
	GetConfig().SetAccountSelectedService(persistent_id, static_cast<NetworkService>(network_service));
}

extern "C" [[maybe_unused]] JNIEXPORT jint JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_getAccountPersistentId([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return static_cast<jint>(GetConfig().account.m_persistent_id);
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setAccountPersistentId([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jint persistent_id)
{
	GetConfig().account.m_persistent_id = persistent_id;
}

extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_isEmulateSkylanderPortalEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return GetConfig().emulated_usb_devices.emulate_skylander_portal;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setEmulateSkylanderPortalEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean enabled)
{
	GetConfig().emulated_usb_devices.emulate_skylander_portal = enabled;
}


extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_isEmulateInfinityBaseEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return GetConfig().emulated_usb_devices.emulate_infinity_base;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setEmulateInfinityBaseEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean enabled)
{
	GetConfig().emulated_usb_devices.emulate_infinity_base = enabled;
}


extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_isEmulateDimensionsToypadEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return GetConfig().emulated_usb_devices.emulate_dimensions_toypad;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setEmulateDimensionsToypadEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean enabled)
{
	GetConfig().emulated_usb_devices.emulate_dimensions_toypad = enabled;
}

// Deep IR Optimizations: dead code elimination fixpoint loop + redundant IR segment merging -
// see IMLOptimizer.cpp. Off by default; can be turned on to try more thorough recompiler
// cleanup, at the cost of a bug in these passes potentially affecting any game.
extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_isAdvancedIMLOptimizationsEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return GetConfig().advanced_iml_optimizations.GetValue();
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setAdvancedIMLOptimizationsEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean enabled)
{
	GetConfig().advanced_iml_optimizations = enabled;
}

// Fast Audio Resampling: forces every AX voice to nearest-neighbor sample fetching instead of
// whatever mode the game requested (Tap/Linear) - see AXVoiceMix_DecodeSamples in ax_mix.cpp.
extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_isFastAudioResamplingEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return GetConfig().fast_audio_resampling.GetValue();
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setFastAudioResamplingEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean enabled)
{
	GetConfig().fast_audio_resampling = (bool)enabled;
}

// Force Audio Downmix: when TV output is Stereo/Mono, mixes Center/LFE/Surround into the front
// L/R pair (ITU-R BS.775 "Lo/Ro") instead of dropping them - see AXOut_SubmitTVFrame/
// AXOut_SubmitDRCFrame in ax_out.cpp.
extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_isForceAudioDownmixEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return GetConfig().force_audio_downmix.GetValue();
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setForceAudioDownmixEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean enabled)
{
	GetConfig().force_audio_downmix = (bool)enabled;
}

// Limitar o processamento de áudio assíncrono: while the audio buffer is critically low, polls
// the backend more aggressively to try to refill it before it runs dry - see
// g_enableAudioBufferSync in audio/IAudioAPI.h and AXOut_update() in ax_out.cpp.
extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_isAudioBufferSyncEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return GetConfig().audio_buffer_sync.GetValue();
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setAudioBufferSyncEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean enabled)
{
	GetConfig().audio_buffer_sync = (bool)enabled;
	// applied immediately, even with a game already running
	IAudioAPI_SetBufferSyncEnabled(enabled);
}

// keeps AndroidMemoryManager's live ceiling in sync with whatever GetConfig() currently says;
// called after either the master toggle or the chosen ceiling changes
static void UpdateMemoryCeilingFromConfig()
{
	AndroidMemoryManager::SetMemoryCeilingMB(
		GetConfig().memory_usage_brake.GetValue() ? (uint64_t)GetConfig().memory_ceiling_mb.GetValue() : 0);
}

extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_isMemoryUsageBrakeEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return GetConfig().memory_usage_brake.GetValue();
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setMemoryUsageBrakeEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean enabled)
{
	GetConfig().memory_usage_brake = (bool)enabled;
	UpdateMemoryCeilingFromConfig();
}

extern "C" [[maybe_unused]] JNIEXPORT jint JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_getMemoryCeilingMB([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return GetConfig().memory_ceiling_mb.GetValue();
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setMemoryCeilingMB([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jint ceilingMB)
{
	GetConfig().memory_ceiling_mb = ceilingMB;
	UpdateMemoryCeilingFromConfig();
}

extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_hasCustomNetworkConfiguration([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return NetworkConfig::XMLExists();
}

extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_getRtcUseSystemTime([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return static_cast<jboolean>(GetConfig().rtc_use_system_time.GetValue());
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setRtcUseSystemTime([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean useSystemTime)
{
	GetConfig().rtc_use_system_time = static_cast<bool>(useSystemTime);
}

extern "C" [[maybe_unused]] JNIEXPORT jlong JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_getRtcCustomTimestamp([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return static_cast<jlong>(GetConfig().rtc_custom_timestamp.GetValue());
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setRtcCustomTimestamp([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jlong timestamp)
{
	GetConfig().rtc_custom_timestamp = static_cast<sint64>(timestamp);
}

// Quality of Life master switches: whether the corresponding in-game menu button/feature shows
// itself at all. Off by default, like the rest of Experimental/Hackers.
extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_isQuickControllerProfileSwitcherEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return GetConfig().qol_quick_controller_profile_switcher_enabled;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setQuickControllerProfileSwitcherEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean enabled)
{
	GetConfig().qol_quick_controller_profile_switcher_enabled = static_cast<bool>(enabled);
}

// ─── Limit to 30 FPS / Emulation speed (global default) ──────────────────────
// Lives in Experimental/Hackers, off by default (limit_to_half_fps{false},
// emulation_speed_percent{100} — see CemuConfig.h). Per-title profiles can
// override either — see NativeGameTitles' getLimitToHalfFpsForTitle etc.
extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_getLimitToHalfFps([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return GetConfig().limit_to_half_fps.GetValue();
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setLimitToHalfFps([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean enabled)
{
	GetConfig().limit_to_half_fps = static_cast<bool>(enabled);
}

extern "C" [[maybe_unused]] JNIEXPORT jint JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_getEmulationSpeedPercent([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return (jint)GetConfig().emulation_speed_percent.GetValue();
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_setEmulationSpeedPercent([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jint speedPercent)
{
	jint clamped = speedPercent < 100 ? 100 : (speedPercent > 200 ? 200 : speedPercent);
	GetConfig().emulation_speed_percent = (uint32)clamped;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeSettings_saveSettings([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	GetConfigHandle().Save();
}
