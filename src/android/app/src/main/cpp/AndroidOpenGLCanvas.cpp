#include "AndroidOpenGLCanvas.h"

std::unique_ptr<AndroidOpenGLCanvas> g_androidOpenGLCanvas;

void AndroidOpenGLCanvas::LogEGLError(const char* where) const
{
	EGLint err = eglGetError ? eglGetError() : EGL_SUCCESS;
	cemuLog_log(LogType::Force, "AndroidOpenGLCanvas: {} failed (EGL error 0x{:04x})", where, (uint32)err);
}

// definida em OpenGLRenderer.cpp (ramo Android)
bool LoadEGLImportsAndroid();

bool AndroidOpenGLCanvas::Initialize(ANativeWindow* mainWindow)
{
	// CORRECAO: as funcoes EGL sao ponteiros carregados por dlsym. Antes desta correcao eles so eram carregados
	// em OpenGLRenderer::Initialize() - DEPOIS deste metodo - e o primeiro eglGetDisplay chamava um ponteiro nulo (pc=0).
	if (!LoadEGLImportsAndroid())
		return false;
	if (!eglGetDisplay || !eglInitialize || !eglBindAPI || !eglChooseConfig || !eglCreateContext || !eglCreateWindowSurface || !eglMakeCurrent || !eglSwapBuffers)
	{
		cemuLog_log(LogType::Force, "AndroidOpenGLCanvas: funcoes EGL essenciais nao encontradas na libEGL.so");
		return false;
	}
	m_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (m_display == EGL_NO_DISPLAY)
	{
		LogEGLError("eglGetDisplay");
		return false;
	}

	EGLint versionMajor = 0, versionMinor = 0;
	if (!eglInitialize(m_display, &versionMajor, &versionMinor))
	{
		LogEGLError("eglInitialize");
		m_display = EGL_NO_DISPLAY;
		return false;
	}
	cemuLog_log(LogType::Force, "AndroidOpenGLCanvas: EGL {}.{} initialized", (int)versionMajor, (int)versionMinor);

	if (!eglBindAPI(EGL_OPENGL_ES_API))
	{
		LogEGLError("eglBindAPI(EGL_OPENGL_ES_API)");
		return false;
	}

	// Ask for a GLES 3.x-capable config with a real window surface, 8/8/8/8 color (matches
	// what the Vulkan path already assumes - see VulkanRenderer's swapchain format choice)
	// and a combined depth/stencil buffer (needed for ImGui's overlay and any GLES-side
	// blit/backbuffer passes - the actual Wii U render targets are always separate textures
	// with their own storage, this is only the default framebuffer's own depth/stencil).
	const EGLint configAttribs[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 8,
		EGL_DEPTH_SIZE, 24,
		EGL_STENCIL_SIZE, 8,
		EGL_NONE,
	};
	EGLint numConfigs = 0;
	if (!eglChooseConfig(m_display, configAttribs, &m_config, 1, &numConfigs) || numConfigs == 0)
	{
		LogEGLError("eglChooseConfig");
		return false;
	}

	const EGLint contextAttribs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 3,
		EGL_NONE,
	};
	m_context = eglCreateContext(m_display, m_config, EGL_NO_CONTEXT, contextAttribs);
	if (m_context == EGL_NO_CONTEXT)
	{
		LogEGLError("eglCreateContext");
		return false;
	}

	m_mainSurface = CreateWindowSurface(mainWindow);
	if (m_mainSurface == EGL_NO_SURFACE)
		return false;

	return true;
}

EGLSurface AndroidOpenGLCanvas::CreateWindowSurface(ANativeWindow* window) const
{
	if (!window)
		return EGL_NO_SURFACE;
	EGLSurface surface = eglCreateWindowSurface(m_display, m_config, (EGLNativeWindowType)window, nullptr);
	if (surface == EGL_NO_SURFACE)
		LogEGLError("eglCreateWindowSurface");
	return surface;
}

AndroidOpenGLCanvas::~AndroidOpenGLCanvas()
{
	if (m_display == EGL_NO_DISPLAY)
		return;
	// unbind before destroying anything that might currently be current
	eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	if (m_padSurface != EGL_NO_SURFACE)
		eglDestroySurface(m_display, m_padSurface);
	if (m_mainSurface != EGL_NO_SURFACE)
		eglDestroySurface(m_display, m_mainSurface);
	if (m_context != EGL_NO_CONTEXT)
		eglDestroyContext(m_display, m_context);
	eglTerminate(m_display);
}

void AndroidOpenGLCanvas::SetMainSurface(ANativeWindow* window)
{
	if (m_hasCurrent && !m_currentIsPad)
	{
		eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		m_hasCurrent = false;
	}
	if (m_mainSurface != EGL_NO_SURFACE)
	{
		eglDestroySurface(m_display, m_mainSurface);
		m_mainSurface = EGL_NO_SURFACE;
	}
	m_mainSurface = CreateWindowSurface(window);
}

void AndroidOpenGLCanvas::SetPadSurface(ANativeWindow* window)
{
	if (m_hasCurrent && m_currentIsPad)
	{
		eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		m_hasCurrent = false;
	}
	if (m_padSurface != EGL_NO_SURFACE)
	{
		eglDestroySurface(m_display, m_padSurface);
		m_padSurface = EGL_NO_SURFACE;
	}
	m_padSurface = CreateWindowSurface(window);
}

void AndroidOpenGLCanvas::ClearPadSurface()
{
	if (m_hasCurrent && m_currentIsPad)
	{
		eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		m_hasCurrent = false;
	}
	if (m_padSurface != EGL_NO_SURFACE)
	{
		eglDestroySurface(m_display, m_padSurface);
		m_padSurface = EGL_NO_SURFACE;
	}
}

bool AndroidOpenGLCanvas::HasPadViewOpen() const
{
	return m_padSurface != EGL_NO_SURFACE;
}

bool AndroidOpenGLCanvas::MakeCurrent(bool padView)
{
	EGLSurface surface = padView ? m_padSurface : m_mainSurface;
	if (surface == EGL_NO_SURFACE)
		return false;
	if (m_hasCurrent && m_currentIsPad == padView)
		return true; // already current, skip the redundant driver call
	if (!eglMakeCurrent(m_display, surface, surface, m_context))
	{
		LogEGLError("eglMakeCurrent");
		return false;
	}
	m_currentIsPad = padView;
	m_hasCurrent = true;
	return true;
}

void AndroidOpenGLCanvas::SwapBuffers(bool swapTV, bool swapDRC)
{
	if (swapTV && m_mainSurface != EGL_NO_SURFACE)
	{
		MakeCurrent(false);
		eglSwapBuffers(m_display, m_mainSurface);
	}
	if (swapDRC && m_padSurface != EGL_NO_SURFACE)
	{
		MakeCurrent(true);
		eglSwapBuffers(m_display, m_padSurface);
	}
	MakeCurrent(false);
}
