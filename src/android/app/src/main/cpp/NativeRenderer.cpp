#include "JNIUtils.h"

#include "Cafe/HW/Latte/Renderer/Renderer.h"
#include "config/CemuConfig.h"

/*
 * Quick Screenshot (Android) - Quality of Life / Experimental-Hackers.
 *
 * This does NOT reimplement screenshot capture from scratch. Cemu already has a complete,
 * working "clean screenshot" pipeline shared by every backend (OpenGL/Vulkan/Metal) and used by
 * the desktop F12 hotkey - see Renderer::RequestScreenshot()/HandleScreenshotRequest()/
 * SaveScreenshot() in Renderer.h/.cpp, and the Vulkan capture implementation in
 * VulkanRenderer.cpp. That pipeline already captures the Wii U's own TV/Gamepad output texture
 * BEFORE the FPS/notification overlay is drawn onto it (see the call order of
 * HandleScreenshotRequest() vs LatteOverlay_render() in LatteRenderTarget.cpp) - so the captured
 * pixels are already free of any overlay drawn by Cemu itself. On Android specifically, the
 * on-screen touch controls and any in-app HUD are drawn by entirely separate Android Views (see
 * InputOverlaySurfaceView / the Compose UI in EmulationScreen.kt) that are composited on top by
 * the OS window manager and were never part of this texture to begin with. In other words: there
 * is nothing to "hide" before capturing - the capture is clean by construction, on every
 * platform, including this one.
 *
 * What this file adds is only the missing Android-side plumbing: a JNI entry point to trigger a
 * capture, and a callback that hands the captured pixels back to Kotlin (as a packed ARGB32 int
 * array, alpha forced to opaque) so they can be encoded to PNG and saved via Android's MediaStore
 * API - see NativeRenderer.kt.
 */
namespace NativeRenderer
{
	JNIUtils::Scopedjclass s_nativeRendererClass;
	jmethodID s_onScreenshotCapturedMethodId = nullptr;

	// Must be called once from a JNI entry function (i.e. a thread the JVM itself attached, which
	// has proper classloader access) before the class is ever looked up from the renderer's own
	// background save-thread, which does not have that classloader access - see JNIUtils::GetEnv().
	void EnsureInitialized(JNIEnv* env)
	{
		if (*s_nativeRendererClass)
			return;
		s_nativeRendererClass = JNIUtils::Scopedjclass("info/cemu/cemu/nativeinterface/NativeRenderer");
		s_onScreenshotCapturedMethodId = env->GetStaticMethodID(*s_nativeRendererClass, "onScreenshotCaptured", "([IIIZ)Z");
	}

	// Matches Renderer::ScreenshotSaveFunction. Called on the renderer's own detached background
	// thread (see Renderer::SaveScreenshot in Renderer.cpp), not the render thread and not a
	// JNI-originated thread - JNIUtils::GetEnv() takes care of attaching it.
	std::optional<std::string> AndroidSaveScreenshot(const std::vector<uint8>& rgb_data, int width, int height, bool mainWindow)
	{
		if (!*s_nativeRendererClass || !s_onScreenshotCapturedMethodId)
			return std::nullopt; // triggerScreenshot() was never called from Kotlin - shouldn't happen

		// rgb_data is tightly packed RGB888 (3 bytes/pixel, no alpha) - see
		// VulkanRenderer::HandleScreenshotRequest(). Repack to ARGB32 ints (alpha forced opaque)
		// for Bitmap.setPixels() on the Kotlin side, which has a well-documented, byte-order-safe
		// int format, unlike manually building a raw Bitmap buffer over JNI.
		std::vector<jint> argbPixels;
		argbPixels.reserve(rgb_data.size() / 3);
		for (size_t i = 0; i + 2 < rgb_data.size(); i += 3)
		{
			jint packed = (jint)(0xFF000000u | ((uint32)rgb_data[i] << 16) | ((uint32)rgb_data[i + 1] << 8) | (uint32)rgb_data[i + 2]);
			argbPixels.push_back(packed);
		}

		JNIEnv* env = JNIUtils::GetEnv();
		jintArray jPixels = env->NewIntArray((jsize)argbPixels.size());
		env->SetIntArrayRegion(jPixels, 0, (jsize)argbPixels.size(), argbPixels.data());
		env->CallStaticBooleanMethod(*s_nativeRendererClass, s_onScreenshotCapturedMethodId, jPixels, (jint)width, (jint)height, (jboolean)mainWindow);
		env->DeleteLocalRef(jPixels);

		// Returning a string here would show as an in-emulator overlay notification (like the
		// desktop build does) - on Android the Kotlin side already gives its own feedback (an
		// immediate snackbar when the button is tapped, then a toast once this callback confirms
		// success/failure), so we deliberately don't also push a Latte overlay notification.
		return std::nullopt;
	}
} // namespace NativeRenderer

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeRenderer_triggerScreenshot([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	NativeRenderer::EnsureInitialized(env);
	if (g_renderer)
		g_renderer->RequestScreenshot(NativeRenderer::AndroidSaveScreenshot);
}

// Quality of Life master switch (see CemuConfig::qol_quick_screenshot_enabled): lets the "Take
// Screenshot" button on the in-game menu (EmulationScreen.kt) know whether to show itself at all.
extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeRenderer_isQuickScreenshotEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return GetConfig().qol_quick_screenshot_enabled;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeRenderer_setQuickScreenshotEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean enabled)
{
	GetConfig().qol_quick_screenshot_enabled = static_cast<bool>(enabled);
}
