#pragma once

#include "Cafe/HW/Latte/Renderer/OpenGL/OpenGLRenderer.h"
#include "Common/GLInclude/GLInclude.h"

#include <android/native_window.h>

// Android equivalent of gui/wxgui/canvas/OpenGLCanvas.cpp's GLCanvasManager - the piece that
// was completely missing on this platform (see the OpenGL-on-Android technical report). Owns
// the single EGLContext shared between the TV and pad (DRC) views, and the two EGLSurfaces
// that context can be made current against, mirroring how VulkanRenderer/WindowSystem already
// treat "main" vs "pad" as two independently-managed surfaces on the same renderer instance.
//
// Lifetime/threading notes:
// - All of this (Initialize, Set*Surface, Clear*Surface, MakeCurrent, SwapBuffers) is only
//   ever called from the single emulator render thread, same as the Vulkan path - Kotlin/JNI
//   entry points (NativeEmulation.cpp) hand over a raw ANativeWindow* and this class does the
//   actual EGL calls synchronously on whatever thread calls them, exactly like
//   VulkanRenderer::InitializeSurface already does for Vulkan.
// - Android destroys/recreates the Surface (and therefore the ANativeWindow) whenever the
//   view is detached (app backgrounded, screen rotated, pad view closed/reopened) - unlike
//   Windows/X11, the EGLContext itself does NOT need to be destroyed for this, only the
//   EGLSurface tied to that particular ANativeWindow. AndroidOpenGLCanvas keeps the context
//   alive across surface churn and only ever recreates the affected EGLSurface.
class AndroidOpenGLCanvas : public OpenGLCanvasCallbacks
{
  public:
	AndroidOpenGLCanvas() = default;
	~AndroidOpenGLCanvas() override;

	AndroidOpenGLCanvas(const AndroidOpenGLCanvas&) = delete;
	AndroidOpenGLCanvas& operator=(const AndroidOpenGLCanvas&) = delete;

	// Creates the EGLDisplay/EGLConfig/EGLContext and an initial EGLSurface for the TV/main
	// window. Must be called exactly once, before any Set*Surface/MakeCurrent/SwapBuffers
	// call. Returns false (and logs the EGL error) on failure.
	bool Initialize(ANativeWindow* mainWindow);

	// (Re)creates the TV/main EGLSurface for a new ANativeWindow - e.g. after the Activity's
	// Surface was destroyed and recreated (background/foreground, rotation). Destroys the
	// previous EGLSurface first, if any. Does not take ownership of oldWindow release - the
	// caller (NativeEmulation.cpp, matching the existing ANativeWindow_release calls already
	// done there for the Vulkan path) is responsible for that, same as today.
	void SetMainSurface(ANativeWindow* window);

	// Same as SetMainSurface, but for the pad (DRC) view, which can be opened/closed
	// independently of the TV view at any time during a play session.
	void SetPadSurface(ANativeWindow* window);

	// Destroys the pad EGLSurface, e.g. when the user closes the pad view.
	void ClearPadSurface();

	// OpenGLCanvasCallbacks
	bool HasPadViewOpen() const override;
	bool MakeCurrent(bool padView) override;
	void SwapBuffers(bool swapTV, bool swapDRC) override;

  private:
	EGLSurface CreateWindowSurface(ANativeWindow* window) const;
	void LogEGLError(const char* where) const;

	EGLDisplay m_display = EGL_NO_DISPLAY;
	EGLConfig m_config = nullptr;
	EGLContext m_context = EGL_NO_CONTEXT;

	EGLSurface m_mainSurface = EGL_NO_SURFACE;
	EGLSurface m_padSurface = EGL_NO_SURFACE;

	// Tracks which surface the context is currently bound to, purely to avoid redundant
	// eglMakeCurrent calls (each one is a driver round-trip) when the same view keeps
	// swapping buffers repeatedly, which is the common case.
	bool m_currentIsPad = false;
	bool m_hasCurrent = false;
};

// Global instance, analogous to g_renderer for the renderer itself - created by
// NativeEmulation_initializeRenderer when the user has selected (or Automatic resolves to)
// the OpenGL backend, and used from every other NativeEmulation_* JNI entry point that
// receives a Surface (setSurface, initializeSurface, clearPadSurface) to know which backend
// to forward that surface to instead of unconditionally assuming Vulkan.
extern std::unique_ptr<AndroidOpenGLCanvas> g_androidOpenGLCanvas;
