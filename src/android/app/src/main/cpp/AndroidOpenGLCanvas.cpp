#include "Common/DiagnosticsFlags.h"
#include "AndroidOpenGLCanvas.h"
#include <unistd.h>

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

	// V11.44: contexto GLES "robusto". Jogos do Wii U leem uniform buffers / vertex buffers alem do tamanho ligado e o
	// hardware do Wii U devolve 0. Em GPUs sem protecao propria (PowerVR confirmado pela Imagination: "PowerVR lacks this
	// safety layer"), a leitura fora do limite vira lixo -> texturas pretas/com ruido. Com robust access o driver devolve 0.
	// Tenta EGL 1.5 (0x31B2), depois EGL_EXT_create_context_robustness (0x30BF), por fim sem robustez.
	constexpr EGLint kEglContextOpenGLRobustAccess = 0x31B2;
	constexpr EGLint kEglContextOpenGLRobustAccessExt = 0x30BF;
	m_context = EGL_NO_CONTEXT;
	// V11.47: na Adreno (kgsl) o contexto robusto QUEBRA o compilador de shader do driver ("Assertion failed: GVI &&
	// cannot compute gv size for oob" no link, log do Zelda BOTW) e derruba o readback de texturas. A Adreno ja
	// zera leituras fora do limite, entao la o contexto robusto fica desligado; so as demais GPUs (PowerVR/Mali) tentam.
	const bool isAdrenoKgsl = (access("/dev/kgsl-3d0", F_OK) == 0);
	if (isAdrenoKgsl)
		cemuLog_log(LogType::Force, "[GL] GPU Adreno (kgsl) detectada: contexto robusto desativado (quebra o compilador de shader da Adreno)");
	const EGLint robustCandidates[3] = { isAdrenoKgsl ? (EGLint)0 : kEglContextOpenGLRobustAccess, isAdrenoKgsl ? (EGLint)0 : kEglContextOpenGLRobustAccessExt, (EGLint)0 };
	for (const EGLint robustAttr : robustCandidates)
	{
		const EGLint robustAttribs[] = {
			EGL_CONTEXT_CLIENT_VERSION, 3,
			robustAttr, EGL_TRUE,
			EGL_NONE,
		};
		const EGLint plainAttribs[] = {
			EGL_CONTEXT_CLIENT_VERSION, 3,
			EGL_NONE,
		};
		m_context = EGL_NO_CONTEXT;
		if (!robustAttr && Diag::IsEnabled(Diag::Flag::GLDebug))
		{
			// "Log profundo do OpenGL": pede contexto de DEPURACAO (EGL_CONTEXT_FLAGS_KHR=0x30FC, EGL_CONTEXT_OPENGL_DEBUG_BIT_KHR=1);
			// sem isso muitos drivers (Adreno) quase nao emitem mensagens de KHR_debug. Se o driver recusar, cai no contexto normal.
			const EGLint debugAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, 0x30FC, 1, EGL_NONE };
			m_context = eglCreateContext(m_display, m_config, EGL_NO_CONTEXT, debugAttribs);
			if (m_context != EGL_NO_CONTEXT)
				cemuLog_log(LogType::Force, "[GL] contexto EGL de DEPURACAO criado (Log profundo do OpenGL ligado)");
		}
		if (m_context == EGL_NO_CONTEXT)
			m_context = eglCreateContext(m_display, m_config, EGL_NO_CONTEXT, robustAttr ? robustAttribs : plainAttribs);
		if (m_context != EGL_NO_CONTEXT)
		{
			cemuLog_log(LogType::Force, "[GL] contexto EGL criado {}", robustAttr ? (robustAttr == kEglContextOpenGLRobustAccess ? "COM acesso robusto a buffers (EGL 1.5)" : "COM acesso robusto a buffers (EXT)") : "SEM acesso robusto a buffers (driver recusou)");
			break;
		}
	}
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
