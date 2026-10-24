#include "Common/DiagnosticsFlags.h"
#include <cstring>
#include "Cafe/HW/Latte/Renderer/OpenGL/OpenGLRenderer.h"
#include "WindowSystem.h"

#include "Cafe/HW/Latte/Core/LatteRingBuffer.h"
#include "Cafe/HW/Latte/Core/LatteDraw.h"
#include "Cafe/HW/Latte/Core/LatteOverlay.h"

#include "Cafe/HW/Latte/Renderer/OpenGL/LatteTextureGL.h"
#include "Cafe/HW/Latte/Renderer/OpenGL/LatteTextureViewGL.h"
#include "Cafe/HW/Latte/Renderer/OpenGL/RendererShaderGL.h"
#include "Cafe/HW/Latte/Renderer/OpenGL/CachedFBOGL.h"
#include "Cafe/HW/Latte/Renderer/OpenGL/OpenGLTextureReadback.h"
#include "Cafe/HW/Latte/Renderer/OpenGL/FrameGenGL.h"

#include "Cafe/HW/Latte/LegacyShaderDecompiler/LatteDecompiler.h"

#include "imgui/imgui_impl_opengl3.h"
#include "imgui/imgui_extension.h"

#include "Cafe/HW/Latte/ISA/RegDefines.h"
#include "Cafe/OS/libs/gx2/GX2.h"
#include "Cafe/HW/Latte/Core/LatteTextureLoaderRGBA8.h"

class DefaultOpenGLCanvasCallbacks : public OpenGLCanvasCallbacks
{
} g_defaultOpenGLCanvasCallbacks;

OpenGLCanvasCallbacks* g_openGLCanvasCallbacks = &g_defaultOpenGLCanvasCallbacks;

void SetOpenGLCanvasCallbacks(OpenGLCanvasCallbacks* callbacks)
{
	cemu_assert_debug(g_openGLCanvasCallbacks == &g_defaultOpenGLCanvasCallbacks);
	g_openGLCanvasCallbacks = callbacks;
}

void ClearOpenGLCanvasCallbacks()
{
	cemu_assert_debug(g_openGLCanvasCallbacks != &g_defaultOpenGLCanvasCallbacks);
	g_openGLCanvasCallbacks = &g_defaultOpenGLCanvasCallbacks;
}

bool GLCanvas_HasPadViewOpen()
{
	return g_openGLCanvasCallbacks->HasPadViewOpen();
}

bool GLCanvas_MakeCurrent(bool padView)
{
	return g_openGLCanvasCallbacks->MakeCurrent(padView);
}

void GLCanvas_SwapBuffers(bool swapTV, bool swapDRC)
{
	g_openGLCanvasCallbacks->SwapBuffers(swapTV, swapDRC);
}

#define STRINGIFY2(X) #X
#define STRINGIFY(X) STRINGIFY2(X)

namespace CemuGL
{
#define GLFUNC(__type, __name)	__type __name;
#define EGLFUNC(__type, __name)	__type __name;
#include "Common/GLInclude/glFunctions.h"
#undef GLFUNC
#undef EGLFUNC
}

#include "config/ActiveSettings.h"
#include "config/LaunchSettings.h"

static const int TEXBUFFER_SIZE = 1024 * 1024 * 32; // 32MB

struct
{
	// options
	bool useTextureUploadBuffer;
	// texture upload
	GLuint uploadBuffer;
	sint32 uploadIndex;
	void* uploadBufferPtr;
	LatteRingBuffer_t* uploadRingBuffer;
	// current texture work buffer (subrange of uploadRingBuffer)
	uint8* texWorkBuffer;
	sint32 texWorkBufferSize;
	// texture upload buffer (when not using persistent buffer)
	std::vector<uint8> texUploadBuffer;
	// FBO for fast clearing (on Nvidia or if glClearTexSubImage is not supported)
	GLuint clearFBO;
}glRendererState;

static const GLenum glDepthFuncTable[] =
{
	GL_NEVER,
	GL_LESS,
	GL_EQUAL,
	GL_LEQUAL,
	GL_GREATER,
	GL_NOTEQUAL,
	GL_GEQUAL,
	GL_ALWAYS
};

static const GLenum glAlphaTestFunc[] =
{
	GL_NEVER,
	GL_LESS,
	GL_EQUAL,
	GL_LEQUAL,
	GL_GREATER,
	GL_NOTEQUAL,
	GL_GEQUAL,
	GL_ALWAYS
};

OpenGLRenderer::OpenGLRenderer() : Renderer(RendererAPI::OpenGL)
{
	glRendererState.useTextureUploadBuffer = false;
	if (glRendererState.useTextureUploadBuffer)
	{
		glCreateBuffers(1, &glRendererState.uploadBuffer);
		glNamedBufferStorage(glRendererState.uploadBuffer, TEXBUFFER_SIZE, nullptr, GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT);
		void* buffer = glMapNamedBufferRange(glRendererState.uploadBuffer, 0, TEXBUFFER_SIZE, GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_UNSYNCHRONIZED_BIT | GL_MAP_FLUSH_EXPLICIT_BIT);
		if (buffer == nullptr)
		{
			cemuLog_log(LogType::Force, "Failed to allocate GL texture upload buffer. Using traditional API instead");
			cemu_assert_debug(false);
		}
		glRendererState.uploadBufferPtr = buffer;
		glRendererState.uploadRingBuffer = LatteRingBuffer_create((uint8*)buffer, TEXBUFFER_SIZE);
		glRendererState.uploadIndex = 0;
	}

#if BOOST_OS_WINDOWS
	try
	{
		m_dxgi_wrapper = std::make_unique<DXGIWrapper>();
	}
	catch (const std::exception& ex)
	{
		cemuLog_log(LogType::Force, "Unable to create dxgi wrapper: {} (VRAM overlay stat won't be available)", ex.what());
	}
#endif
}

OpenGLRenderer::~OpenGLRenderer()
{
	if(m_pipeline != 0)
		glDeleteProgramPipelines(1, &m_pipeline);

	glDeleteBuffers(1, &m_backbufferBlit_uniformBuffer);

	if (m_fsrIntermediateFBO)
		glDeleteFramebuffers(1, &m_fsrIntermediateFBO);
	if (m_fsrIntermediateTexture)
		glDeleteTextures(1, &m_fsrIntermediateTexture);
}

OpenGLRenderer* OpenGLRenderer::GetInstance()
{
	cemu_assert_debug(g_renderer && dynamic_cast<OpenGLRenderer*>(g_renderer.get()));
	return (OpenGLRenderer*)g_renderer.get();
}

bool OpenGLRenderer::ImguiBegin(bool mainWindow)
{
	if (!mainWindow)
	{
		GLCanvas_MakeCurrent(true);
		m_isPadViewContext = true;
	}

	if(!Renderer::ImguiBegin(mainWindow))
		return false;
	
	renderstate_resetColorControl();
	renderstate_resetDepthControl();
	renderstate_resetStencilMask();

	if (glClipControl)
		glClipControl(GL_LOWER_LEFT, GL_NEGATIVE_ONE_TO_ONE);

	ImGui_ImplOpenGL3_NewFrame();
	ImGui_UpdateWindowInformation(mainWindow);
	ImGui::NewFrame();
	return true;
}

void OpenGLRenderer::ImguiEnd()
{
	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

	if (m_isPadViewContext)
	{
		GLCanvas_MakeCurrent(false);
		m_isPadViewContext = false;
	}

	if (glClipControl)
		glClipControl(GL_UPPER_LEFT, GL_NEGATIVE_ONE_TO_ONE);
}

ImTextureID OpenGLRenderer::GenerateTexture(const std::vector<uint8>& data, const Vector2i& size)
{
	GLuint textureId;
	glGenTextures(1, &textureId);

	glBindTexture(GL_TEXTURE_2D, textureId);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	// GLES: GL_CLAMP nao existe (GL_INVALID_ENUM) e o formato interno precisa ser sized (GL_SRGB8) - o PowerVR/Adreno registravam
	// "TexParameter INVALID_ENUM" + "glTexImage2D INVALID_VALUE" em toda abertura, deixando essa textura da interface sem dados.
	const bool gles = IsGLES();
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, gles ? GL_CLAMP_TO_EDGE : GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, gles ? GL_CLAMP_TO_EDGE : GL_CLAMP);

	glActiveTexture(GL_TEXTURE0);
	GLint prevUnpackAlignment = 4;
	glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevUnpackAlignment);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1); // linhas RGB de 3 bytes/pixel nao sao multiplas de 4
	glTexImage2D(GL_TEXTURE_2D, 0, gles ? GL_SRGB8 : GL_SRGB, size.x, size.y, 0, GL_RGB, GL_UNSIGNED_BYTE, data.data());
	glPixelStorei(GL_UNPACK_ALIGNMENT, prevUnpackAlignment);
	return (ImTextureID)(uintptr_t)textureId;
}

void OpenGLRenderer::DeleteTexture(ImTextureID id)
{
	if (id)
	{
		GLuint textureId = (GLuint)(uintptr_t)id;
		glDeleteTextures(1, &textureId);
	}
}

void OpenGLRenderer::DeleteFontTextures()
{
	ImGui_ImplOpenGL3_DestroyFontsTexture();
}

typedef void(*GL_IMPORT)();

#if BOOST_OS_WINDOWS
GL_IMPORT _GetOpenGLFunction(HMODULE hLib, const char* name)
{
	GL_IMPORT r = (GL_IMPORT)wglGetProcAddress(name);
	if (r == nullptr)
		r = (GL_IMPORT)GetProcAddress(hLib, name);
	return r;
}

void LoadOpenGLImports()
{
	HMODULE hLib = LoadLibraryA("opengl32.dll");
#define GLFUNC(__type, __name)	__name = (__type)_GetOpenGLFunction(hLib, STRINGIFY(__name));
#include "Common/GLInclude/glFunctions.h"
#undef GLFUNC
}
#elif (BOOST_OS_LINUX || BOOST_OS_BSD) && !defined(__ANDROID__)
GL_IMPORT _GetOpenGLFunction(void* hLib, PFNGLXGETPROCADDRESSPROC func, const char* name)
{
	GL_IMPORT r = (GL_IMPORT)func((const GLubyte*)name);
	return r;
}

#include <dlfcn.h>
// #define RTLD_NOW        	0x00002
// #define RTLD_GLOBAL        	0x00100

void LoadOpenGLImports()
{
	PFNGLXGETPROCADDRESSPROC _glXGetProcAddress = nullptr;
	void* libGL = dlopen("libGL.so.1", RTLD_NOW | RTLD_GLOBAL);
	_glXGetProcAddress = (PFNGLXGETPROCADDRESSPROC)dlsym(libGL, "glXGetProcAddressARB");
	if(!_glXGetProcAddress)
	{
		libGL = dlopen("libGL.so", RTLD_NOW | RTLD_GLOBAL);
		_glXGetProcAddress = (PFNGLXGETPROCADDRESSPROC)dlsym(libGL, "glXGetProcAddressARB");
	}

	void* libEGL = dlopen("libEGL.so.1", RTLD_NOW | RTLD_GLOBAL);
	if(!libEGL)
	{
		libGL = dlopen("libEGL.so", RTLD_NOW | RTLD_GLOBAL);
	}

#define GLFUNC(__type, __name)	__name = (__type)_GetOpenGLFunction(libGL, _glXGetProcAddress, STRINGIFY(__name));
#define EGLFUNC(__type, __name)	__name = (__type)dlsym(libEGL, STRINGIFY(__name));
#include "Common/GLInclude/glFunctions.h"
#undef GLFUNC
#undef EGLFUNC
}

// dummy function for all code that is statically linked with cemu and attempts to use eglSwapInterval
// used to suppress wxWidgets calls to eglSwapInterval
extern "C"
EGLAPI EGLBoolean EGLAPIENTRY eglSwapInterval(EGLDisplay dpy, EGLint interval)
{
	return EGL_TRUE;
}

#elif defined(__ANDROID__)
#include <dlfcn.h>
#include <mutex>
#include <unordered_map>
#include "Cafe/HW/Latte/Core/RendererInfo.h"

#if defined(__ANDROID__)
static void _glDebugCallbackAndroid(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam);
#endif

// GLES: estas caps so existem no GL desktop. glEnable/glDisable com elas gera GL_INVALID_ENUM ("cap is not one of the values accepted by
// this function" no log do PowerVR/Adreno). No GLES o sRGB do framebuffer e automatico (formato sRGB do anexo) e o tamanho do ponto vem
// do shader, entao pular a chamada e o comportamento correto, nao uma perda.
static inline bool IsDesktopOnlyCapGLES(GLenum cap)
{
	return OpenGLRenderer::IsGLES() && (cap == GL_FRAMEBUFFER_SRGB || cap == GL_PROGRAM_POINT_SIZE || cap == GL_POINT_SPRITE || cap == GL_ALPHA_TEST || cap == GL_COLOR_LOGIC_OP);
}
static inline void CapEnableGLES(GLenum cap) { if (!IsDesktopOnlyCapGLES(cap)) glEnable(cap); }
static inline void CapDisableGLES(GLenum cap) { if (!IsDesktopOnlyCapGLES(cap)) glDisable(cap); }
#include <unordered_set>
#include <cstring>
#include <string>

// Mirrors the Linux/BSD dlopen+dlsym approach above, but against libEGL.so/libGLESv3.so
// instead of libGL.so/libGLX. Unlike desktop GLX, eglGetProcAddress is guaranteed by the
// EGL spec to resolve ALL GLES functions, core and extension alike (there is no
// "must already be in libGLESv3.so's export table" restriction the way there historically
// was on some desktop GL drivers) - so it's tried first, with a dlsym fallback for
// robustness on drivers that are stricter about what eglGetProcAddress accepts before a
// context is current. Note that this is expected to return nullptr for every desktop-only
// entry point that plain doesn't exist on GLES (DSA functions, 1D textures, glGetTexImage,
// etc.) - that's by design, see the DSA fallback wrappers in GLInclude.h and the dedicated
// GLES codepaths in LatteTextureGL.cpp/TextureReadbackGL.cpp for how those are handled.
static void* g_libGLESv3 = nullptr;
static void* g_libEGL = nullptr;

GL_IMPORT _GetOpenGLFunction(void* libGLESv3, const char* name)
{
	GL_IMPORT r = nullptr;
	if (g_libEGL)
	{
		using PFNEGLGETPROCADDRESS = void* (*)(const char*);
		static auto _eglGetProcAddress = (PFNEGLGETPROCADDRESS)dlsym(g_libEGL, "eglGetProcAddress");
		if (_eglGetProcAddress)
			r = (GL_IMPORT)_eglGetProcAddress(name);
	}
	if (!r && libGLESv3)
		r = (GL_IMPORT)dlsym(libGLESv3, name);
	return r;
}

// ---------------------------------------------------------------------------------------------
// CORRECAO DO CRASH "pc=0" DO OPENGL NO ANDROID
// AndroidOpenGLCanvas::Initialize() chama eglGetDisplay/eglInitialize/... ANTES de o OpenGLRenderer
// existir, mas esses ponteiros EGL so eram preenchidos dentro de OpenGLRenderer::Initialize()
// (LoadOpenGLImports). Resultado: chamada a ponteiro nulo -> SIGSEGV com pc=0.
// LoadEGLImportsAndroid() carrega SO as funcoes EGL (nao precisa de contexto) e e chamada no
// inicio de AndroidOpenGLCanvas::Initialize().
// ---------------------------------------------------------------------------------------------
bool LoadEGLImportsAndroid()
{
	if (!g_libEGL)
		g_libEGL = dlopen("libEGL.so", RTLD_NOW | RTLD_GLOBAL);
	if (!g_libEGL)
	{
		cemuLog_log(LogType::Force, "[GL] ERRO: dlopen(libEGL.so) falhou");
		return false;
	}
#define GLFUNC(__type, __name)
#define EGLFUNC(__type, __name)	__name = (__type)dlsym(g_libEGL, STRINGIFY(__name));
#include "Common/GLInclude/glFunctions.h"
#undef GLFUNC
#undef EGLFUNC
	return true;
}

// ---------------------------------------------------------------------------------------------
// REDE DE SEGURANCA: funcoes de OpenGL desktop que nao existem no GLES (ex.: glPrimitiveRestartIndex,
// glDrawBuffer, glClearDepth, glPolygonMode...) ficam NULL e uma chamada direta derruba o app (pc=0).
// Para cada funcao ausente que o codigo SEMPRE chama diretamente (nunca testa se e nula), instalamos
// um stub que registra o nome UMA vez no log e retorna 0. As funcoes que o Cemu usa para DETECTAR
// recursos (kFeatureDetected - gerada por varredura do codigo) continuam NULL de proposito.
// ---------------------------------------------------------------------------------------------
static const char* const kFeatureDetected[] = {
	"glBeginTransformFeedback",
	"glBindTexture",
	"glBindTextureUnit",
	"glBufferStorage",
	"glClearTexImage",
	"glClearTexSubImage",
	"glClipControl",
	"glCompressedTextureSubImage1D",
	"glCompressedTextureSubImage2D",
	"glCompressedTextureSubImage3D",
	"glCopyImageSubData",
	"glCopyImageSubDataEXT",
	"glCopyImageSubDataOES",
	"glCopyNamedBufferSubData",
	"glCreateBuffers",
	"glCreateFramebuffers",
	"glCreateTextures",
	"glDepthRangedNV",
	"glGenTextures",
	"glGetProgramBinary",
	"glGetTextureSubImage",
	"glMaxShaderCompilerThreadsARB",
	"glNamedBufferStorage",
	"glNamedBufferSubData",
	"glProgramBinary",
	"glTextureBarrier",
	"glTextureParameteri",
	"glTextureStorage1D",
	"glTextureStorage2D",
	"glTextureStorage3D",
	"glTextureSubImage1D",
	"glTextureSubImage2D",
	"glTextureSubImage3D",
	"glTextureView",
	"glTextureViewEXT",
	"glTextureViewOES",
	"wglSwapIntervalEXT"
};

static bool IsFeatureDetectedFunction(const char* name)
{
	for (const char* n : kFeatureDetected)
		if (strcmp(n, name) == 0)
			return true;
	return false;
}

static void GLStubReport(const char* name)
{
	static std::mutex s_mutex;
	static std::unordered_set<std::string> s_seen;
	std::lock_guard<std::mutex> l(s_mutex);
	if (s_seen.insert(name).second)
		cemuLog_log(LogType::Force, "[GL] AVISO: a funcao '{}' nao existe no OpenGL ES; chamada ignorada (retorna 0). Ela precisa de um caminho GLES proprio.", name);
}

#define GLFUNC(__type, __name)	static uintptr_t GLStub_##__name() { GLStubReport(STRINGIFY(__name)); return 0; }
#define EGLFUNC(__type, __name)
#include "Common/GLInclude/glFunctions.h"
#undef GLFUNC
#undef EGLFUNC


// ---------------------------------------------------------------------------------------------
// IMPLEMENTACOES GLES das funcoes de OpenGL desktop (DSA, 1D, glGetTexImage, ...).
// Antes elas viravam stubs que nao faziam nada (ou ponteiro nulo -> crash). Aqui cada uma e emulada com
// as chamadas equivalentes do OpenGL ES 3.2 (bind -> chamada normal -> restaura o bind anterior).
// Carregadas sob demanda por _GetOpenGLFunction (EGL/GLESv3).
// ---------------------------------------------------------------------------------------------
namespace ESShim
{
	struct Fn
	{
		void (*BindBuffer)(GLenum, GLuint);
		void (*BufferData)(GLenum, GLsizeiptr, const void*, GLenum);
		void (*BufferSubData)(GLenum, GLintptr, GLsizeiptr, const void*);
		void* (*MapBufferRange)(GLenum, GLintptr, GLsizeiptr, GLbitfield);
		void (*GetBufferParameteriv)(GLenum, GLenum, GLint*);
		void (*GenBuffers)(GLsizei, GLuint*);
		void (*DrawBuffers)(GLsizei, const GLenum*);
		void (*Viewport)(GLint, GLint, GLsizei, GLsizei);
		void (*ClearDepthf)(GLfloat);
		void (*DepthRangef)(GLfloat, GLfloat);
		void (*BindFramebuffer)(GLenum, GLuint);
		void (*FramebufferTexture)(GLenum, GLenum, GLuint, GLint);
		void (*FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
		void (*FramebufferTextureLayer)(GLenum, GLenum, GLuint, GLint, GLint);
		void (*InvalidateFramebuffer)(GLenum, GLsizei, const GLenum*);
		void (*GenFramebuffers)(GLsizei, GLuint*);
		void (*DeleteFramebuffers)(GLsizei, const GLuint*);
		void (*GetIntegerv)(GLenum, GLint*);
		void (*GenVertexArrays)(GLsizei, GLuint*);
		void (*BindVertexArray)(GLuint);
		void (*EnableVertexAttribArray)(GLuint);
		void (*DisableVertexAttribArray)(GLuint);
		void (*BindVertexBuffer)(GLuint, GLuint, GLintptr, GLsizei);
		void (*VertexAttribBinding)(GLuint, GLuint);
		void (*VertexAttribIFormat)(GLuint, GLint, GLenum, GLuint);
		void (*VertexBindingDivisor)(GLuint, GLuint);
		void (*BeginQuery)(GLenum, GLuint);
		void (*EndQuery)(GLenum);
		void (*GetQueryiv)(GLenum, GLenum, GLint*);
		void (*GetQueryObjectuiv)(GLuint, GLenum, GLuint*);
		void (*GetTexLevelParameteriv)(GLenum, GLint, GLenum, GLint*);
		void (*BindTexture)(GLenum, GLuint);
		void (*DrawElementsInstancedBaseVertex)(GLenum, GLsizei, GLenum, const void*, GLsizei, GLint);
		void (*ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
	};
	static Fn g;
	static bool g_loaded = false;

	static void Load()
	{
		if (g_loaded)
			return;
		g_loaded = true;
#define L(member, name) g.member = (decltype(g.member))_GetOpenGLFunction(g_libGLESv3, name)
		L(BindBuffer, "glBindBuffer"); L(BufferData, "glBufferData"); L(BufferSubData, "glBufferSubData");
		L(MapBufferRange, "glMapBufferRange"); L(GetBufferParameteriv, "glGetBufferParameteriv"); L(GenBuffers, "glGenBuffers");
		L(DrawBuffers, "glDrawBuffers"); L(Viewport, "glViewport"); L(ClearDepthf, "glClearDepthf"); L(DepthRangef, "glDepthRangef");
		L(BindFramebuffer, "glBindFramebuffer"); L(FramebufferTexture, "glFramebufferTexture");
		L(FramebufferTexture2D, "glFramebufferTexture2D"); L(FramebufferTextureLayer, "glFramebufferTextureLayer");
		L(InvalidateFramebuffer, "glInvalidateFramebuffer"); L(GenFramebuffers, "glGenFramebuffers"); L(DeleteFramebuffers, "glDeleteFramebuffers");
		L(GetIntegerv, "glGetIntegerv"); L(GenVertexArrays, "glGenVertexArrays"); L(BindVertexArray, "glBindVertexArray");
		L(EnableVertexAttribArray, "glEnableVertexAttribArray"); L(DisableVertexAttribArray, "glDisableVertexAttribArray");
		L(BindVertexBuffer, "glBindVertexBuffer"); L(VertexAttribBinding, "glVertexAttribBinding");
		L(VertexAttribIFormat, "glVertexAttribIFormat"); L(VertexBindingDivisor, "glVertexBindingDivisor");
		L(BeginQuery, "glBeginQuery"); L(EndQuery, "glEndQuery"); L(GetQueryiv, "glGetQueryiv"); L(GetQueryObjectuiv, "glGetQueryObjectuiv");
		L(GetTexLevelParameteriv, "glGetTexLevelParameteriv"); L(BindTexture, "glBindTexture");
		L(DrawElementsInstancedBaseVertex, "glDrawElementsInstancedBaseVertex"); L(ReadPixels, "glReadPixels");
#undef L
	}

	static void Warn(const char* what)
	{
		static std::mutex m;
		static std::unordered_set<std::string> seen;
		std::lock_guard<std::mutex> l(m);
		if (seen.insert(what).second)
			cemuLog_log(LogType::Force, "[GL] AVISO: {}", what);
	}

	constexpr GLenum kCopyWrite = 0x8F37, kFramebuffer = 0x8D40, kDrawFbo = 0x8CA9, kFboBinding = 0x8CA6, kVaoBinding = 0x85B5, kTex2D = 0x0DE1;
	constexpr GLenum kBufferSize = 0x8764, kTexBinding2D = 0x8069;

	// ---- buffers (DSA) ----
	static void CreateBuffers(GLsizei n, GLuint* buffers)
	{
		Load();
		g.GenBuffers(n, buffers);
		// o objeto so passa a existir no primeiro bind
		GLint prev = 0;
		g.GetIntegerv(kCopyWrite, &prev);
		for (GLsizei i = 0; i < n; i++)
			g.BindBuffer(kCopyWrite, buffers[i]);
		g.BindBuffer(kCopyWrite, (GLuint)prev);
	}
	static void NamedBufferStorage(GLuint buffer, GLsizeiptr size, const void* data, GLbitfield flags)
	{
		Load();
		if (flags & 0x0040 /*GL_MAP_PERSISTENT_BIT*/)
			Warn("glNamedBufferStorage com GL_MAP_PERSISTENT_BIT nao existe no GLES; criando buffer normal (mapeamento persistente indisponivel)");
		GLint prev = 0;
		g.GetIntegerv(kCopyWrite, &prev);
		g.BindBuffer(kCopyWrite, buffer);
		g.BufferData(kCopyWrite, size, data, 0x88E8 /*DYNAMIC_DRAW*/);
		g.BindBuffer(kCopyWrite, (GLuint)prev);
	}
	static void NamedBufferSubData(GLuint buffer, GLintptr offset, GLsizeiptr size, const void* data)
	{
		Load();
		GLint prev = 0;
		g.GetIntegerv(kCopyWrite, &prev);
		g.BindBuffer(kCopyWrite, buffer);
		g.BufferSubData(kCopyWrite, offset, size, data);
		g.BindBuffer(kCopyWrite, (GLuint)prev);
	}
	static GLbitfield AccessToMapBits(GLenum access)
	{
		switch (access)
		{
		case 0x88B8: return 0x0001; // READ_ONLY -> MAP_READ_BIT
		case 0x88B9: return 0x0002; // WRITE_ONLY -> MAP_WRITE_BIT
		default: return 0x0003;
		}
	}
	static void* MapBuffer(GLenum target, GLenum access)
	{
		Load();
		GLint size = 0;
		g.GetBufferParameteriv(target, kBufferSize, &size);
		return g.MapBufferRange(target, 0, size, AccessToMapBits(access));
	}
	static void* MapNamedBuffer(GLuint buffer, GLenum access)
	{
		Load();
		GLint prev = 0;
		g.GetIntegerv(kCopyWrite, &prev);
		g.BindBuffer(kCopyWrite, buffer);
		void* p = MapBuffer(kCopyWrite, access);
		g.BindBuffer(kCopyWrite, (GLuint)prev);
		return p;
	}
	static void* MapNamedBufferRange(GLuint buffer, GLintptr offset, GLsizeiptr length, GLbitfield access)
	{
		Load();
		GLint prev = 0;
		g.GetIntegerv(kCopyWrite, &prev);
		g.BindBuffer(kCopyWrite, buffer);
		access &= ~(GLbitfield)0x0040; // sem PERSISTENT/COHERENT no ES
		access &= ~(GLbitfield)0x0080;
		void* p = g.MapBufferRange(kCopyWrite, offset, length, access);
		g.BindBuffer(kCopyWrite, (GLuint)prev);
		return p;
	}
	static void GetBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, void* data)
	{
		Load();
		void* p = g.MapBufferRange(target, offset, size, 0x0001);
		if (p)
		{
			memcpy(data, p, (size_t)size);
			using UnmapFn = GLboolean(*)(GLenum);
			static UnmapFn unmap = (UnmapFn)_GetOpenGLFunction(g_libGLESv3, "glUnmapBuffer");
			if (unmap)
				unmap(target);
		}
	}
	static void GetNamedBufferSubData(GLuint buffer, GLintptr offset, GLsizeiptr size, void* data)
	{
		Load();
		GLint prev = 0;
		g.GetIntegerv(kCopyWrite, &prev);
		g.BindBuffer(kCopyWrite, buffer);
		GetBufferSubData(kCopyWrite, offset, size, data);
		g.BindBuffer(kCopyWrite, (GLuint)prev);
	}

	// ---- estado simples ----
	static void ViewportIndexedf(GLuint index, GLfloat x, GLfloat y, GLfloat w, GLfloat h)
	{
		Load();
		if (index == 0)
			g.Viewport((GLint)x, (GLint)y, (GLsizei)w, (GLsizei)h);
	}
	static void DrawBuffer(GLenum mode)
	{
		Load();
		g.DrawBuffers(1, &mode);
	}
	static void ClearDepth(GLdouble d) { Load(); g.ClearDepthf((GLfloat)d); }
	static void DepthRange(GLdouble n, GLdouble f) { Load(); g.DepthRangef((GLfloat)n, (GLfloat)f); }
	static void Noop2(GLenum, GLenum) {}
	static void NoopU(GLuint) {}
	static void NoopBindFragDataLocation(GLuint, GLuint, const GLchar*) {}
	static void NoopTexImage1D(GLenum, GLint, GLint, GLsizei, GLint, GLenum, GLenum, const void*) { Warn("texturas 1D nao existem no GLES (ignorado)"); }
	static void NoopTexSubImage1D(GLenum, GLint, GLint, GLsizei, GLenum, GLenum, const void*) {}
	static void NoopTexStorage1D(GLenum, GLsizei, GLenum, GLsizei) { Warn("texturas 1D nao existem no GLES (ignorado)"); }
	static void NoopCompressedTexImage1D(GLenum, GLint, GLenum, GLsizei, GLint, GLsizei, const void*) {}
	static void NoopCompressedTexSubImage1D(GLenum, GLint, GLint, GLsizei, GLenum, GLsizei, const void*) {}
	static void NoopInvalidateTexImage(GLuint, GLint) {}
	static void NoopSSBBinding(GLuint, GLuint, GLuint) {}
	static void NoopQueryCounter(GLuint, GLenum) {}

	// ---- framebuffers (DSA) ----
	struct FboScope
	{
		GLint prev = 0;
		explicit FboScope(GLuint fbo) { g.GetIntegerv(kFboBinding, &prev); g.BindFramebuffer(kDrawFbo, fbo); }
		~FboScope() { g.BindFramebuffer(kDrawFbo, (GLuint)prev); }
	};
	static void NamedFramebufferTexture(GLuint fbo, GLenum attachment, GLuint texture, GLint level)
	{
		Load();
		FboScope s(fbo);
		if (texture == 0)
			g.FramebufferTexture2D(kDrawFbo, attachment, kTex2D, 0, 0);
		else if (g.FramebufferTexture)
			g.FramebufferTexture(kDrawFbo, attachment, texture, level);
		else
			g.FramebufferTexture2D(kDrawFbo, attachment, kTex2D, texture, level);
	}
	static void NamedFramebufferTexture2DEXT(GLuint fbo, GLenum attachment, GLenum textarget, GLuint texture, GLint level)
	{
		Load();
		FboScope s(fbo);
		g.FramebufferTexture2D(kDrawFbo, attachment, textarget, texture, level);
	}
	static void NamedFramebufferTextureLayer(GLuint fbo, GLenum attachment, GLuint texture, GLint level, GLint layer)
	{
		Load();
		FboScope s(fbo);
		g.FramebufferTextureLayer(kDrawFbo, attachment, texture, level, layer);
	}
	static void InvalidateNamedFramebufferData(GLuint fbo, GLsizei n, const GLenum* attachments)
	{
		Load();
		FboScope s(fbo);
		g.InvalidateFramebuffer(kDrawFbo, n, attachments);
	}

	// ---- queries ----
	// GL_SAMPLES_PASSED (0x8914) NAO existe no GLES: glBeginQuery() com esse alvo gera GL_INVALID_ENUM, a query nunca comeca e o
	// glGetQueryObject*() seguinte fica preso dentro do driver (logs do Zelda BOTW/Mario Kart 8 em OpenGL: a thread da GPU congelava em
	// LatteQueryObjectGL::getResult). O GLES 3.0+ tem GL_ANY_SAMPLES_PASSED(_CONSERVATIVE), que da 0/1 - o suficiente para a oclusao do Wii U.
	static GLenum MapQueryTarget(GLenum target)
	{
		constexpr GLenum kSamplesPassed = 0x8914;                  // GL_SAMPLES_PASSED (desktop)
		constexpr GLenum kAnySamplesPassedConservative = 0x8D6A;   // GL_ANY_SAMPLES_PASSED_CONSERVATIVE (GLES 3.0)
		return target == kSamplesPassed ? kAnySamplesPassedConservative : target;
	}
	static void BeginQueryIndexed(GLenum target, GLuint index, GLuint id)
	{
		Load();
		if (index == 0)
			g.BeginQuery(MapQueryTarget(target), id);
	}
	static void EndQueryIndexed(GLenum target, GLuint index)
	{
		Load();
		if (index == 0)
			g.EndQuery(MapQueryTarget(target));
	}
	static void GetQueryIndexediv(GLenum target, GLuint index, GLenum pname, GLint* params)
	{
		Load();
		if (index == 0)
			g.GetQueryiv(MapQueryTarget(target), pname, params);
		else if (params)
			*params = 0;
	}
	static void GetQueryObjecti64v(GLuint id, GLenum pname, GLint64* params)
	{
		Load();
		GLuint v = 0;
		g.GetQueryObjectuiv(id, pname, &v);
		*params = (GLint64)v;
	}
	static void GetQueryObjectiv(GLuint id, GLenum pname, GLint* params)
	{
		Load();
		GLuint v = 0;
		g.GetQueryObjectuiv(id, pname, &v);
		*params = (GLint)v;
	}

	// ---- texturas ----
	static void GetTextureLevelParameteriv(GLuint texture, GLint level, GLenum pname, GLint* params)
	{
		Load();
		GLint prev = 0;
		g.GetIntegerv(kTexBinding2D, &prev);
		g.BindTexture(kTex2D, texture);
		g.GetTexLevelParameteriv(kTex2D, level, pname, params);
		g.BindTexture(kTex2D, (GLuint)prev);
	}
	// leitura da textura 2D atualmente ligada via FBO + glReadPixels
	static void GetTexImage(GLenum target, GLint level, GLenum format, GLenum type, void* pixels)
	{
		Load();
		if (target != kTex2D || !pixels)
			return;
		GLint tex = 0, w = 0, h = 0;
		g.GetIntegerv(kTexBinding2D, &tex);
		g.GetTexLevelParameteriv(kTex2D, level, 0x1000 /*WIDTH*/, &w);
		g.GetTexLevelParameteriv(kTex2D, level, 0x1001 /*HEIGHT*/, &h);
		if (w <= 0 || h <= 0)
			return;
		GLint prevFbo = 0;
		g.GetIntegerv(kFboBinding, &prevFbo);
		GLuint fbo = 0;
		g.GenFramebuffers(1, &fbo);
		g.BindFramebuffer(0x8CA8 /*READ_FRAMEBUFFER*/, fbo);
		g.FramebufferTexture2D(0x8CA8, 0x8CE0, kTex2D, (GLuint)tex, level);
		if (format == 0x1907 /*GL_RGB*/ && type == 0x1401)
		{
			std::vector<uint8> rgba((size_t)w * (size_t)h * 4);
			g.ReadPixels(0, 0, w, h, 0x1908, 0x1401, rgba.data());
			uint8* dst = (uint8*)pixels;
			for (size_t i = 0; i < (size_t)w * (size_t)h; i++)
			{
				dst[i * 3 + 0] = rgba[i * 4 + 0];
				dst[i * 3 + 1] = rgba[i * 4 + 1];
				dst[i * 3 + 2] = rgba[i * 4 + 2];
			}
		}
		else
			g.ReadPixels(0, 0, w, h, format, type, pixels);
		g.FramebufferTexture2D(0x8CA8, 0x8CE0, kTex2D, 0, 0);
		g.BindFramebuffer(0x8CA8, (GLuint)prevFbo);
		g.DeleteFramebuffers(1, &fbo);
	}

	// ---- desenho ----
	static void DrawElementsInstancedBaseVertexBaseInstance(GLenum mode, GLsizei count, GLenum type, const void* indices, GLsizei instanceCount, GLint baseVertex, GLuint baseInstance)
	{
		Load();
		// baseInstance e emulado em _setupVertexAttributes (deslocamento dos ponteiros dos atributos por instancia)
		g.DrawElementsInstancedBaseVertex(mode, count, type, indices, instanceCount, baseVertex);
	}

	// ---- vertex arrays (DSA) ----
	struct VaoScope
	{
		GLint prev = 0;
		explicit VaoScope(GLuint vao) { g.GetIntegerv(kVaoBinding, &prev); g.BindVertexArray(vao); }
		~VaoScope() { g.BindVertexArray((GLuint)prev); }
	};
	static void CreateVertexArrays(GLsizei n, GLuint* arrays)
	{
		Load();
		g.GenVertexArrays(n, arrays);
		GLint prev = 0;
		g.GetIntegerv(kVaoBinding, &prev);
		for (GLsizei i = 0; i < n; i++)
			g.BindVertexArray(arrays[i]);
		g.BindVertexArray((GLuint)prev);
	}
	static void DisableVertexArrayAttrib(GLuint vao, GLuint index) { Load(); VaoScope s(vao); g.DisableVertexAttribArray(index); }
	static void EnableVertexArrayAttrib(GLuint vao, GLuint index) { Load(); VaoScope s(vao); g.EnableVertexAttribArray(index); }
	static void VertexArrayElementBuffer(GLuint vao, GLuint buffer) { Load(); VaoScope s(vao); g.BindBuffer(0x8893 /*ELEMENT_ARRAY_BUFFER*/, buffer); }
	static void VertexArrayVertexBuffer(GLuint vao, GLuint bindingIndex, GLuint buffer, GLintptr offset, GLsizei stride) { Load(); VaoScope s(vao); g.BindVertexBuffer(bindingIndex, buffer, offset, stride); }
	static void VertexArrayAttribBinding(GLuint vao, GLuint attribIndex, GLuint bindingIndex) { Load(); VaoScope s(vao); g.VertexAttribBinding(attribIndex, bindingIndex); }
	static void VertexArrayAttribIFormat(GLuint vao, GLuint attribIndex, GLint size, GLenum type, GLuint relOffset) { Load(); VaoScope s(vao); g.VertexAttribIFormat(attribIndex, size, type, relOffset); }
	static void VertexArrayBindingDivisor(GLuint vao, GLuint bindingIndex, GLuint divisor) { Load(); VaoScope s(vao); g.VertexBindingDivisor(bindingIndex, divisor); }
}

// instala as implementacoes acima nas funcoes que estao nulas. Retorna quantas foram instaladas.
#define ESSHIM_SET(name, fn) do { if (!name) { name = (decltype(name))(void*)&ESShim::fn; n++; installed += #name " "; } } while (0)
static int InstallESShims(std::string& installed)
{
	int n = 0;
	// usadas SEM teste de nulo no Cemu (a falta de qualquer uma delas derrubava o app com pc=0)
	ESSHIM_SET(glCreateBuffers, CreateBuffers);
	ESSHIM_SET(glNamedBufferStorage, NamedBufferStorage);
	ESSHIM_SET(glNamedBufferSubData, NamedBufferSubData);
	ESSHIM_SET(glViewportIndexedf, ViewportIndexedf);
	ESSHIM_SET(glDrawBuffer, DrawBuffer);
	ESSHIM_SET(glClearDepth, ClearDepth);
	ESSHIM_SET(glDepthRange, DepthRange);
	ESSHIM_SET(glPolygonMode, Noop2);
	ESSHIM_SET(glPrimitiveRestartIndex, NoopU);
	ESSHIM_SET(glClampColor, Noop2);
	ESSHIM_SET(glBindFragDataLocation, NoopBindFragDataLocation);
	ESSHIM_SET(glTexImage1D, NoopTexImage1D);
	ESSHIM_SET(glTexSubImage1D, NoopTexSubImage1D);
	ESSHIM_SET(glTexStorage1D, NoopTexStorage1D);
	ESSHIM_SET(glCompressedTexImage1D, NoopCompressedTexImage1D);
	ESSHIM_SET(glCompressedTexSubImage1D, NoopCompressedTexSubImage1D);
	ESSHIM_SET(glInvalidateTexImage, NoopInvalidateTexImage);
	ESSHIM_SET(glShaderStorageBlockBinding, NoopSSBBinding);
	ESSHIM_SET(glQueryCounter, NoopQueryCounter);
	ESSHIM_SET(glGetTexImage, GetTexImage);
	ESSHIM_SET(glMapBuffer, MapBuffer);
	ESSHIM_SET(glMapNamedBuffer, MapNamedBuffer);
	ESSHIM_SET(glMapNamedBufferRange, MapNamedBufferRange);
	ESSHIM_SET(glGetBufferSubData, GetBufferSubData);
	ESSHIM_SET(glGetNamedBufferSubData, GetNamedBufferSubData);
	ESSHIM_SET(glNamedFramebufferTexture, NamedFramebufferTexture);
	ESSHIM_SET(glNamedFramebufferTexture2DEXT, NamedFramebufferTexture2DEXT);
	ESSHIM_SET(glNamedFramebufferTextureLayer, NamedFramebufferTextureLayer);
	ESSHIM_SET(glInvalidateNamedFramebufferData, InvalidateNamedFramebufferData);
	ESSHIM_SET(glBeginQueryIndexed, BeginQueryIndexed);
	ESSHIM_SET(glEndQueryIndexed, EndQueryIndexed);
	ESSHIM_SET(glGetQueryIndexediv, GetQueryIndexediv);
	ESSHIM_SET(glGetQueryObjecti64v, GetQueryObjecti64v);
	ESSHIM_SET(glGetQueryObjectiv, GetQueryObjectiv);
	ESSHIM_SET(glGetTextureLevelParameteriv, GetTextureLevelParameteriv);
	ESSHIM_SET(glDrawElementsInstancedBaseVertexBaseInstance, DrawElementsInstancedBaseVertexBaseInstance);
	ESSHIM_SET(glCreateVertexArrays, CreateVertexArrays);
	ESSHIM_SET(glDisableVertexArrayAttrib, DisableVertexArrayAttrib);
	ESSHIM_SET(glEnableVertexArrayAttrib, EnableVertexArrayAttrib);
	ESSHIM_SET(glVertexArrayElementBuffer, VertexArrayElementBuffer);
	ESSHIM_SET(glVertexArrayVertexBuffer, VertexArrayVertexBuffer);
	ESSHIM_SET(glVertexArrayAttribBinding, VertexArrayAttribBinding);
	ESSHIM_SET(glVertexArrayAttribIFormat, VertexArrayAttribIFormat);
	ESSHIM_SET(glVertexArrayBindingDivisor, VertexArrayBindingDivisor);
	return n;
}
#undef ESSHIM_SET

static void InstallGLStubsForMissingFunctions()
{
	{
		std::string installedShims;
		const int numShims = InstallESShims(installedShims);
		cemuLog_log(LogType::Force, "[GL] {} funcoes desktop emuladas com chamadas do GLES: {}", numShims, installedShims);
	}
	std::string stubbed, detected;
	int numStubbed = 0, numDetected = 0;
#define GLFUNC(__type, __name)	if (!__name) { if (IsFeatureDetectedFunction(STRINGIFY(__name))) { detected += STRINGIFY(__name) " "; numDetected++; } else { __name = (__type)(void*)&GLStub_##__name; stubbed += STRINGIFY(__name) " "; numStubbed++; } }
#define EGLFUNC(__type, __name)
#include "Common/GLInclude/glFunctions.h"
#undef GLFUNC
#undef EGLFUNC
	cemuLog_log(LogType::Force, "[GL] {} funcoes de GL desktop ausentes no GLES foram trocadas por stub com log: {}", numStubbed, stubbed);
	cemuLog_log(LogType::Force, "[GL] {} funcoes ausentes usadas apenas para detectar recurso (mantidas nulas): {}", numDetected, detected);
}

void LoadOpenGLImports()
{
	g_libGLESv3 = dlopen("libGLESv3.so", RTLD_NOW | RTLD_GLOBAL);
	if (!g_libGLESv3)
		cemuLog_log(LogType::Force, "Failed to dlopen libGLESv3.so");
	g_libEGL = dlopen("libEGL.so", RTLD_NOW | RTLD_GLOBAL);
	if (!g_libEGL)
		cemuLog_log(LogType::Force, "Failed to dlopen libEGL.so");

#define GLFUNC(__type, __name)	__name = (__type)_GetOpenGLFunction(g_libGLESv3, STRINGIFY(__name));
#define EGLFUNC(__type, __name)	__name = (__type)dlsym(g_libEGL, STRINGIFY(__name));
#include "Common/GLInclude/glFunctions.h"
#undef GLFUNC
#undef EGLFUNC
	InstallGLStubsForMissingFunctions();
}

#elif BOOST_OS_MACOS
void LoadOpenGLImports()
{
	cemu_assert_unimplemented();
}
#endif

void OpenGLRenderer::Initialize()
{
	Renderer::Initialize();
	auto lock = cemuLog_acquire();
	cemuLog_log(LogType::Force, "------- Init OpenGL graphics backend -------");

	GLCanvas_MakeCurrent(false);
	LoadOpenGLImports();
	GetVendorInformation();	
	QuerySupportedFormats();

#if defined(__ANDROID__)
	if (glDebugMessageCallback)
	{
		glEnable(GL_DEBUG_OUTPUT);
		if (Diag::IsEnabled(Diag::Flag::GLDebug))
		{
			glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS); // a mensagem chega na chamada GL que a causou
			glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, NULL, GL_TRUE); // todas as fontes/tipos/severidades
		}
		glDebugMessageCallback(_glDebugCallbackAndroid, NULL);
		cemuLog_log(LogType::Force, "[GL] KHR_debug ativo (mensagens do driver em [GLDEBUG])");
	}
#endif
#if defined(__ANDROID__)
	// GLES: tenta GL_EXT_clip_control (mesma semantica do ARB); sem ela, emula no vertex shader
	if (!glClipControl && IsGLES())
	{
		if (IsGLExtensionSupported("GL_EXT_clip_control"))
		{
			void* fn = (void*)_GetOpenGLFunction(g_libGLESv3, "glClipControlEXT");
			if (fn)
			{
				glClipControl = (decltype(glClipControl))fn;
				cemuLog_log(LogType::Force, "[GL] usando GL_EXT_clip_control");
			}
		}
		if (!glClipControl)
		{
			s_clipControlEmulated = true;
			cemuLog_log(LogType::Force, "[GL] GL_EXT_clip_control ausente: emulando clip control (flip Y + Z 0..1) nos shaders");
		}
	}
#endif

#if BOOST_OS_WINDOWS
	if (wglSwapIntervalEXT)
		wglSwapIntervalEXT(0); // disable V-Sync per default
#endif

	if (glMaxShaderCompilerThreadsARB)
		glMaxShaderCompilerThreadsARB(0xFFFFFFFF);

	cemuLog_log(LogType::Force, "OpenGL extensions:");
	cemuLog_log(LogType::Force, "ARB_clip_control: {}", glClipControl ? "available" : "not supported");
	cemuLog_log(LogType::Force, "ARB_get_program_binary: {}", (glGetProgramBinary != NULL && glProgramBinary != NULL) ? "available" : "not supported");
	cemuLog_log(LogType::Force, "ARB_clear_texture: {}", (glClearTexImage != NULL) ? "available" : "not supported");
	cemuLog_log(LogType::Force, "ARB_copy_image: {}", (glCopyImageSubData != NULL) ? "available" : "not supported");
	cemuLog_log(LogType::Force, "NV_depth_buffer_float: {}", (glDepthRangedNV != NULL) ? "available" : "not supported");

#if !defined(__ANDROID__)
	// enable framebuffer SRGB support
	CapEnableGLES(GL_FRAMEBUFFER_SRGB);

	if (this->m_vendor != GfxVendor::AMD)
	{
		// don't enable this for AMD because GL_PROGRAM_POINT_SIZE breaks shader binaries for some reason
		// it also seems like AMD ignores GL_POINT_SPRITE and gl_PointCoord is always 0.0/0.0

		// point size is always defined via shader
		CapEnableGLES(GL_PROGRAM_POINT_SIZE);

		// since we are using compatibility profile point sprites are disabled by default (e.g. gl_PointCoord wont work)
		CapEnableGLES(GL_POINT_SPRITE);
	}
#else
	// GLES: GL_FRAMEBUFFER_SRGB / GL_PROGRAM_POINT_SIZE / GL_POINT_SPRITE nao existem (dariam GL_INVALID_ENUM);
	// o tamanho do ponto sempre vem do shader e o sRGB do framebuffer e decidido pela surface.
#endif

	// check if clip control is available
	if (glClipControl)
	{
		glClipControl(GL_UPPER_LEFT, GL_NEGATIVE_ONE_TO_ONE);
	}
	else if (s_clipControlEmulated)
	{
		ApplyFrontFace();
	}
	else
	{
		cemuLog_log(LogType::Force, "ARB_CLIP_CONTROL not supported by graphics driver. This will lead to crashes or graphical artifacts.");
	}

	// set pixel unpack alignment to 1 byte
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	// on NVIDIA rendering to SNORM textures is clamped to [0.0,1.0] range for non-core profiles
	// we can still disable the clamping using an older function (note that AMD and Intel don't need this, which is technically incorrect according to the compatibility profile spec)
	if (m_vendor == GfxVendor::Nvidia)
		glClampColor(GL_CLAMP_FRAGMENT_COLOR, GL_FALSE);


#if !defined(__ANDROID__)
	glEnable(GL_PRIMITIVE_RESTART);
	glPrimitiveRestartIndex(0xFFFFFFFF);
#else
	// GLES nao tem glPrimitiveRestartIndex (ponteiro NULL -> crash). Usa o "primitive restart" de indice fixo
	// (GL_PRIMITIVE_RESTART_FIXED_INDEX = 0x8D69): o indice de reinicio e o maior valor do tipo (0xFFFF/0xFFFFFFFF).
	glEnable(0x8D69);
#endif

	glGenProgramPipelines(1, &m_pipeline);
	glBindProgramPipeline(m_pipeline);

	lock.unlock();

	// create framebuffer for fast clearing (avoid glClearTexSubImage on Nvidia)
	if (glCreateFramebuffers)
		glCreateFramebuffers(1, &glRendererState.clearFBO);
	else
	{
		glGenFramebuffers(1, &glRendererState.clearFBO);
		// bind to initialize
		glBindFramebuffer(GL_FRAMEBUFFER_EXT, glRendererState.clearFBO);
		glBindFramebuffer(GL_FRAMEBUFFER_EXT, 0);
	}

	// create uniform buffers for backbufferblit
	glCreateBuffers(1, &m_backbufferBlit_uniformBuffer);
	glNamedBufferStorage(m_backbufferBlit_uniformBuffer, sizeof(RendererOutputShader::OutputUniformVariables), nullptr, GL_DYNAMIC_STORAGE_BIT);

	draw_init();

	catchOpenGLError();

	glGenBuffers(1, &glStreamoutCacheRingBuffer);
	glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, glStreamoutCacheRingBuffer);
	glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER, LatteStreamout_GetRingBufferSize(), NULL, GL_DYNAMIC_DRAW);
	glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, 0);

	catchOpenGLError();

	// imgui
#if defined(__ANDROID__)
	ImGui_ImplOpenGL3_Init("#version 300 es");
#else
	ImGui_ImplOpenGL3_Init("#version 130");
#endif
}

bool OpenGLRenderer::IsPadWindowActive()
{
	return GLCanvas_HasPadViewOpen();
}

void OpenGLRenderer::Flush(bool waitIdle)
{
	glFlush();
	if (waitIdle)
		glFinish();
}

void OpenGLRenderer::NotifyLatteCommandProcessorIdle()
{
	glFlush();
}

void OpenGLRenderer::GetVendorInformation()
{
	// example vendor strings:
	// ATI Technologies Inc.
	// NVIDIA Corporation 
	// Intel
	char* glVendorString = (char*)glGetString(GL_VENDOR);
	char* glRendererString = (char*)glGetString(GL_RENDERER);
	char* glVersionString = (char*)glGetString(GL_VERSION);

	cemuLog_log(LogType::Force, "GL_VENDOR: {}", glVendorString ? glVendorString : "unknown");
	cemuLog_log(LogType::Force, "GL_RENDERER: {}", glRendererString ? glRendererString : "unknown");
	cemuLog_log(LogType::Force, "GL_VERSION: {}", glVersionString ? glVersionString : "unknown");
	RendererInfo::Set(IsGLES() ? "OpenGL ES" : "OpenGL", std::string(glRendererString ? glRendererString : "desconhecido"));

	if(glVersionString && boost::icontains(glVersionString, "Mesa"))
	{
		m_vendor = GfxVendor::Mesa;
		return;
	}

	if (glVendorString)
	{
		if ((toupper(glVendorString[0]) == 'A' && toupper(glVendorString[1]) == 'T' && toupper(glVendorString[2]) == 'I') ||
			(toupper(glVendorString[0]) == 'A' && toupper(glVendorString[1]) == 'M' && toupper(glVendorString[2]) == 'D'))
		{
			m_vendor = GfxVendor::AMD;
			return;
		}
		else if (memcmp(glVendorString, "NVIDIA", 6) == 0)
		{
			m_vendor = GfxVendor::Nvidia;
			return;
		}
		else if (memcmp(glVendorString, "Intel", 5) == 0)
		{
			m_vendor = GfxVendor::Intel;
			return;
		}
	}

	m_vendor = GfxVendor::Generic;
}

bool OpenGLRenderer::IsGLExtensionSupported(std::string_view extensionName)
{
	// Modern (GL 3.0+ / GLES 3.0+) indexed extension query. This project already assumes
	// at least that baseline elsewhere, and the legacy single-string glGetString(GL_EXTENSIONS)
	// is disallowed (returns null) on core-profile desktop contexts anyway.
	GLint extensionCount = 0;
	glGetIntegerv(GL_NUM_EXTENSIONS, &extensionCount);
	for (GLint i = 0; i < extensionCount; i++)
	{
		const char* ext = (const char*)glGetStringi(GL_EXTENSIONS, (GLuint)i);
		if (ext && extensionName == ext)
			return true;
	}
	return false;
}

// Real runtime checks for texture compression format support, used by the per-format
// "texture_compression_bcX" experimental options (BC1-BC5 -> ETC2/EAC transcode - see
// texture_chooseDecodedFormat below and LatteTextureGL.cpp's GetOpenGLFormatInfo).
// Before this function existed, BC1/S3TC support was simply assumed unconditionally
// here - a reasonable assumption on desktop GL, where S3TC has been a de facto standard
// for two decades, but not a safe one on Android/GLES, where this same renderer also
// runs and BC/S3TC support is inconsistent between GPU vendors.
void OpenGLRenderer::QuerySupportedFormats()
{
	const char* glVersionString = (const char*)glGetString(GL_VERSION);
	bool isGLES = glVersionString && boost::icontains(glVersionString, "OpenGL ES");
	s_isGLES = isGLES;
	s_hasTextureShadowLod = !isGLES || IsGLExtensionSupported("GL_EXT_texture_shadow_lod");
	s_hasDepthClamp = !isGLES || IsGLExtensionSupported("GL_EXT_depth_clamp");
	{
		const char* rendererName = (const char*)glGetString(GL_RENDERER);
		s_isPowerVR = isGLES && rendererName && strstr(rendererName, "PowerVR") != nullptr;
	}
	s_hasBorderClamp = !isGLES || IsGLExtensionSupported("GL_EXT_texture_border_clamp") || IsGLExtensionSupported("GL_OES_texture_border_clamp");
	int versionMajor = 0, versionMinor = 0;
	if (glVersionString)
	{
		const char* versionNumberStart = glVersionString;
		if (isGLES)
		{
			const char* esPos = strstr(glVersionString, "OpenGL ES");
			if (esPos)
				versionNumberStart = esPos + strlen("OpenGL ES ");
		}
		sscanf(versionNumberStart, "%d.%d", &versionMajor, &versionMinor);
	}

	m_supportedFormatInfo.fmt_bc1 =
		IsGLExtensionSupported("GL_EXT_texture_compression_s3tc") ||
		IsGLExtensionSupported("GL_EXT_texture_compression_dxt1") ||
		IsGLExtensionSupported("GL_ANGLE_texture_compression_dxt1");

	// Unlike fmt_bc1, BC2/DXT3 has no "DXT1-only" partial extension fallback - only the
	// full bundle extension covers it.
	m_supportedFormatInfo.fmt_bc2 = IsGLExtensionSupported("GL_EXT_texture_compression_s3tc");
	// same reasoning as fmt_bc2 - BC3/DXT5 is part of the same bundle extension.
	m_supportedFormatInfo.fmt_bc3 = IsGLExtensionSupported("GL_EXT_texture_compression_s3tc");

	// BC4/BC5 (RGTC) is a separate extension bundle from S3TC - covers both formats and
	// both UNORM/SNORM variants together.
	m_supportedFormatInfo.fmt_bc4 =
		IsGLExtensionSupported("GL_EXT_texture_compression_rgtc") ||
		IsGLExtensionSupported("GL_ARB_texture_compression_rgtc");
	m_supportedFormatInfo.fmt_bc5 = m_supportedFormatInfo.fmt_bc4;

	// V11.28: alem das extensoes, consulta a lista real de formatos comprimidos que o driver aceita
	{
		GLint numFmts = 0;
		glGetIntegerv(0x86A2 /*GL_NUM_COMPRESSED_TEXTURE_FORMATS*/, &numFmts);
		if (numFmts > 0 && numFmts < 512)
		{
			std::vector<GLint> fmts(numFmts);
			glGetIntegerv(0x86A3 /*GL_COMPRESSED_TEXTURE_FORMATS*/, fmts.data());
			bool hasDxt1 = false, hasDxt3 = false, hasDxt5 = false, hasRgtc = false;
			for (GLint f : fmts)
			{
				if (f == 0x83F0 || f == 0x83F1) hasDxt1 = true;
				if (f == 0x83F2) hasDxt3 = true;
				if (f == 0x83F3) hasDxt5 = true;
				if (f == 0x8DBB || f == 0x8DBD) hasRgtc = true;
			}
			cemuLog_log(LogType::Force, "[GL] formatos comprimidos anunciados pelo driver: {} (DXT1={} DXT3={} DXT5={} RGTC={})", numFmts, hasDxt1, hasDxt3, hasDxt5, hasRgtc);
			if (hasDxt1) m_supportedFormatInfo.fmt_bc1 = true;
			if (hasDxt3) m_supportedFormatInfo.fmt_bc2 = true;
			if (hasDxt5) m_supportedFormatInfo.fmt_bc3 = true;
			if (hasRgtc) { m_supportedFormatInfo.fmt_bc4 = true; m_supportedFormatInfo.fmt_bc5 = true; }
		}
	}

	// ETC2 is mandatory-core in GLES 3.0+ (any conformant Android GPU) and in desktop
	// OpenGL 4.3+; also available on older desktop GL via GL_ARB_ES3_compatibility.
	bool etc2CoreVersion = isGLES ? (versionMajor >= 3) : (versionMajor > 4 || (versionMajor == 4 && versionMinor >= 3));
	m_supportedFormatInfo.fmt_etc2 = etc2CoreVersion || IsGLExtensionSupported("GL_ARB_ES3_compatibility");

	{
		auto pick = [&](bool native, bool optEtc2) -> const char* { return native ? "nativo" : ((optEtc2 && !isGLES && m_supportedFormatInfo.fmt_etc2) ? "transcodificado para ETC2 (opcao experimental ligada)" : "decodificado em software para 8 bits (RGBA8/R8/RG8)"); };
		cemuLog_log(LogType::Force, "[GL] BC1 -> {} | BC2 -> {} | BC3 -> {} | BC4 -> {} | BC5 -> {}", pick(m_supportedFormatInfo.fmt_bc1, GetConfig().texture_compression_bc1), pick(m_supportedFormatInfo.fmt_bc2, GetConfig().texture_compression_bc2), pick(m_supportedFormatInfo.fmt_bc3, GetConfig().texture_compression_bc3), pick(m_supportedFormatInfo.fmt_bc4, GetConfig().texture_compression_bc4), pick(m_supportedFormatInfo.fmt_bc5, GetConfig().texture_compression_bc5));
	}
	cemuLog_log(LogType::Force, "OpenGL texture compression support: BC1/S3TC={} BC2/DXT3={} BC3/DXT5={} BC4-5/RGTC={} ETC2={}", m_supportedFormatInfo.fmt_bc1, m_supportedFormatInfo.fmt_bc2, m_supportedFormatInfo.fmt_bc3, m_supportedFormatInfo.fmt_bc4, m_supportedFormatInfo.fmt_etc2);
	// V11.30: relatorio completo de capacidades GL (true/false) para diagnostico
	{
		auto B = [](bool v) { return v ? "true" : "false"; };
		GLint iv[3] = {};
		auto gi = [&](GLenum e) { GLint v = -1; glGetIntegerv(e, &v); while (glGetError() != GL_NO_ERROR) {} return v; };
		cemuLog_log(LogType::Force, "[GLCaps] ===== relatorio OpenGL =====");
		cemuLog_log(LogType::Force, "[GLCaps] versao={} GLES={} major={} minor={} GLSL={}", glVersionString ? glVersionString : "?", B(isGLES), versionMajor, versionMinor, (const char*)glGetString(0x8B8C /*GL_SHADING_LANGUAGE_VERSION*/) ? (const char*)glGetString(0x8B8C) : "?");
		cemuLog_log(LogType::Force, "[GLCaps] limites: MAX_TEXTURE_SIZE={} MAX_3D_TEXTURE_SIZE={} MAX_ARRAY_TEXTURE_LAYERS={} MAX_TEXTURE_IMAGE_UNITS={} MAX_COMBINED_TEXTURE_IMAGE_UNITS={} MAX_VERTEX_ATTRIBS={} MAX_DRAW_BUFFERS={} MAX_COLOR_ATTACHMENTS={} MAX_UNIFORM_BLOCK_SIZE={} MAX_UNIFORM_BUFFER_BINDINGS={} UNIFORM_BUFFER_OFFSET_ALIGNMENT={} MAX_SAMPLES={}",
			gi(GL_MAX_TEXTURE_SIZE), gi(0x8073), gi(0x88FF), gi(GL_MAX_TEXTURE_IMAGE_UNITS), gi(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS), gi(GL_MAX_VERTEX_ATTRIBS), gi(GL_MAX_DRAW_BUFFERS), gi(0x8CDF), gi(0x8A30), gi(0x8A2F), gi(0x8A34), gi(0x8D57));
		cemuLog_log(LogType::Force, "[GLCaps] limites: MAX_SHADER_STORAGE_BLOCK_SIZE={} MAX_SHADER_STORAGE_BUFFER_BINDINGS={} MAX_VERTEX_UNIFORM_COMPONENTS={} MAX_FRAGMENT_UNIFORM_COMPONENTS={} MAX_VARYING_COMPONENTS={}",
			gi(0x90DE), gi(0x90DD), gi(0x8B4A), gi(0x8B49), gi(0x8B4B));
		(void)iv;
		// extensoes que o emulador pode usar: true = driver tem
		static const char* wanted[] = {
			"GL_ARB_buffer_storage","GL_EXT_buffer_storage","GL_ARB_texture_view","GL_EXT_texture_view","GL_OES_texture_view","GL_ARB_copy_image","GL_EXT_copy_image","GL_OES_copy_image",
			"GL_ARB_texture_storage","GL_ARB_direct_state_access","GL_ARB_shader_image_load_store","GL_ARB_compute_shader","GL_ARB_shader_storage_buffer_object","GL_ARB_clip_control","GL_EXT_clip_control",
			"GL_ARB_get_program_binary","GL_OES_get_program_binary","GL_ARB_gpu_shader5","GL_EXT_gpu_shader5","GL_ARB_sample_shading","GL_OES_sample_shading",
			"GL_EXT_texture_filter_anisotropic","GL_EXT_texture_compression_s3tc","GL_EXT_texture_compression_rgtc","GL_EXT_texture_compression_bptc","GL_KHR_texture_compression_astc_ldr",
			"GL_EXT_texture_border_clamp","GL_OES_texture_border_clamp","GL_EXT_texture_mirror_clamp_to_edge","GL_ARB_texture_mirror_clamp_to_edge","GL_EXT_texture_sRGB_decode",
			"GL_EXT_color_buffer_float","GL_EXT_color_buffer_half_float","GL_EXT_float_blend","GL_EXT_depth_clamp","GL_ARB_depth_clamp","GL_EXT_polygon_offset_clamp",
			"GL_EXT_blend_func_extended","GL_ARB_blend_func_extended","GL_EXT_draw_buffers_indexed","GL_OES_draw_buffers_indexed","GL_ARB_draw_buffers_blend","GL_EXT_draw_elements_base_vertex","GL_OES_draw_elements_base_vertex",
			"GL_ARB_base_instance","GL_EXT_base_instance","GL_ARB_multi_bind","GL_ARB_vertex_attrib_binding","GL_ARB_polygon_offset_clamp","GL_KHR_debug","GL_EXT_geometry_shader","GL_OES_geometry_shader",
			"GL_EXT_shader_framebuffer_fetch","GL_ARB_texture_barrier","GL_NV_texture_barrier","GL_EXT_disjoint_timer_query","GL_ARB_timer_query","GL_ARB_sync","GL_EXT_shader_io_blocks",
			"GL_EXT_primitive_bounding_box","GL_EXT_tessellation_shader","GL_OES_shader_multisample_interpolation","GL_QCOM_texture_foveated","GL_QCOM_tiled_rendering","GL_EXT_robustness","GL_KHR_robustness","GL_EXT_memory_object","GL_EXT_EGL_image_storage"};
		std::string yes, no;
		for (const char* w : wanted)
		{
			if (IsGLExtensionSupported(w)) yes += std::string(w) + " ";
			else no += std::string(w) + " ";
		}
		cemuLog_log(LogType::Force, "[GLCaps] extensoes uteis = TRUE: {}", yes);
		cemuLog_log(LogType::Force, "[GLCaps] extensoes uteis = FALSE: {}", no);
		// V11.49: resumo "o que falta e o que o emulador faz no lugar" (cada item: qualquer uma das extensoes serve)
		{
			struct Need { const char* what; std::initializer_list<const char*> exts; const char* consequence; };
			const Need needs[] = {
				{ "texture_shadow_lod", { "GL_EXT_texture_shadow_lod" }, "SAMPLE_C_L/LZ em shadow array emulado com textureGrad() (Zelda BOTW)" },
				{ "clip_control", { "GL_ARB_clip_control", "GL_EXT_clip_control" }, "Z/Y emulados no vertex shader (uf_clipFlipY)" },
				{ "buffer_storage", { "GL_ARB_buffer_storage", "GL_EXT_buffer_storage" }, "buffers persistentes indisponiveis -> uploads mais lentos" },
				{ "texture_view", { "GL_ARB_texture_view", "GL_EXT_texture_view", "GL_OES_texture_view" }, "views de textura emuladas por copia" },
				{ "copy_image", { "GL_ARB_copy_image", "GL_EXT_copy_image", "GL_OES_copy_image" }, "copias de textura via FBO/blit" },
				{ "draw_buffers_indexed", { "GL_ARB_draw_buffers_blend", "GL_EXT_draw_buffers_indexed", "GL_OES_draw_buffers_indexed" }, "blend/mascara por alvo de render nao individual" },
				{ "base_vertex", { "GL_EXT_draw_elements_base_vertex", "GL_OES_draw_elements_base_vertex" }, "indices com base vertex reescritos na CPU" },
				{ "geometry_shader", { "GL_EXT_geometry_shader", "GL_OES_geometry_shader" }, "RECT_LIST/geometry shaders do Wii U sem emulacao" },
				{ "texture_barrier", { "GL_ARB_texture_barrier", "GL_NV_texture_barrier", "GL_EXT_shader_framebuffer_fetch" }, "feedback de textura/framebuffer sem barreira" },
				{ "float_render_targets", { "GL_EXT_color_buffer_float" }, "render targets float indisponiveis" },
				{ "float_blend", { "GL_EXT_float_blend" }, "blend em alvos float indisponivel" },
				{ "depth_clamp", { "GL_ARB_depth_clamp", "GL_EXT_depth_clamp" }, "EMULADO no vertex shader (uf_depthClampEmu, limite por vertice: pequenas diferencas em sombras/ceu)" },
				{ "anisotropic", { "GL_EXT_texture_filter_anisotropic" }, "filtro anisotropico ausente" },
				{ "border_clamp", { "GL_EXT_texture_border_clamp", "GL_OES_texture_border_clamp" }, "wrap CLAMP_BORDER vira clamp_to_edge (implementado em OpenGLRendererCore.cpp)" },
				{ "mirror_clamp", { "GL_EXT_texture_mirror_clamp_to_edge", "GL_ARB_texture_mirror_clamp_to_edge" }, "wrap MIRROR_CLAMP vira mirror" },
				{ "sample_shading", { "GL_ARB_sample_shading", "GL_OES_sample_shading" }, "MSAA por amostra ausente" },
				{ "robustness", { "GL_KHR_robustness", "GL_EXT_robustness" }, "leitura fora do limite de buffer nao e zerada pelo driver" },
			};
			std::string missing;
			for (const Need& nd : needs)
			{
				bool any = false;
				for (const char* e : nd.exts)
					any = any || IsGLExtensionSupported(e);
				if (!any)
					missing += fmt::format("\n[GLCaps]   FALTA {}: {}", nd.what, nd.consequence);
			}
			cemuLog_log(LogType::Force, "[GLCaps] resumo do que FALTA no driver (e o efeito/emulacao):{}", missing.empty() ? std::string(" nada") : missing);
		}
		GLint ne = 0;
		glGetIntegerv(GL_NUM_EXTENSIONS, &ne);
		std::string all;
		for (GLint k = 0; k < ne; k++)
		{
			const char* e = (const char*)glGetStringi(GL_EXTENSIONS, (GLuint)k);
			if (e) { all += e; all += ' '; }
		}
		cemuLog_log(LogType::Force, "[GLCaps] todas as {} extensoes do driver: {}", ne, all);
		cemuLog_log(LogType::Force, "[GLCaps] decisoes: bc1={} bc2={} bc3={} bc4={} bc5={} etc2={}", B(m_supportedFormatInfo.fmt_bc1), B(m_supportedFormatInfo.fmt_bc2), B(m_supportedFormatInfo.fmt_bc3), B(m_supportedFormatInfo.fmt_bc4), B(m_supportedFormatInfo.fmt_bc5), B(m_supportedFormatInfo.fmt_etc2));
		cemuLog_log(LogType::Force, "[GLCaps] ===== fim do relatorio =====");
	}
}

void _glDebugCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar *message, const void *userParam)
{
	if (LatteGPUState.glVendor == GLVENDOR_NVIDIA && strstr(message, "Buffer"))
		return;
	if (LatteGPUState.glVendor == GLVENDOR_NVIDIA && strstr(message, "performance warning"))
		return;
	if (LatteGPUState.glVendor == GLVENDOR_NVIDIA && strstr(message, "Dithering is enabled"))
		return;
	if (LatteGPUState.glVendor == GLVENDOR_NVIDIA && strstr(message, "Blending is enabled, but is not supported for integer framebuffers"))
		return;
	if (LatteGPUState.glVendor == GLVENDOR_NVIDIA && strstr(message, "does not have a defined base level"))
		return;
	if(LatteGPUState.glVendor == GLVENDOR_NVIDIA && strstr(message, "has depth comparisons disabled, with a texture object"))
		return;

	cemuLog_log(LogType::Force, "GLDEBUG: {}", message);

	cemu_assert_debug(false);
}

#if defined(__ANDROID__)
// GLES: registra mensagens do driver (KHR_debug, nucleo no ES 3.2) com limite e sem repeticao, para diagnosticar
// o que o renderer faz de errado em cada jogo. So loga erros/comportamento indefinido/portabilidade/severidade alta.
static void _glDebugCallbackAndroid(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam)
{
	// "Log profundo do OpenGL" (Experimental > Logs e Diagnostico): sem filtro de severidade, ate 6 repeticoes por id e 3000 no total.
	// Desligado (padrao): so erros/severidade media+, 2 por id e 250 no total (como antes).
	const bool deepLog = Diag::IsEnabled(Diag::Flag::GLDebug);
	if (!deepLog && (severity == GL_DEBUG_SEVERITY_NOTIFICATION || severity == GL_DEBUG_SEVERITY_LOW))
		return;
	static std::mutex m;
	static std::unordered_map<uint32, int> perId;
	static int total = 0;
	std::lock_guard<std::mutex> l(m);
	if (++perId[id] > (deepLog ? 6 : 2) || total >= (deepLog ? 3000 : 250))
		return;
	total++;
	// Com o diagnostico de texturas ligado, um ERRO do driver (ex.: "pixel buffer format or pixel type is an invalid enum") despeja as
	// ultimas texturas carregadas: a marcada "EM ANDAMENTO"/a mais recente e a que causou o erro (a mensagem e sincrona).
	if (deepLog && type == 0x824C /*GL_DEBUG_TYPE_ERROR*/ && total <= 8)
		Diag::DumpRecentTextures("erro do driver GL");
	cemuLog_log(LogType::Force, "[GLDEBUG] tipo=0x{:x} sev=0x{:x} id={}: {}", (uint32)type, (uint32)severity, (uint32)id, message ? message : "");
}
#endif

void OpenGLRenderer::EnableDebugMode()
{
	glEnable(GL_DEBUG_OUTPUT);
	glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
	glDebugMessageCallback(_glDebugCallback, NULL);
	glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, NULL, true);
}

#if defined(__ANDROID__)
static void FrameGenGL_PresentMain()
{
	GLCanvas_SwapBuffers(true, false);
}
#endif

void OpenGLRenderer::SwapBuffers(bool swapTV, bool swapDRC)
{
#if defined(__ANDROID__)
	// Frame Generation: captura o frame real ANTES do swap...
	if (swapTV)
		FrameGenGL::CaptureRealFrame();
#endif
	GLCanvas_SwapBuffers(swapTV, swapDRC);
#if defined(__ANDROID__)
	// ...e gera/apresenta os frames extras DEPOIS dele
	if (swapTV)
	{
		// os shaders do FrameGenGL desenham em orientacao nativa (origem inferior-esquerda)
		if (glClipControl)
			glClipControl(GL_LOWER_LEFT, GL_NEGATIVE_ONE_TO_ONE);
		FrameGenGL::GenerateAfterRealSwap(&FrameGenGL_PresentMain);
		if (glClipControl)
		{
			glClipControl(GL_UPPER_LEFT, GL_NEGATIVE_ONE_TO_ONE);
			_prevInvertY = false;
			_prevHalfZ = false;
		}
	}
#endif

	if (swapTV)
		cleanupAfterFrame();
}

bool OpenGLRenderer::BeginFrame(bool mainWindow)
{
	if (!mainWindow && !IsPadWindowActive())
		return false;

	GLCanvas_MakeCurrent(!mainWindow);

	ClearColorbuffer(!mainWindow);
	return true;
}

void OpenGLRenderer::DrawEmptyFrame(bool mainWindow)
{
	if (!BeginFrame(mainWindow))
		return;
	
	SwapBuffers(mainWindow, !mainWindow);
}

void OpenGLRenderer::ClearColorbuffer(bool padView)
{
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);
}


void OpenGLRenderer::HandleScreenshotRequest(LatteTextureView* texView, bool padView)
{
	if(!m_screenshot_requested && m_screenshot_state == ScreenshotState::None)
		return;

	if (IsPadWindowActive())
	{
		// we already took a pad view screenshow and want a main window screenshot
		if (m_screenshot_state == ScreenshotState::Main && padView)
			return;

		if (m_screenshot_state == ScreenshotState::Pad && !padView)
			return;

		// remember which screenshot is left to take
		if (m_screenshot_state == ScreenshotState::None)
			m_screenshot_state = padView ? ScreenshotState::Main : ScreenshotState::Pad;
		else
			m_screenshot_state = ScreenshotState::None;
	}
	else
		m_screenshot_state = ScreenshotState::None;

	int screenshotWidth, screenshotHeight;
	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	texture_bindAndActivate(texView, 0);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &screenshotWidth);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &screenshotHeight);
	glPixelStorei(GL_PACK_ALIGNMENT, 1); // set alignment to 1

	const sint32 pixelDataSize = screenshotWidth * screenshotHeight * 3;
	std::vector<uint8> rgb_data(pixelDataSize);

	glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb_data.data());
	texture_bindAndActivate(nullptr, 0);
	
	const bool srcUsesSRGB = HAS_FLAG(texView->format, Latte::E_GX2SURFFMT::FMT_BIT_SRGB);
	const bool dstUsesSRGB = (!padView && LatteGPUState.tvBufferUsesSRGB) || (padView && LatteGPUState.drcBufferUsesSRGB);
	if((srcUsesSRGB && !dstUsesSRGB) || (!srcUsesSRGB && dstUsesSRGB))
	{
		for (sint32 iy = 0; iy < screenshotHeight; ++iy)
		{
			for (sint32 ix = 0; ix < screenshotWidth; ++ix)
			{
				uint8* pData = rgb_data.data() + (ix + iy * screenshotWidth) * 3;
				if (srcUsesSRGB && !dstUsesSRGB)
				{
					// SRGB -> RGB
					pData[0] = SRGBComponentToRGB(pData[0]);
					pData[1] = SRGBComponentToRGB(pData[1]);
					pData[2] = SRGBComponentToRGB(pData[2]);
				}
				else if (!srcUsesSRGB && dstUsesSRGB)
				{
					// RGB -> SRGB
					pData[0] = RGBComponentToSRGB(pData[0]);
					pData[1] = RGBComponentToSRGB(pData[1]);
					pData[2] = RGBComponentToSRGB(pData[2]);
				}
			}
		}
	}

	SaveScreenshot(rgb_data, screenshotWidth, screenshotHeight, !padView);
}

void OpenGLRenderer::DrawBackbufferQuad(LatteTextureView* texView, RendererOutputShader* shader, bool useLinearTexFilter, sint32 imageX, sint32 imageY, sint32 imageWidth, sint32 imageHeight, bool padView, bool clearBackground)
{
	if (padView && !IsPadWindowActive())
		return;

	catchOpenGLError();
	GLCanvas_MakeCurrent(padView);

	renderstate_resetColorControl();
	renderstate_resetDepthControl();
	attributeStream_reset();

	// bind back buffer
	rendertarget_bindFramebufferObject(nullptr);

	if (clearBackground)
	{
		int windowWidth, windowHeight;
		if (padView)
			WindowSystem::GetPadWindowPhysSize(windowWidth, windowHeight);
		else
			WindowSystem::GetWindowPhysSize(windowWidth, windowHeight);
		g_renderer->renderTarget_setViewport(0, 0, windowWidth, windowHeight, 0.0f, 1.0f);
		g_renderer->ClearColorbuffer(padView);
	}

	shader_unbind(RendererShader::ShaderType::kGeometry);
	shader_bind(shader->GetVertexShader());
	shader_bind(shader->GetFragmentShader());

	// update and bind uniform buffer
	auto uniformBuffer = shader->FillUniformBlockBuffer(*texView, {imageWidth, imageHeight}, padView);
	glNamedBufferSubData(m_backbufferBlit_uniformBuffer, 0, sizeof(uniformBuffer), &uniformBuffer);

	glBindBufferBase(GL_UNIFORM_BUFFER, 0, m_backbufferBlit_uniformBuffer);

	// set viewport
	glViewportIndexedf(0, imageX, imageY, imageWidth, imageHeight);

	LatteTextureViewGL* texViewGL = (LatteTextureViewGL*)texView;
	texture_bindAndActivate(texView, 0);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	texViewGL->samplerState.clampS = texViewGL->samplerState.clampT = 0xFF;

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, useLinearTexFilter ? GL_LINEAR : GL_NEAREST);
	texViewGL->samplerState.filterMin = 0xFFFFFFFF;
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, useLinearTexFilter ? GL_LINEAR : GL_NEAREST);
	texViewGL->samplerState.filterMag = 0xFFFFFFFF;

	CapDisableGLES(GL_FRAMEBUFFER_SRGB);

	uint16 indexData[6] = { 0,1,2,3,4,5 };
	glDrawRangeElements(GL_TRIANGLES, 0, 5, 6, GL_UNSIGNED_SHORT, indexData);

	CapEnableGLES(GL_FRAMEBUFFER_SRGB);

	// unbind texture
	texture_bindAndActivate(nullptr, 0);

	catchOpenGLError();

	// restore viewport
	glViewportIndexedf(0, prevViewportX, prevViewportY, prevViewportWidth, prevViewportHeight);

	// switch back to TV context
	if (padView)
		GLCanvas_MakeCurrent(false);
}

void OpenGLRenderer::DrawBackbufferQuadFSR(LatteTextureView* texView, sint32 imageX, sint32 imageY, sint32 imageWidth, sint32 imageHeight, bool padView, bool clearBackground, bool renderUpsideDown)
{
	if (padView && !IsPadWindowActive())
		return;

	catchOpenGLError();
	GLCanvas_MakeCurrent(padView);

	renderstate_resetColorControl();
	renderstate_resetDepthControl();
	attributeStream_reset();

	// (re)create the EASU->RCAS intermediate texture if the output size changed
	if (m_fsrIntermediateWidth != imageWidth || m_fsrIntermediateHeight != imageHeight || m_fsrIntermediateTexture == 0)
	{
		if (m_fsrIntermediateFBO)
			glDeleteFramebuffers(1, &m_fsrIntermediateFBO);
		if (m_fsrIntermediateTexture)
			glDeleteTextures(1, &m_fsrIntermediateTexture);
		m_fsrIntermediateTexture = glCreateTextureWrapper(GL_TEXTURE_2D);
		glTextureStorage2DWrapper(GL_TEXTURE_2D, m_fsrIntermediateTexture, 1, GL_RGBA8, imageWidth, imageHeight);
		glTextureParameteriWrapper(GL_TEXTURE_2D, m_fsrIntermediateTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTextureParameteriWrapper(GL_TEXTURE_2D, m_fsrIntermediateTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTextureParameteriWrapper(GL_TEXTURE_2D, m_fsrIntermediateTexture, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTextureParameteriWrapper(GL_TEXTURE_2D, m_fsrIntermediateTexture, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glGenFramebuffers(1, &m_fsrIntermediateFBO);
		GLint prevFBO;
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
		glBindFramebuffer(GL_FRAMEBUFFER, m_fsrIntermediateFBO);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_fsrIntermediateTexture, 0);
		glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
		m_fsrIntermediateWidth = imageWidth;
		m_fsrIntermediateHeight = imageHeight;
	}

	// shared uniform values (same for both passes - only textureSrc/the render target differ)
	auto uniformBuffer = RendererOutputShader::s_fsr_easu_shader->FillUniformBlockBuffer(*texView, {imageWidth, imageHeight}, padView);
	glBindBufferBase(GL_UNIFORM_BUFFER, 0, m_backbufferBlit_uniformBuffer);
	uint16 indexData[6] = { 0,1,2,3,4,5 };

	// --- pass 1: EASU, game texture (native res) -> intermediate texture (output res) ---
	glBindFramebuffer(GL_FRAMEBUFFER, m_fsrIntermediateFBO);
	glViewportIndexedf(0, 0, 0, (float)imageWidth, (float)imageHeight);
	CapDisableGLES(GL_FRAMEBUFFER_SRGB); // EASU/RCAS both operate on raw values; sRGB encode (if any) is applied by RCAS's shared preamble, same as every other filter
	shader_unbind(RendererShader::ShaderType::kGeometry);
	shader_bind(RendererOutputShader::s_fsr_easu_shader->GetVertexShader());
	shader_bind(RendererOutputShader::s_fsr_easu_shader->GetFragmentShader());
	glNamedBufferSubData(m_backbufferBlit_uniformBuffer, 0, sizeof(uniformBuffer), &uniformBuffer);

	LatteTextureViewGL* texViewGL = (LatteTextureViewGL*)texView;
	texture_bindAndActivate(texView, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	texViewGL->samplerState.clampS = texViewGL->samplerState.clampT = 0xFF;
	// EASU samples via textureGather, which ignores min/mag filter entirely - NEAREST is set
	// here purely so nothing accidentally relies on mip filtering; it has no effect on gather.
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	texViewGL->samplerState.filterMin = texViewGL->samplerState.filterMag = 0xFFFFFFFF;
	glDrawRangeElements(GL_TRIANGLES, 0, 5, 6, GL_UNSIGNED_SHORT, indexData);
	texture_bindAndActivate(nullptr, 0);
	catchOpenGLError();

	// --- pass 2: RCAS, intermediate texture (output res) -> real backbuffer ---
	rendertarget_bindFramebufferObject(nullptr);
	if (clearBackground)
	{
		int windowWidth, windowHeight;
		if (padView)
			WindowSystem::GetPadWindowPhysSize(windowWidth, windowHeight);
		else
			WindowSystem::GetWindowPhysSize(windowWidth, windowHeight);
		g_renderer->renderTarget_setViewport(0, 0, windowWidth, windowHeight, 0.0f, 1.0f);
		g_renderer->ClearColorbuffer(padView);
	}
	glViewportIndexedf(0, imageX, imageY, imageWidth, imageHeight);
	// O passe EASU grava a imagem espelhada na textura intermediaria (mesma origem do clip control); o RCAS
	// precisa da variante OPOSTA para voltar a orientacao (sem isso o FSR Upscale saia de cabeca para baixo).
	const bool rcasUpsideDown = IsGLES() ? !renderUpsideDown : renderUpsideDown;
	RendererOutputShader* rcasShader = rcasUpsideDown ? RendererOutputShader::s_fsr_rcas_shader_ud : RendererOutputShader::s_fsr_rcas_shader;
	shader_bind(rcasShader->GetVertexShader());
	shader_bind(rcasShader->GetFragmentShader());
	glNamedBufferSubData(m_backbufferBlit_uniformBuffer, 0, sizeof(uniformBuffer), &uniformBuffer);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, m_fsrIntermediateTexture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glDrawRangeElements(GL_TRIANGLES, 0, 5, 6, GL_UNSIGNED_SHORT, indexData);
	glBindTexture(GL_TEXTURE_2D, 0);
	CapEnableGLES(GL_FRAMEBUFFER_SRGB);
	catchOpenGLError();

	// restore viewport
	glViewportIndexedf(0, prevViewportX, prevViewportY, prevViewportWidth, prevViewportHeight);

	if (padView)
		GLCanvas_MakeCurrent(false);
}

void OpenGLRenderer::ApplyFrontFace()
{
	bool ccw = m_guestFrontFaceCCW;
	if (s_clipControlEmulated && !_prevInvertY)
		ccw = !ccw; // emulando UPPER_LEFT: o flip de Y espelha o sentido (clip control preserva o sentido no clip space)
	glFrontFace(ccw ? GL_CCW : GL_CW);
}

void OpenGLRenderer::renderTarget_setViewport(float x, float y, float width, float height, float nearZ, float farZ, bool halfZ /*= false*/)
{
	if (prevNearZ != nearZ || prevFarZ != farZ || _prevHalfZ != halfZ)
	{
		if (*(uint32*)&farZ == 0x3f7fffff)
			*(uint32*)&farZ = 0x3f800000;
		if (glDepthRangedNV)
			glDepthRangedNV(nearZ, farZ);
		else
			glDepthRange(nearZ, farZ);
		prevNearZ = nearZ;
		prevFarZ = farZ;
	}

	bool invertY = false;
	if (height < 0.0)
	{
		invertY = true;
		y += height;
		height = -height;
	}

	if (glClipControl && (_prevInvertY != invertY || _prevHalfZ != halfZ))
	{
		GLenum clipDepth = halfZ ? GL_ZERO_TO_ONE : GL_NEGATIVE_ONE_TO_ONE;
		if (invertY)
			glClipControl(GL_LOWER_LEFT, clipDepth); // OpenGL style
		else
			glClipControl(GL_UPPER_LEFT, clipDepth); // DX style (default for GX2)
		_prevInvertY = invertY;
		_prevHalfZ = halfZ;
	}
	else if (s_clipControlEmulated && (_prevInvertY != invertY || _prevHalfZ != halfZ))
	{
		const bool invertChanged = (_prevInvertY != invertY);
		_prevInvertY = invertY;
		_prevHalfZ = halfZ;
		if (invertChanged)
			ApplyFrontFace(); // o flip de Y inverte o sentido dos poligonos
	}

	if (prevViewportX == x && prevViewportY == y && prevViewportWidth == width && prevViewportHeight == height)
		return; // viewport did not change
	glViewportIndexedf(0, x, y, width, height);
	prevViewportX = x;
	prevViewportY = y;
	prevViewportWidth = width;
	prevViewportHeight = height;
}

void OpenGLRenderer::renderTarget_setScissor(sint32 scissorX, sint32 scissorY, sint32 scissorWidth, sint32 scissorHeight)
{
	if (prevScissorEnable != true)
	{
		// enable scissor box
		glEnable(GL_SCISSOR_TEST);
		prevScissorEnable = true;
	}
	glScissor(scissorX, scissorY, scissorWidth, scissorHeight);
}

LatteCachedFBO* OpenGLRenderer::rendertarget_createCachedFBO(uint64 key)
{
	return new CachedFBOGL(key);
}

void OpenGLRenderer::rendertarget_deleteCachedFBO(LatteCachedFBO* cfbo)
{
	auto cfboGL = (CachedFBOGL*)cfbo;
	if (prevBoundFBO == cfboGL->glId_fbo)
	{
		glBindFramebuffer(GL_FRAMEBUFFER_EXT, 0);
		prevBoundFBO = 0;
	}
	glDeleteFramebuffers(1, &cfboGL->glId_fbo);
}

// set active FBO
void OpenGLRenderer::rendertarget_bindFramebufferObject(LatteCachedFBO* cfbo)
{
	GLuint fboid;
	if (cfbo)
	{
		const auto cfboGL = (CachedFBOGL*)cfbo;
		fboid = cfboGL->glId_fbo;
	}
	else
		fboid = 0;

	if (prevBoundFBO != fboid)
	{
		glBindFramebuffer(GL_FRAMEBUFFER_EXT, fboid);
		prevBoundFBO = fboid;
	}
}

void OpenGLRenderer::renderstate_setChannelTargetMask(uint32 renderTargetMask)
{
	if (renderTargetMask != prevTargetColorMask)
	{
		for (sint32 i = 0; i < 8; i++)
		{
			uint32 targetRGBAMask = ((renderTargetMask >> (i * 4)) & 0xF);
			if (targetRGBAMask != ((prevTargetColorMask >> (i * 4)) & 0xF))
			{
				// update color mask
				glColorMaski(i, (targetRGBAMask & 1) ? GL_TRUE : GL_FALSE, (targetRGBAMask & 2) ? GL_TRUE : GL_FALSE, (targetRGBAMask & 4) ? GL_TRUE : GL_FALSE, (targetRGBAMask & 8) ? GL_TRUE : GL_FALSE);
			}
		}
		prevTargetColorMask = renderTargetMask;
	}
}

void OpenGLRenderer::renderstate_setAlwaysWriteDepth()
{
	if (prevDepthEnable == 0)
	{
		glEnable(GL_DEPTH_TEST);
		prevDepthEnable = 1;
	}
	glDepthFunc(GL_ALWAYS);
	prevDepthFunc = Latte::LATTE_DB_DEPTH_CONTROL::E_ZFUNC::ALWAYS;
}

static const GLuint table_glBlendSrcDst[] =
{
	/* 0x00 */ GL_ZERO,
	/* 0x01 */ GL_ONE,
	/* 0x02 */ GL_SRC_COLOR,
	/* 0x03 */ GL_ONE_MINUS_SRC_COLOR,
	/* 0x04 */ GL_SRC_ALPHA,
	/* 0x05 */ GL_ONE_MINUS_SRC_ALPHA,
	/* 0x06 */ GL_DST_ALPHA,
	/* 0x07 */ GL_ONE_MINUS_DST_ALPHA,
	/* 0x08 */ GL_DST_COLOR,
	/* 0x09 */ GL_ONE_MINUS_DST_COLOR,
	/* 0x0A */ GL_SRC_ALPHA_SATURATE,
	/* 0x0B */ 0xFFFFFFFF,
	/* 0x0C */ 0xFFFFFFFF,
	/* 0x0D */ GL_CONSTANT_COLOR,
	/* 0x0E */ GL_ONE_MINUS_CONSTANT_COLOR,
	/* 0x0F */ GL_SRC1_COLOR,
	/* 0x10 */ GL_ONE_MINUS_SRC1_COLOR,
	/* 0x11 */ GL_SRC1_ALPHA,
	/* 0x12 */ GL_ONE_MINUS_SRC1_ALPHA,
	/* 0x13 */ GL_CONSTANT_ALPHA,
	/* 0x14 */ GL_ONE_MINUS_CONSTANT_ALPHA
};

static GLuint GetGLBlendFactor(Latte::LATTE_CB_BLENDN_CONTROL::E_BLENDFACTOR blendFactor)
{
	uint32 blendFactorU = (uint32)blendFactor;
	if (blendFactorU >= 0xF && blendFactorU <= 0x12)
	{
		debug_printf("Unsupported dual-source blending used\n");
		cemu_assert_debug(false); // dual-source blending
		return GL_ZERO;
	}
	if (blendFactorU >= (sizeof(table_glBlendSrcDst) / sizeof(table_glBlendSrcDst[0])))
	{
		debug_printf("GetGLBlendFactor: Constant 0x%x out of range\n", blendFactor);
		return GL_ZERO;
	}
	if (table_glBlendSrcDst[blendFactorU] == -1)
	{
		debug_printf("GetGLBlendFactor: Constant 0x%x is invalid\n", blendFactor);
		cemu_assert_debug(false);
		return GL_ZERO;
	}
	return table_glBlendSrcDst[blendFactorU];
}

static const GLuint table_glBlendCombine[] =
{
	GL_FUNC_ADD,
	GL_FUNC_SUBTRACT,
	GL_MIN,
	GL_MAX,
	GL_FUNC_REVERSE_SUBTRACT
};

GLuint GetGLBlendCombineFunc(Latte::LATTE_CB_BLENDN_CONTROL::E_COMBINEFUNC combineFunc)
{
	uint32 combineFuncU = (uint32)combineFunc;
	if (combineFuncU >= (sizeof(table_glBlendCombine) / sizeof(table_glBlendCombine[0])))
	{
		cemu_assert_suspicious();
		return GL_FUNC_ADD;
	}
	return table_glBlendCombine[combineFuncU];
}


void* OpenGLRenderer::texture_acquireTextureUploadBuffer(uint32 size)
{
	if (glRendererState.useTextureUploadBuffer)
	{
		glRendererState.texWorkBuffer = LatteRingBuffer_allocate(glRendererState.uploadRingBuffer, size, 1024);
		glRendererState.texWorkBufferSize = size;
		return glRendererState.texWorkBuffer;
	}
	// static memory buffer
	if (glRendererState.texUploadBuffer.size() < size)
	{
		glRendererState.texUploadBuffer.resize(size);
	}
	return glRendererState.texUploadBuffer.data();
}

void OpenGLRenderer::texture_releaseTextureUploadBuffer(uint8* mem)
{
	// do nothing
}

TextureDecoder* OpenGLRenderer::texture_chooseDecodedFormat(Latte::E_GX2SURFFMT format, bool isDepth, Latte::E_DIM dim, uint32 width, uint32 height)
{
	TextureDecoder* texDecoder = nullptr;
	if (isDepth)
	{
		if (format == Latte::E_GX2SURFFMT::R32_FLOAT)
		{
			return TextureDecoder_R32_FLOAT::getInstance();
		}
		if (format == Latte::E_GX2SURFFMT::D24_S8_UNORM)
		{
			return TextureDecoder_D24_S8::getInstance();
		}
		else if (format == Latte::E_GX2SURFFMT::D24_S8_FLOAT)
		{
			return TextureDecoder_NullData64::getInstance();
		}
		else if (format == Latte::E_GX2SURFFMT::D32_S8_FLOAT)
		{
			return TextureDecoder_D32_S8_UINT_X24::getInstance();
		}
		else if (format == Latte::E_GX2SURFFMT::R32_FLOAT)
		{
			return TextureDecoder_R32_FLOAT::getInstance();
		}
		else if (format == Latte::E_GX2SURFFMT::R16_UNORM)
		{
			return TextureDecoder_R16_FLOAT::getInstance();
		}
		return nullptr;
	}
	if (format == Latte::E_GX2SURFFMT::R4_G4_UNORM)
		texDecoder = TextureDecoder_R4_G4_UNORM_To_RGBA4::getInstance();
	else if (format == Latte::E_GX2SURFFMT::R4_G4_B4_A4_UNORM)
		texDecoder = TextureDecoder_R4_G4_B4_A4_UNORM::getInstance();
	else if (format == Latte::E_GX2SURFFMT::R16_G16_B16_A16_FLOAT)
		texDecoder = TextureDecoder_R16_G16_B16_A16_FLOAT::getInstance();
	else if (format == Latte::E_GX2SURFFMT::R16_G16_FLOAT)
		texDecoder = TextureDecoder_R16_G16_FLOAT::getInstance();
	else if (format == Latte::E_GX2SURFFMT::R16_SNORM)
		texDecoder = TextureDecoder_R16_SNORM::getInstance();
	else if (format == Latte::E_GX2SURFFMT::R16_FLOAT)
		texDecoder = TextureDecoder_R16_FLOAT::getInstance();
	else if (format == Latte::E_GX2SURFFMT::R32_FLOAT)
		texDecoder = TextureDecoder_R32_FLOAT::getInstance();
	else if (format == Latte::E_GX2SURFFMT::BC1_UNORM)
	{
		if (GetInstance()->m_supportedFormatInfo.fmt_bc1)
			texDecoder = TextureDecoder_BC1::getInstance();
		else if ((GetConfig().texture_compression_bc1 && OpenGLRenderer::CanUseEtc2Transcode(dim)) && GetInstance()->m_supportedFormatInfo.fmt_etc2)
			texDecoder = TextureDecoder_BC1_To_ETC2_RGB8::getInstance();
		else
			texDecoder = TextureDecoder_BC1_RGBA8::getInstance();
	}
	else if (format == Latte::E_GX2SURFFMT::BC1_SRGB)
	{
		if (GetInstance()->m_supportedFormatInfo.fmt_bc1)
			texDecoder = TextureDecoder_BC1::getInstance();
		else if ((GetConfig().texture_compression_bc1 && OpenGLRenderer::CanUseEtc2Transcode(dim)) && GetInstance()->m_supportedFormatInfo.fmt_etc2)
			texDecoder = TextureDecoder_BC1_To_ETC2_RGB8::getInstance();
		else
			texDecoder = TextureDecoder_BC1_RGBA8::getInstance();
	}
	else if (format == Latte::E_GX2SURFFMT::BC2_UNORM)
	{
		if (GetInstance()->m_supportedFormatInfo.fmt_bc2)
			texDecoder = TextureDecoder_BC2::getInstance();
		else if ((GetConfig().texture_compression_bc2 && OpenGLRenderer::CanUseEtc2Transcode(dim)) && GetInstance()->m_supportedFormatInfo.fmt_etc2)
			texDecoder = TextureDecoder_BC2_To_ETC2_RGBA8_EAC::getInstance();
		else
			texDecoder = TextureDecoder_BC2_RGBA8::getInstance();
	}
	else if (format == Latte::E_GX2SURFFMT::BC2_SRGB)
	{
		if (GetInstance()->m_supportedFormatInfo.fmt_bc2)
			texDecoder = TextureDecoder_BC2::getInstance();
		else if ((GetConfig().texture_compression_bc2 && OpenGLRenderer::CanUseEtc2Transcode(dim)) && GetInstance()->m_supportedFormatInfo.fmt_etc2)
			texDecoder = TextureDecoder_BC2_To_ETC2_RGBA8_EAC::getInstance();
		else
			texDecoder = TextureDecoder_BC2_RGBA8::getInstance();
	}
	else if (format == Latte::E_GX2SURFFMT::BC3_UNORM)
	{
		if (GetInstance()->m_supportedFormatInfo.fmt_bc3)
			texDecoder = TextureDecoder_BC3::getInstance();
		else if ((GetConfig().texture_compression_bc3 && OpenGLRenderer::CanUseEtc2Transcode(dim)) && GetInstance()->m_supportedFormatInfo.fmt_etc2)
			texDecoder = TextureDecoder_BC3_To_ETC2_RGBA8_EAC::getInstance();
		else
			texDecoder = TextureDecoder_BC3_RGBA8::getInstance();
	}
	else if (format == Latte::E_GX2SURFFMT::BC3_SRGB)
	{
		if (GetInstance()->m_supportedFormatInfo.fmt_bc3)
			texDecoder = TextureDecoder_BC3::getInstance();
		else if ((GetConfig().texture_compression_bc3 && OpenGLRenderer::CanUseEtc2Transcode(dim)) && GetInstance()->m_supportedFormatInfo.fmt_etc2)
			texDecoder = TextureDecoder_BC3_To_ETC2_RGBA8_EAC::getInstance();
		else
			texDecoder = TextureDecoder_BC3_RGBA8::getInstance();
	}
	else if (format == Latte::E_GX2SURFFMT::BC4_UNORM)
	{
		// Native BC4/RGTC and the ETC2/EAC transcode both require a plain 2D (or 2D
		// array) texture type in this GL backend - other dimensions (3D, cube, ...)
		// always fall back to the fully uncompressed path regardless of the options
		// below, same restriction the code already had before this feature existed.
		if (dim != Latte::E_DIM::DIM_2D && dim != Latte::E_DIM::DIM_2D_ARRAY)
			texDecoder = TextureDecoder_BC4_UNORM_uncompress::getInstance();
		else if (GetInstance()->m_supportedFormatInfo.fmt_bc4)
			texDecoder = TextureDecoder_BC4::getInstance();
		else if ((GetConfig().texture_compression_bc4 && OpenGLRenderer::CanUseEtc2Transcode(dim)) && GetInstance()->m_supportedFormatInfo.fmt_etc2)
			texDecoder = TextureDecoder_BC4_To_ETC2_R11EAC::getInstance();
		else
			texDecoder = TextureDecoder_BC4_R8::getInstance();
	}
	else if (format == Latte::E_GX2SURFFMT::BC4_SNORM)
	{
		// Fixed: this used to have native vs. uncompress backwards for the 2D case
		// (treating SNORM data as UNORM, silently corrupting anything using the negative
		// half of the range - typically normal map channels) - see the SNORM bug fix
		// report. TextureDecoder_BC4_UNORM_uncompress is still used for the final
		// fallback here deliberately: it decodes to plain UNORM floats, which is what
		// this GL path already expected downstream before this feature - a real SNORM
        // uncompressed fallback would need its own decoder and format, out of scope here.
		if (dim != Latte::E_DIM::DIM_2D && dim != Latte::E_DIM::DIM_2D_ARRAY)
			texDecoder = TextureDecoder_BC4_UNORM_uncompress::getInstance();
		else if (GetInstance()->m_supportedFormatInfo.fmt_bc4)
			texDecoder = TextureDecoder_BC4::getInstance();
		else if ((GetConfig().texture_compression_bc4 && OpenGLRenderer::CanUseEtc2Transcode(dim)) && GetInstance()->m_supportedFormatInfo.fmt_etc2)
			texDecoder = TextureDecoder_BC4_To_ETC2_R11EAC_SNORM::getInstance();
		else
			texDecoder = TextureDecoder_BC4_R8_SNORM::getInstance();
	}
	else if (format == Latte::E_GX2SURFFMT::BC5_UNORM)
	{
		if (GetInstance()->m_supportedFormatInfo.fmt_bc5)
			texDecoder = TextureDecoder_BC5::getInstance();
		else if ((GetConfig().texture_compression_bc5 && OpenGLRenderer::CanUseEtc2Transcode(dim)) && GetInstance()->m_supportedFormatInfo.fmt_etc2)
			texDecoder = TextureDecoder_BC5_To_ETC2_RG11EAC::getInstance();
		else
			texDecoder = TextureDecoder_BC5_RG8::getInstance();
	}
	else if (format == Latte::E_GX2SURFFMT::BC5_SNORM)
	{
		if (GetInstance()->m_supportedFormatInfo.fmt_bc5)
			texDecoder = TextureDecoder_BC5::getInstance();
		else if ((GetConfig().texture_compression_bc5 && OpenGLRenderer::CanUseEtc2Transcode(dim)) && GetInstance()->m_supportedFormatInfo.fmt_etc2)
			texDecoder = TextureDecoder_BC5_To_ETC2_RG11EAC_SNORM::getInstance();
		else
			texDecoder = TextureDecoder_BC5_RG8_SNORM::getInstance();
	}
	else if (format == Latte::E_GX2SURFFMT::R8_G8_B8_A8_UNORM)
		texDecoder = TextureDecoder_R8_G8_B8_A8::getInstance();
	else if (format == Latte::E_GX2SURFFMT::R8_G8_B8_A8_SNORM)
		texDecoder = TextureDecoder_R8_G8_B8_A8::getInstance();
	else if (format == Latte::E_GX2SURFFMT::R8_G8_B8_A8_SRGB)
		texDecoder = TextureDecoder_R8_G8_B8_A8::getInstance();
	else if (format == Latte::E_GX2SURFFMT::R8_UNORM)
		texDecoder = TextureDecoder_R8::getInstance();
	else if (format == Latte::E_GX2SURFFMT::R8_SNORM)
		texDecoder = TextureDecoder_R8::getInstance();
	else if (format == Latte::E_GX2SURFFMT::R8_G8_UNORM)
		texDecoder = TextureDecoder_R8_G8::getInstance();
	else if (format == Latte::E_GX2SURFFMT::R8_G8_SNORM)
		texDecoder = TextureDecoder_R8_G8::getInstance();
	else if (format == Latte::E_GX2SURFFMT::R16_UNORM)
		texDecoder = TextureDecoder_R16_UNORM::getInstance();
	else if (format == Latte::E_GX2SURFFMT::R16_G16_B16_A16_UNORM)
		texDecoder = TextureDecoder_R16_G16_B16_A16::getInstance();
	else if (format == Latte::E_GX2SURFFMT::R16_G16_B16_A16_SNORM)
		texDecoder = TextureDecoder_R16_G16_B16_A16::getInstance();
	else if (format == Latte::E_GX2SURFFMT::R16_G16_UNORM)
		texDecoder = TextureDecoder_R16_G16::getInstance();
	else if (format == Latte::E_GX2SURFFMT::R5_G6_B5_UNORM)
		texDecoder = IsGLES() ? (TextureDecoder*)TextureDecoder_R5_G6_B5_swappedRB::getInstance() : (TextureDecoder*)TextureDecoder_R5_G6_B5::getInstance();
	else if (format == Latte::E_GX2SURFFMT::R5_G5_B5_A1_UNORM)
		texDecoder = TextureDecoder_R5_G5_B5_A1_UNORM_swappedOpenGL::getInstance();
	else if (format == Latte::E_GX2SURFFMT::A1_B5_G5_R5_UNORM)
		texDecoder = TextureDecoder_A1_B5_G5_R5_UNORM::getInstance();
	else if (format == Latte::E_GX2SURFFMT::R32_G32_FLOAT)
		texDecoder = TextureDecoder_R32_G32_FLOAT::getInstance();
	else if (format == Latte::E_GX2SURFFMT::R32_G32_UINT)
		texDecoder = TextureDecoder_R32_G32_UINT::getInstance();
	else if (format == Latte::E_GX2SURFFMT::R32_UINT)
		texDecoder = TextureDecoder_R32_UINT::getInstance();
	else if (format == Latte::E_GX2SURFFMT::R16_UINT)
		texDecoder = TextureDecoder_R16_UINT::getInstance();
	else if (format == Latte::E_GX2SURFFMT::R8_UINT)
		texDecoder = TextureDecoder_R8_UINT::getInstance();
	else if (format == Latte::E_GX2SURFFMT::R32_G32_B32_A32_FLOAT)
		texDecoder = TextureDecoder_R32_G32_B32_A32_FLOAT::getInstance();
	else if (format == Latte::E_GX2SURFFMT::R10_G10_B10_A2_UNORM)
		texDecoder = TextureDecoder_R10_G10_B10_A2_UNORM::getInstance();
	else if (format == Latte::E_GX2SURFFMT::A2_B10_G10_R10_UNORM)
		texDecoder = TextureDecoder_A2_B10_G10_R10_UNORM_To_RGBA16::getInstance();
	else if (format == Latte::E_GX2SURFFMT::R10_G10_B10_A2_SNORM)
		texDecoder = TextureDecoder_R10_G10_B10_A2_SNORM_To_RGBA16::getInstance();
	else if (format == Latte::E_GX2SURFFMT::R10_G10_B10_A2_SRGB)
		texDecoder = TextureDecoder_R10_G10_B10_A2_UNORM::getInstance();
	else if (format == Latte::E_GX2SURFFMT::R11_G11_B10_FLOAT)
		texDecoder = TextureDecoder_R11_G11_B10_FLOAT::getInstance();
	else if (format == Latte::E_GX2SURFFMT::R32_G32_B32_A32_UINT)
		texDecoder = TextureDecoder_R32_G32_B32_A32_UINT::getInstance();
	else if (format == Latte::E_GX2SURFFMT::R16_G16_B16_A16_UINT)
		texDecoder = TextureDecoder_R16_G16_B16_A16_UINT::getInstance();
	else if (format == Latte::E_GX2SURFFMT::R8_G8_B8_A8_UINT)
		texDecoder = TextureDecoder_R8_G8_B8_A8_UINT::getInstance();
	else if (format == Latte::E_GX2SURFFMT::R24_X8_UNORM)
		texDecoder = TextureDecoder_R24_X8::getInstance();
	else if (format == Latte::E_GX2SURFFMT::X24_G8_UINT)
		texDecoder = TextureDecoder_X24_G8_UINT::getInstance();
	else if (format == Latte::E_GX2SURFFMT::D32_S8_FLOAT)
		texDecoder = TextureDecoder_D32_S8_UINT_X24::getInstance();
	else
		cemu_assert_debug(false);

	cemu_assert_debug(!isDepth);

	return texDecoder;
}

// use standard API to upload texture data
void OpenGLRenderer_texture_loadSlice_normal(LatteTexture* hostTextureGeneric, sint32 width, sint32 height, sint32 depth, void* pixelData, sint32 sliceIndex, sint32 mipIndex, uint32 imageSize)
{
	auto hostTexture = (LatteTextureGL*)hostTextureGeneric;
	sint32 effectiveWidth = width;
	sint32 effectiveHeight = height;
	sint32 effectiveDepth = depth;
	cemu_assert_debug(hostTexture->overwriteInfo.hasResolutionOverwrite == false); // not supported in _loadSlice
	cemu_assert_debug(hostTexture->overwriteInfo.hasFormatOverwrite == false); // not supported in _loadSlice
	// get format info
	LatteTextureGL::FormatInfoGL glFormatInfo;
	LatteTextureGL::GetOpenGLFormatInfo(hostTexture->isDepth, hostTexture->overwriteInfo.hasFormatOverwrite ? (Latte::E_GX2SURFFMT)hostTexture->overwriteInfo.format : hostTexture->format, hostTexture->dim, &glFormatInfo);
	// upload slice
	catchOpenGLError();
	if (mipIndex >= hostTexture->maxPossibleMipLevels)
	{
		cemuLog_logDebug(LogType::Force, "2D texture mip level allocated out of range");
		return;
	}
	if (hostTexture->dim == Latte::E_DIM::DIM_2D || hostTexture->dim == Latte::E_DIM::DIM_2D_MSAA)
	{
		if (glFormatInfo.glIsCompressed)
			glCompressedTextureSubImage2DWrapper(hostTexture->glTexTarget, hostTexture->glId_texture, mipIndex, 0, 0, effectiveWidth, effectiveHeight, glFormatInfo.glInternalFormat, imageSize, pixelData);
		else
			glTextureSubImage2DWrapper(hostTexture->glTexTarget, hostTexture->glId_texture, mipIndex, 0, 0, effectiveWidth, effectiveHeight, glFormatInfo.glSuppliedFormat, glFormatInfo.glSuppliedFormatType, pixelData);
	}
	else if (hostTexture->dim == Latte::E_DIM::DIM_1D)
	{
		if (OpenGLRenderer::IsGLES())
		{
			// hostTexture->glTexTarget is already GL_TEXTURE_2D here (redirected in
			// GenerateEmptyTextureFromGX2Dim - GLES has no GL_TEXTURE_1D), so upload through
			// the matching 2D entry points instead, height fixed at 1.
			if (glFormatInfo.glIsCompressed)
				glCompressedTextureSubImage2DWrapper(hostTexture->glTexTarget, hostTexture->glId_texture, mipIndex, 0, 0, width, 1, glFormatInfo.glInternalFormat, imageSize, pixelData);
			else
				glTextureSubImage2DWrapper(hostTexture->glTexTarget, hostTexture->glId_texture, mipIndex, 0, 0, width, 1, glFormatInfo.glSuppliedFormat, glFormatInfo.glSuppliedFormatType, pixelData);
		}
		else if (glFormatInfo.glIsCompressed)
			glCompressedTextureSubImage1DWrapper(hostTexture->glTexTarget, hostTexture->glId_texture, mipIndex, 0, width, glFormatInfo.glInternalFormat, imageSize, pixelData);
		else
			glTextureSubImage1DWrapper(hostTexture->glTexTarget, hostTexture->glId_texture, mipIndex, 0, width, glFormatInfo.glSuppliedFormat, glFormatInfo.glSuppliedFormatType, pixelData);
	}
	else if (hostTexture->dim == Latte::E_DIM::DIM_2D_ARRAY || hostTexture->dim == Latte::E_DIM::DIM_2D_ARRAY_MSAA ||
			 hostTexture->dim == Latte::E_DIM::DIM_3D ||
			 hostTexture->dim == Latte::E_DIM::DIM_CUBEMAP)
	{
		if (glFormatInfo.glIsCompressed)
			glCompressedTextureSubImage3DWrapper(hostTexture->glTexTarget, hostTexture->glId_texture, mipIndex, 0, 0, sliceIndex, effectiveWidth, effectiveHeight, 1, glFormatInfo.glInternalFormat, imageSize, pixelData);
		else
			glTextureSubImage3DWrapper(hostTexture->glTexTarget, hostTexture->glId_texture, mipIndex, 0, 0, sliceIndex, effectiveWidth, effectiveHeight, 1, glFormatInfo.glSuppliedFormat, glFormatInfo.glSuppliedFormatType, pixelData);
	}
	catchOpenGLError();
}

// use persistent buffers to upload data
void OpenGLRenderer_texture_loadSlice_viaBuffers(LatteTexture* hostTexture, sint32 width, sint32 height, sint32 depth, void* pixelData, sint32 sliceIndex, sint32 mipIndex, uint32 imageSize)
{
	// bind buffer
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, glRendererState.uploadBuffer);
	catchOpenGLError();
	// upload data to buffer
	cemu_assert(imageSize <= TEXBUFFER_SIZE);

	cemu_assert_debug(glRendererState.texWorkBufferSize == imageSize);
	glFlushMappedBufferRange(GL_PIXEL_UNPACK_BUFFER, (GLintptr)(glRendererState.texWorkBuffer - (uint8*)glRendererState.uploadBufferPtr), imageSize);
	catchOpenGLError();
	OpenGLRenderer_texture_loadSlice_normal(hostTexture, width, height, depth, (void*)(glRendererState.texWorkBuffer - (uint8*)glRendererState.uploadBufferPtr), sliceIndex, mipIndex, imageSize);
	// unbind buffer and sync
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
	catchOpenGLError();
}

void OpenGLRenderer::texture_loadSlice(LatteTexture* hostTexture, sint32 width, sint32 height, sint32 depth, void* pixelData, sint32 sliceIndex, sint32 mipIndex, uint32 imageSize)
{
	if (glRendererState.useTextureUploadBuffer)
		OpenGLRenderer_texture_loadSlice_viaBuffers(hostTexture, width, height, depth, pixelData, sliceIndex, mipIndex, imageSize);
	else
		OpenGLRenderer_texture_loadSlice_normal(hostTexture, width, height, depth, pixelData, sliceIndex, mipIndex, imageSize);
}

void OpenGLRenderer::texture_clearColorSlice(LatteTexture* hostTexture, sint32 sliceIndex, sint32 mipIndex, float r, float g, float b, float a)
{
	LatteTextureGL* texGL = (LatteTextureGL*)hostTexture;
	cemu_assert_debug(!texGL->isDepth);

	sint32 eWidth, eHeight;
	hostTexture->GetEffectiveSize(eWidth, eHeight, mipIndex);
	renderstate_resetColorControl();
	renderTarget_setViewport(0, 0, eWidth, eHeight, 0.0f, 1.0f);
	LatteMRT::BindColorBufferOnly(hostTexture->GetOrCreateView(mipIndex, 1, sliceIndex, 1));
	glClearColor(r, g, b, a);
	glClear(GL_COLOR_BUFFER_BIT);
}

void OpenGLRenderer::texture_clearDepthSlice(LatteTexture* hostTexture, uint32 sliceIndex, sint32 mipIndex, bool clearDepth, bool clearStencil, float depthValue, uint32 stencilValue)
{
	LatteTextureGL* texGL = (LatteTextureGL*)hostTexture;
	cemu_assert_debug(texGL->isDepth);

	sint32 eWidth, eHeight;
	hostTexture->GetEffectiveSize(eWidth, eHeight, mipIndex);
	renderstate_resetColorControl();
	renderstate_resetDepthControl();
	renderTarget_setViewport(0, 0, eWidth, eHeight, 0.0f, 1.0f);
	LatteMRT::BindDepthBufferOnly(hostTexture->GetOrCreateView(mipIndex, 1, sliceIndex, 1));

	if (!hostTexture->hasStencil)
		clearStencil = false;

	if (clearDepth)
		glClearDepth(clearDepth);
	if (clearStencil)
	{
		renderstate_resetStencilMask();
		glClearStencil((GLint)stencilValue);
	}
	glClear((clearDepth ? GL_DEPTH_BUFFER_BIT : 0) | (clearStencil ? GL_STENCIL_BUFFER_BIT : 0));
	catchOpenGLError();
}


void OpenGLRenderer::texture_clearSlice(LatteTexture* hostTextureGeneric, sint32 sliceIndex, sint32 mipIndex)
{
	auto hostTexture = (LatteTextureGL*)hostTextureGeneric;
	// get OpenGL format info
	LatteTextureGL::FormatInfoGL formatInfoGL;
	LatteTextureGL::GetOpenGLFormatInfo(hostTexture->isDepth, hostTexture->format, hostTexture->dim, &formatInfoGL);
	// get effective size of mip
	sint32 effectiveWidth, effectiveHeight;
	hostTexture->GetEffectiveSize(effectiveWidth, effectiveHeight, mipIndex);

	// on Nvidia glClearTexImage and glClearTexSubImage has bad performance (clearing a 4K texture takes up to 50ms)
	// clearing with glTextureSubImage2D from a CPU RAM buffer is only slightly slower
	// clearing with glTextureSubImage2D from a OpenGL buffer is 10-20% faster than glClearTexImage
	// clearing with FBO and glClear is orders of magnitude faster than the other methods
	// (these are results from 2018, may be different now)

	if (this->m_vendor == GfxVendor::Nvidia || glClearTexSubImage == nullptr)
	{
		if (formatInfoGL.glIsCompressed)
		{
			cemuLog_logDebug(LogType::Force, "Unsupported clear for compressed texture");
			return; // todo - create integer texture view to clear compressed textures
		}
		if (hostTextureGeneric->isDepth)
		{
			// V11.35: limpa texturas de profundidade (e stencil) via FBO. Antes retornava sem limpar no GLES, deixando
			// profundidade antiga na textura (sombras/z-fighting errados).
			const bool hasStencil = (formatInfoGL.glInternalFormat == 0x88F0 /*GL_DEPTH24_STENCIL8*/ || formatInfoGL.glInternalFormat == 0x8CAD /*GL_DEPTH32F_STENCIL8*/);
			const GLenum attachment = hasStencil ? 0x821A /*GL_DEPTH_STENCIL_ATTACHMENT*/ : GL_DEPTH_ATTACHMENT;
			glBindFramebuffer(GL_FRAMEBUFFER_EXT, glRendererState.clearFBO);
			glFramebufferTexture2D(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
			if (hostTexture->dim == Latte::E_DIM::DIM_2D)
				glFramebufferTexture2D(GL_FRAMEBUFFER_EXT, attachment, GL_TEXTURE_2D, hostTexture->glId_texture, mipIndex);
			else
				glFramebufferTextureLayer(GL_FRAMEBUFFER, attachment, hostTexture->glId_texture, mipIndex, sliceIndex);
			GLint prevDepthMaskI = 1;
			glGetIntegerv(GL_DEPTH_WRITEMASK, &prevDepthMaskI);
			const GLboolean scissorWasOn = glIsEnabled(GL_SCISSOR_TEST);
			glDepthMask(GL_TRUE);
			if (scissorWasOn)
				glDisable(GL_SCISSOR_TEST);
			glClearDepth(0.0);
			glClear(GL_DEPTH_BUFFER_BIT); // so profundidade (glStencilMask/glClearStencil nao estao carregadas neste loader)
			if (scissorWasOn)
				glEnable(GL_SCISSOR_TEST);
			glDepthMask(prevDepthMaskI ? GL_TRUE : GL_FALSE);
			glFramebufferTexture2D(GL_FRAMEBUFFER_EXT, attachment, GL_TEXTURE_2D, 0, 0);
			glBindFramebuffer(GL_FRAMEBUFFER_EXT, prevBoundFBO);
			return;
		}

		glBindFramebuffer(GL_FRAMEBUFFER_EXT, glRendererState.clearFBO);
		// set attachment
		if (hostTexture->dim == Latte::E_DIM::DIM_2D)
			glFramebufferTexture2D(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, hostTexture->glId_texture, mipIndex);
		else
			glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, hostTexture->glId_texture, mipIndex, sliceIndex);

		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		glBindFramebuffer(GL_FRAMEBUFFER_EXT, prevBoundFBO);
		return;
	}
	if (glClearTexSubImage == nullptr)
		return;
	glClearTexSubImage(hostTexture->glId_texture, mipIndex, 0, 0, sliceIndex, effectiveWidth, effectiveHeight, 1, formatInfoGL.glSuppliedFormat, formatInfoGL.glSuppliedFormatType, NULL);
}

LatteTexture* OpenGLRenderer::texture_createTextureEx(Latte::E_DIM dim, MPTR physAddress, MPTR physMipAddress, Latte::E_GX2SURFFMT format, uint32 width, uint32 height, uint32 depth, uint32 pitch, uint32 mipLevels,
	uint32 swizzle, Latte::E_HWTILEMODE tileMode, bool isDepth)
{
	return new LatteTextureGL(dim, physAddress, physMipAddress, format, width, height, depth, pitch, mipLevels, swizzle, tileMode, isDepth);
}

void OpenGLRenderer::texture_setActiveTextureUnit(sint32 index)
{
	if (activeTextureUnit != index)
	{
		glActiveTexture(GL_TEXTURE0 + index);
		activeTextureUnit = index;
	}
}

void OpenGLRenderer::texture_bindAndActivate(LatteTextureView* textureView, uint32 textureUnit)
{
	const auto textureViewGL = (LatteTextureViewGL*)textureView;
	if (textureViewGL)
		textureViewGL->RefreshFakeViewIfNeeded(); // views falsas (sem glTextureView) precisam acompanhar a textura base
	// don't call glBindTexture if the texture is already bound
	if (m_latteBoundTextures[textureUnit] == textureViewGL)
	{
		texture_setActiveTextureUnit(textureUnit);
		return; // already bound
	}
	// bind
	m_latteBoundTextures[textureUnit] = textureViewGL;
	texture_setActiveTextureUnit(textureUnit);
	if (textureViewGL)
	{
		glBindTexture(textureViewGL->glTexTarget, textureViewGL->glTexId);
	}
}

void OpenGLRenderer::texture_notifyDelete(LatteTextureView* textureView)
{
	for (uint32 i = 0; i < Latte::GPU_LIMITS::NUM_TEXTURES_PER_STAGE * 3; i++)
	{
		if (m_latteBoundTextures[i] == textureView)
			m_latteBoundTextures[i] = nullptr;
	}
}

// set Latte texture, on the OpenGL renderer this behaves like _bindAndActivate() but doesn't call _setActiveTextureUnit() if the texture is already bound
void OpenGLRenderer::texture_setLatteTexture(LatteTextureView* textureView1, uint32 textureUnit)
{
	auto textureView = ((LatteTextureViewGL*)textureView1);

	cemu_assert_debug(textureUnit < Latte::GPU_LIMITS::NUM_TEXTURES_PER_STAGE * 3);
	if (textureView)
		textureView->RefreshFakeViewIfNeeded(); // views falsas (sem glTextureView) precisam acompanhar a textura base
	if (m_latteBoundTextures[textureUnit] == textureView)
		return;
	if (textureView == nullptr)
		return;
	// bind
	if (glBindTextureUnit)
	{
		glBindTextureUnit(textureUnit, textureView->glTexId);
		m_latteBoundTextures[textureUnit] = textureView;
		activeTextureUnit = -1;
	}
	else
	{
		texture_setActiveTextureUnit(textureUnit);
		glBindTexture(textureView->glTexTarget, textureView->glTexId);
		m_latteBoundTextures[textureUnit] = textureView;
	}
}

void OpenGLRenderer::texture_copyImageSubData(LatteTexture* src, sint32 srcMip, sint32 effectiveSrcX, sint32 effectiveSrcY, sint32 srcSlice, LatteTexture* dst, sint32 dstMip, sint32 effectiveDstX, sint32 effectiveDstY,
	sint32 dstSlice, sint32 effectiveCopyWidth, sint32 effectiveCopyHeight, sint32 srcDepth)
{
	auto srcGL = (LatteTextureGL*)src;
	auto dstGL = (LatteTextureGL*)dst;
	if ((srcGL->isAlternativeFormat || dstGL->isAlternativeFormat) && (srcGL->glInternalFormat != dstGL->glInternalFormat))
	{
		if (srcGL->format == Latte::E_GX2SURFFMT::R16_G16_B16_A16_UINT && dstGL->format == Latte::E_GX2SURFFMT::BC4_UNORM)
		{
			cemu_assert_debug(dstGL->dim != Latte::E_DIM::DIM_2D);
			// special case where BC4 format is replaced with R16F for array/3d-textures (since OpenGL's BC4 compression only supports 2D textures)
			texture_syncSliceSpecialBC4(srcGL, srcSlice, srcMip, dstGL, dstSlice, dstMip);
			return;
		}
		else
		{
			cemuLog_logDebug(LogType::Force, "_syncSlice() called with unhandled alternative format");
			return;
		}
	}

	if (srcGL->format == Latte::E_GX2SURFFMT::R32_G32_B32_A32_UINT && dstGL->format == Latte::E_GX2SURFFMT::BC3_UNORM)
	{
		if ((dstGL->width >> dstMip) < 4 ||	(dstGL->height >> dstMip) < 4)
		{
			texture_syncSliceSpecialIntegerToBC3(srcGL, srcSlice, srcMip, dstGL, dstSlice, dstMip);
			return;

		}
	}
	catchOpenGLError();
	glCopyImageSubData(srcGL->glId_texture, srcGL->glTexTarget, srcMip, effectiveSrcX, effectiveSrcY, srcSlice, dstGL->glId_texture, dstGL->glTexTarget, dstMip, effectiveDstX, effectiveDstY, dstSlice, effectiveCopyWidth, effectiveCopyHeight, srcDepth);
	catchOpenGLError();
}

LatteTextureReadbackInfo* OpenGLRenderer::texture_createReadback(LatteTextureView* textureView)
{
	return new LatteTextureReadbackInfoGL(textureView);
}

void LatteDraw_resetAttributePointerCache();

void OpenGLRenderer::attributeStream_reset()
{
	// reset attribute state
	attributeStream_unbindVertexBuffer();
	// setup vertices
	SetArrayElementBuffer(0);
	LatteDraw_resetAttributePointerCache();
	SetAttributeArrayState(0, true, -1);
	SetAttributeArrayState(1, true, -1);
	for (uint32 i = 0; i < GPU_GL_MAX_NUM_ATTRIBUTE; i++)
		SetAttributeArrayState(i, false, -1);
}

void OpenGLRenderer::bufferCache_init(const sint32 bufferSize)
{
	glGenBuffers(1, &glAttributeCacheAB);
	glBindBuffer(GL_ARRAY_BUFFER, glAttributeCacheAB);
	glBufferData(GL_ARRAY_BUFFER, bufferSize, NULL, GL_STREAM_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void OpenGLRenderer::attributeStream_bindVertexCacheBuffer()
{
	if (_boundArrayBuffer == glAttributeCacheAB)
		return;
	_boundArrayBuffer = glAttributeCacheAB;
	glBindBuffer(GL_ARRAY_BUFFER, glAttributeCacheAB);
}

void OpenGLRenderer::attributeStream_unbindVertexBuffer()
{
	if (_boundArrayBuffer == 0)
		return;
	_boundArrayBuffer = 0;
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

RendererShader* OpenGLRenderer::shader_create(RendererShader::ShaderType type, uint64 baseHash, uint64 auxHash, const std::string& source, bool isGameShader, bool isGfxPackShader)
{
	return new RendererShaderGL(type, baseHash, auxHash, isGameShader, isGfxPackShader, source);
}

void OpenGLRenderer::shader_bind(RendererShader* shader)
{
	auto shaderGL = (RendererShaderGL*)shader;
	GLbitfield shaderBit;
	const auto program = shaderGL->GetProgram();
	switch(shader->GetType())
	{
	case RendererShader::ShaderType::kVertex:
		if (program == prevVertexShaderProgram)
			return;
		shaderBit = GL_VERTEX_SHADER_BIT;
		prevVertexShaderProgram = program;
		break;
	case RendererShader::ShaderType::kFragment:
		if (program == prevPixelShaderProgram)
			return;
		shaderBit = GL_FRAGMENT_SHADER_BIT;
		prevPixelShaderProgram = program;
		break;
	case RendererShader::ShaderType::kGeometry:
		if (program == prevGeometryShaderProgram)
			return;
		shaderBit = GL_GEOMETRY_SHADER_BIT;
		prevGeometryShaderProgram = program;
		break;
	default:
		UNREACHABLE;
	}

	catchOpenGLError();
	glUseProgramStages(m_pipeline, shaderBit, program);
	catchOpenGLError();
}

void OpenGLRenderer::shader_unbind(RendererShader::ShaderType shaderType)
{
	switch (shaderType) { 
		case RendererShader::ShaderType::kVertex:
		glUseProgramStages(m_pipeline, GL_VERTEX_SHADER_BIT, 0);
		prevVertexShaderProgram = -1;
		break;
	case RendererShader::ShaderType::kFragment: 
		glUseProgramStages(m_pipeline, GL_FRAGMENT_SHADER_BIT, 0);
		prevPixelShaderProgram = -1;
		break;
	case RendererShader::ShaderType::kGeometry: 
		glUseProgramStages(m_pipeline, GL_GEOMETRY_SHADER_BIT, 0);
		prevGeometryShaderProgram = -1;
		break;
	default: 
		UNREACHABLE;
	}
}

void decodeBC4Block_UNORM(uint8* blockStorage, float* rOutput);

void OpenGLRenderer::texture_syncSliceSpecialBC4(LatteTexture* srcTexture, sint32 srcSliceIndex, sint32 srcMipIndex, LatteTexture* dstTexture, sint32 dstSliceIndex, sint32 dstMipIndex)
{
	auto srcTextureGL = (LatteTextureGL*)srcTexture;
	auto dstTextureGL = (LatteTextureGL*)dstTexture;

	sint32 sourceTexWidth = std::max(srcTexture->width >> srcMipIndex, 1);
	sint32 sourceTexHeight = std::max(srcTexture->height >> srcMipIndex, 1);
	sint32 destTexWidth = std::max(dstTexture->width >> dstMipIndex, 1);
	sint32 destTexHeight = std::max(dstTexture->height >> dstMipIndex, 1);

	sint32 compressedCopyWidth = std::min(sourceTexWidth, std::max(1, destTexWidth / 4));
	sint32 compressedCopyHeight = std::min(sourceTexHeight, std::max(1, destTexHeight / 4));

	uint8* texelData = (uint8*)malloc(compressedCopyWidth*compressedCopyHeight * 8);
	float* pixelRGBA16fData = (float*)malloc(destTexWidth*destTexHeight * sizeof(float) * 2);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	if (glGetTextureSubImage)
		glGetTextureSubImage(srcTextureGL->glId_texture, 0, 0, 0, srcSliceIndex, compressedCopyWidth, compressedCopyHeight, 1, GL_RGBA_INTEGER, GL_UNSIGNED_SHORT, compressedCopyWidth * compressedCopyHeight * 8, texelData);
	for (sint32 bx = 0; bx < compressedCopyWidth; bx++)
	{
		for (sint32 by = 0; by < compressedCopyHeight; by++)
		{
			float rBlock[4 * 4];
			decodeBC4Block_UNORM(texelData + (bx + by * compressedCopyWidth) * 8, rBlock);
			for (sint32 sy = 0; sy < std::min(4, destTexHeight - by * 4); sy++)
			{
				for (sint32 sx = 0; sx < std::min(4, destTexWidth - bx * 4); sx++)
				{
					sint32 pixelIndex = (bx * 4 + sx) + (by * 4 + sy)*destTexWidth;
					pixelRGBA16fData[pixelIndex * 2] = rBlock[sx + sy * 4];
					pixelRGBA16fData[pixelIndex * 2 + 1] = rBlock[sx + sy * 4];
				}
			}
		}
	}
	// upload mip
	if (glGetTextureSubImage && glTextureSubImage3D)
		glTextureSubImage3D(dstTextureGL->glId_texture, dstMipIndex, 0, 0, dstSliceIndex, destTexWidth, destTexHeight, 1, GL_RG, GL_FLOAT, pixelRGBA16fData);
	free(pixelRGBA16fData);
	free(texelData);
	catchOpenGLError();
}

void OpenGLRenderer::texture_syncSliceSpecialIntegerToBC3(LatteTexture* srcTexture, sint32 srcSliceIndex, sint32 srcMipIndex, LatteTexture* dstTexture, sint32 dstSliceIndex, sint32 dstMipIndex)
{
	auto srcTextureGL = (LatteTextureGL*)srcTexture;
	auto dstTextureGL = (LatteTextureGL*)dstTexture;

	sint32 sourceTexWidth = std::max(srcTexture->width >> srcMipIndex, 1);
	sint32 sourceTexHeight = std::max(srcTexture->height >> srcMipIndex, 1);
	sint32 destTexWidth = std::max(dstTexture->width >> dstMipIndex, 1);
	sint32 destTexHeight = std::max(dstTexture->height >> dstMipIndex, 1);

	sint32 compressedCopyWidth = std::min(sourceTexWidth, std::max(1, destTexWidth / 4));
	sint32 compressedCopyHeight = std::min(sourceTexHeight, std::max(1, destTexHeight / 4));

	uint8* texelData = (uint8*)malloc(compressedCopyWidth*compressedCopyHeight * 16);

	catchOpenGLError();
	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	catchOpenGLError();
	if (glGetTextureSubImage)
		glGetTextureSubImage(srcTextureGL->glId_texture, 0, 0, 0, srcSliceIndex, compressedCopyWidth, compressedCopyHeight, 1, GL_RGBA_INTEGER, GL_UNSIGNED_INT, compressedCopyWidth * compressedCopyHeight * 16, texelData);
	//float* pixelRGBA16fData = (float*)malloc(destTexWidth*destTexHeight * sizeof(float) * 2);
	//for (sint32 bx = 0; bx < compressedCopyWidth; bx++)
	//{
	//	for (sint32 by = 0; by < compressedCopyHeight; by++)
	//	{
	//		float rBlock[4 * 4];
	//		decodeBC4Block_UNORM(texelData + (bx + by * compressedCopyWidth) * 8, rBlock);
	//		for (sint32 sy = 0; sy < min(4, destTexHeight - by * 4); sy++)
	//		{
	//			for (sint32 sx = 0; sx < min(4, destTexWidth - bx * 4); sx++)
	//			{
	//				sint32 pixelIndex = (bx * 4 + sx) + (by * 4 + sy)*destTexWidth;
	//				pixelRGBA16fData[pixelIndex * 2] = rBlock[sx + sy * 4];
	//				pixelRGBA16fData[pixelIndex * 2 + 1] = rBlock[sx + sy * 4];
	//			}
	//		}
	//	}
	//}
	// upload mip
	catchOpenGLError();
	if (glGetTextureSubImage && glCompressedTextureSubImage3D)
		glCompressedTextureSubImage3D(dstTextureGL->glId_texture, dstMipIndex, 0, 0, dstSliceIndex, destTexWidth, destTexHeight, 1, dstTextureGL->glInternalFormat, compressedCopyWidth * compressedCopyHeight * 16, texelData);
	free(texelData);
	catchOpenGLError();
}

void OpenGLRenderer::renderstate_updateBlendingAndColorControl()
{
	catchOpenGLError();
	const auto& colorControlReg = LatteGPUState.contextNew.CB_COLOR_CONTROL;

	const auto specialOp = colorControlReg.get_SPECIAL_OP();
	uint32 blendEnableMask = colorControlReg.get_BLEND_MASK();
	// V11.51: no GLES sem GL_EXT_float_blend, ligar o blend num alvo float de 32 bits e erro de GL (INVALID_OPERATION) e o draw some
	// (manchas pretas / efeitos faltando). Emulacao: nesses alvos o draw sai sem blend (sobrescreve em vez de misturar).
	// Se o driver tem a extensao (ou e GL desktop), nada muda e o blend e feito pelo driver.
	{
		static const bool s_blendOnFloat32Unsupported = [this]() {
			const char* ver = (const char*)glGetString(GL_VERSION);
			const bool es = ver && strncmp(ver, "OpenGL ES", 9) == 0;
			const bool unsupported = es && !IsGLExtensionSupported("GL_EXT_float_blend") && !IsGLExtensionSupported("GL_NV_float_blend");
			if (unsupported)
				cemuLog_log(LogType::Force, "[GL] sem GL_EXT_float_blend: blend sera DESLIGADO em alvos float de 32 bits (R32/RG32/RGBA32 float)");
			return unsupported;
		}();
		if (s_blendOnFloat32Unsupported && blendEnableMask != 0)
		{
			for (uint32 i = 0; i < 8; i++)
			{
				if ((blendEnableMask & (1u << i)) == 0)
					continue;
				const Latte::E_GX2SURFFMT fmt = LatteMRT::GetColorBufferFormat(i, LatteGPUState.contextNew);
				if (fmt == Latte::E_GX2SURFFMT::R32_FLOAT || fmt == Latte::E_GX2SURFFMT::R32_G32_FLOAT || fmt == Latte::E_GX2SURFFMT::R32_G32_B32_A32_FLOAT)
					blendEnableMask &= ~(1u << i);
			}
		}
	}
	const auto logicOp = colorControlReg.get_ROP();

	cemu_assert_debug(!colorControlReg.get_MULTIWRITE_ENABLE()); // not supported

	uint32 renderTargetMask = LatteGPUState.contextNew.CB_TARGET_MASK.get_MASK();
	if (specialOp == Latte::LATTE_CB_COLOR_CONTROL::E_SPECIALOP::DISABLE)
	{
		renderTargetMask = 0;
	}
	OpenGLRenderer::renderstate_setChannelTargetMask(renderTargetMask);
	catchOpenGLError();

	// handle depth and stencil control
	// get depth control parameters
	bool depthEnable = LatteGPUState.contextNew.DB_DEPTH_CONTROL.get_Z_ENABLE();
	auto depthFunc = LatteGPUState.contextNew.DB_DEPTH_CONTROL.get_Z_FUNC();
	bool depthWriteEnable = LatteGPUState.contextNew.DB_DEPTH_CONTROL.get_Z_WRITE_ENABLE();

	// get stencil control parameters
	bool stencilEnable = LatteGPUState.contextNew.DB_DEPTH_CONTROL.get_STENCIL_ENABLE();
	bool backStencilEnable = LatteGPUState.contextNew.DB_DEPTH_CONTROL.get_BACK_STENCIL_ENABLE();
	auto frontStencilFunc = LatteGPUState.contextNew.DB_DEPTH_CONTROL.get_STENCIL_FUNC_F();
	auto frontStencilZPass = LatteGPUState.contextNew.DB_DEPTH_CONTROL.get_STENCIL_ZPASS_F();
	auto frontStencilZFail = LatteGPUState.contextNew.DB_DEPTH_CONTROL.get_STENCIL_ZFAIL_F();
	auto frontStencilFail = LatteGPUState.contextNew.DB_DEPTH_CONTROL.get_STENCIL_FAIL_F();
	auto backStencilFunc = LatteGPUState.contextNew.DB_DEPTH_CONTROL.get_STENCIL_FUNC_B();
	auto backStencilZPass = LatteGPUState.contextNew.DB_DEPTH_CONTROL.get_STENCIL_ZPASS_B();
	auto backStencilZFail = LatteGPUState.contextNew.DB_DEPTH_CONTROL.get_STENCIL_ZFAIL_B();
	auto backStencilFail = LatteGPUState.contextNew.DB_DEPTH_CONTROL.get_STENCIL_FAIL_B();

	// get stencil control parameters
	uint32 stencilCompareMaskFront = LatteGPUState.contextNew.DB_STENCILREFMASK.get_STENCILMASK_F();
	uint32 stencilWriteMaskFront = LatteGPUState.contextNew.DB_STENCILREFMASK.get_STENCILWRITEMASK_F();
	uint32 stencilRefFront = LatteGPUState.contextNew.DB_STENCILREFMASK.get_STENCILREF_F();
	uint32 stencilCompareMaskBack = LatteGPUState.contextNew.DB_STENCILREFMASK_BF.get_STENCILMASK_B();
	uint32 stencilWriteMaskBack = LatteGPUState.contextNew.DB_STENCILREFMASK_BF.get_STENCILWRITEMASK_B();
	uint32 stencilRefBack = LatteGPUState.contextNew.DB_STENCILREFMASK_BF.get_STENCILREF_B();

	const static GLenum stencilActionGX2ToGL[] =
	{
		GL_KEEP,
		GL_ZERO,
		GL_REPLACE,
		GL_INCR,
		GL_DECR,
		GL_INVERT,
		GL_INCR_WRAP,
		GL_DECR_WRAP
	};

	if (prevStencilEnable != stencilEnable)
	{
		if (stencilEnable)
			glEnable(GL_STENCIL_TEST);
		else
			glDisable(GL_STENCIL_TEST);
		prevStencilEnable = stencilEnable;
	}

	// update stencil parameters only if stencil is enabled
	if (stencilEnable)
	{
		if (!backStencilEnable)
		{
			// if back face stencil is disabled then use front parameters
			backStencilFunc = frontStencilFunc;
			backStencilZPass = frontStencilZPass;
			backStencilZFail = frontStencilZFail;
			backStencilFail = frontStencilFail;
			stencilRefBack = stencilRefFront;
			stencilCompareMaskBack = stencilCompareMaskFront;
			stencilWriteMaskBack = stencilWriteMaskFront;
		}
		// update stencil configuration for front side
		if (prevFrontStencilFail != frontStencilFail || prevFrontStencilZFail != frontStencilZFail || prevFrontStencilZPass != frontStencilZPass)
		{
			glStencilOpSeparate(GL_FRONT, stencilActionGX2ToGL[(size_t)frontStencilFail], stencilActionGX2ToGL[(size_t)frontStencilZFail], stencilActionGX2ToGL[(size_t)frontStencilZPass]);
			prevFrontStencilFail = frontStencilFail;
			prevFrontStencilZFail = frontStencilZFail;
			prevFrontStencilZPass = frontStencilZPass;
		}
		if (prevFrontStencilFunc != frontStencilFunc || prevStencilRefFront != stencilRefFront || prevStencilCompareMaskFront != stencilCompareMaskFront)
		{
			glStencilFuncSeparate(GL_FRONT, glDepthFuncTable[(size_t)frontStencilFunc], stencilRefFront, stencilCompareMaskFront);
			prevFrontStencilFunc = frontStencilFunc;
			prevStencilRefFront = stencilRefFront;
			prevStencilCompareMaskFront = stencilCompareMaskFront;
		}
		if (prevStencilWriteMaskFront != stencilWriteMaskFront)
		{
			glStencilMaskSeparate(GL_FRONT, stencilWriteMaskFront);
			prevStencilWriteMaskFront = stencilWriteMaskFront;
		}
		// update stencil configuration for back side
		if (prevBackStencilFail != backStencilFail || prevBackStencilZFail != backStencilZFail || prevBackStencilZPass != backStencilZPass)
		{
			glStencilOpSeparate(GL_BACK, stencilActionGX2ToGL[(size_t)backStencilFail], stencilActionGX2ToGL[(size_t)backStencilZFail], stencilActionGX2ToGL[(size_t)backStencilZPass]);
			prevBackStencilFail = backStencilFail;
			prevBackStencilZFail = backStencilZFail;
			prevBackStencilZPass = backStencilZPass;
		}
		if (prevBackStencilFunc != backStencilFunc || prevStencilRefBack != stencilRefBack || prevStencilCompareMaskBack != stencilCompareMaskBack)
		{
			glStencilFuncSeparate(GL_BACK, glDepthFuncTable[(size_t)backStencilFunc], stencilRefBack, stencilCompareMaskBack);
			prevBackStencilFunc = backStencilFunc;
			prevStencilRefBack = stencilRefBack;
			prevStencilCompareMaskBack = stencilCompareMaskBack;
		}
		if (prevStencilWriteMaskBack != stencilWriteMaskBack)
		{
			glStencilMaskSeparate(GL_BACK, stencilWriteMaskBack);
			prevStencilWriteMaskBack = stencilWriteMaskBack;
		}
	}

	if (depthEnable != prevDepthEnable)
	{
		if (depthEnable)
			glEnable(GL_DEPTH_TEST);
		else
			glDisable(GL_DEPTH_TEST);
		prevDepthEnable = depthEnable;
	}
	if (depthWriteEnable != prevDepthWriteEnable)
	{
		if (depthWriteEnable)
			glDepthMask(GL_TRUE);
		else
			glDepthMask(GL_FALSE);
		prevDepthWriteEnable = depthWriteEnable;
	}
	if (depthFunc != prevDepthFunc)
	{
		glDepthFunc(glDepthFuncTable[(size_t)depthFunc]);
		prevDepthFunc = depthFunc;
	}

	catchOpenGLError();
	uint32 blendChangeMask = blendEnableMask ^ prevBlendMask;
	if (blendChangeMask)
	{
		for (uint32 i = 0; i < 8; i++)
		{
			if ((blendChangeMask & (1 << i)) != 0)
			{
				// bit changed -> blend mode was toggled
				if ((blendEnableMask & (1 << i)) != 0)
					glEnablei(GL_BLEND, i);
				else
					glDisablei(GL_BLEND, i);
			}
		}
		prevBlendMask = blendEnableMask;
	}
	catchOpenGLError();

	uint32* blendColorConstant = LatteGPUState.contextRegister + Latte::REGADDR::CB_BLEND_RED;
	if (blendColorConstant[0] != prevBlendColorConstant[0] ||
		blendColorConstant[1] != prevBlendColorConstant[1] ||
		blendColorConstant[2] != prevBlendColorConstant[2] ||
		blendColorConstant[3] != prevBlendColorConstant[3])
	{
		glBlendColor(*(float*)(blendColorConstant + 0), *(float*)(blendColorConstant + 1), *(float*)(blendColorConstant + 2), *(float*)(blendColorConstant + 3));
		prevBlendColorConstant[0] = blendColorConstant[0];
		prevBlendColorConstant[1] = blendColorConstant[1];
		prevBlendColorConstant[2] = blendColorConstant[2];
		prevBlendColorConstant[3] = blendColorConstant[3];
	}

	for (uint32 i = 0; i < 8; i++)
	{
		const auto& blendControlReg = LatteGPUState.contextNew.CB_BLENDN_CONTROL[i];
		if (blendControlReg.getRawValue() != prevBlendControlReg[i])
		{
			if (blendControlReg.get_SEPARATE_ALPHA_BLEND())
			{
				glBlendFuncSeparatei(i, GetGLBlendFactor(blendControlReg.get_COLOR_SRCBLEND()), GetGLBlendFactor(blendControlReg.get_COLOR_DSTBLEND()), GetGLBlendFactor(blendControlReg.get_ALPHA_SRCBLEND()), GetGLBlendFactor(blendControlReg.get_ALPHA_DSTBLEND()));
				glBlendEquationSeparatei(i, GetGLBlendCombineFunc(blendControlReg.get_COLOR_COMB_FCN()), GetGLBlendCombineFunc(blendControlReg.get_ALPHA_COMB_FCN()));
			}
			else
			{
				auto colorSrc = GetGLBlendFactor(blendControlReg.get_COLOR_SRCBLEND());
				auto colorDst = GetGLBlendFactor(blendControlReg.get_COLOR_DSTBLEND());
				glBlendFuncSeparatei(i, colorSrc, colorDst, colorSrc, colorDst);
				auto combineFunc = GetGLBlendCombineFunc(blendControlReg.get_COLOR_COMB_FCN());
				glBlendEquationSeparatei(i, combineFunc, combineFunc);
			}
			prevBlendControlReg[i] = blendControlReg.getRawValue();
		}
	}
	// set logic op
	uint32 logicOpGL = GL_COPY;
	if (logicOp == Latte::LATTE_CB_COLOR_CONTROL::E_LOGICOP::COPY)
		logicOpGL = GL_COPY;
	else if (logicOp == Latte::LATTE_CB_COLOR_CONTROL::E_LOGICOP::SET)
		logicOpGL = GL_SET;
	else if (logicOp == Latte::LATTE_CB_COLOR_CONTROL::E_LOGICOP::CLEAR)
		logicOpGL = GL_CLEAR;
	else if (logicOp == Latte::LATTE_CB_COLOR_CONTROL::E_LOGICOP::OR)
		logicOpGL = GL_OR;
	else
		cemu_assert_unimplemented();
	if (prevLogicOp != logicOpGL)
	{
		if (logicOpGL != GL_COPY)
			CapEnableGLES(GL_COLOR_LOGIC_OP);
		else
			CapDisableGLES(GL_COLOR_LOGIC_OP);
		glLogicOp(logicOpGL);
		prevLogicOp = logicOpGL;
	}

	// polygon control
	const auto& polygonControlReg = LatteGPUState.contextNew.PA_SU_SC_MODE_CNTL;
	const auto frontFace = polygonControlReg.get_FRONT_FACE();
	uint32 cullFront = polygonControlReg.get_CULL_FRONT();
	uint32 cullBack = polygonControlReg.get_CULL_BACK();
	// todo - polygon modes
	uint32 lineAndPointOffsetEnabled = polygonControlReg.get_OFFSET_PARA_ENABLED();
	uint32 polyOffsetFrontEnabled = polygonControlReg.get_OFFSET_FRONT_ENABLED();
	uint32 polyOffsetBackEnabled = polygonControlReg.get_OFFSET_BACK_ENABLED();

	uint32 cullEnable = (cullFront || cullBack) ? 1 : 0;
	if (prevCullEnable != cullEnable)
	{
		if (cullEnable)
			glEnable(GL_CULL_FACE);
		else
			glDisable(GL_CULL_FACE);
		prevCullEnable = cullEnable;
	}
	if (prevCullFrontFace != frontFace)
	{
		if (frontFace == Latte::LATTE_PA_SU_SC_MODE_CNTL::E_FRONTFACE::CCW)
			m_guestFrontFaceCCW = true;
		else if (frontFace == Latte::LATTE_PA_SU_SC_MODE_CNTL::E_FRONTFACE::CW)
			m_guestFrontFaceCCW = false;
		else
			cemu_assert_unimplemented();
		ApplyFrontFace();
		prevCullFrontFace = frontFace;
	}
	if (prevCullFront != cullFront || prevCullBack != cullBack)
	{
		if (cullFront && cullBack)
			glCullFace(GL_FRONT_AND_BACK);
		else if (cullFront == 0 && cullBack)
			glCullFace(GL_BACK);
		else if (cullFront && cullBack == 0)
			glCullFace(GL_FRONT);
		else
			; // front and back disabled, do nothing here since we force disable culling via glDisable(GL_CULL_FACE) above
		prevCullFront = cullFront;
		prevCullBack = cullBack;
	}

	if (polyOffsetFrontEnabled != prevPolygonOffsetFrontEnabled)
	{
		if (polyOffsetFrontEnabled)
			glEnable(GL_POLYGON_OFFSET_FILL);
		else
			glDisable(GL_POLYGON_OFFSET_FILL);
		prevPolygonOffsetFrontEnabled = polyOffsetFrontEnabled;

	}

	if (polyOffsetFrontEnabled)
	{
		// if polygon offset is enabled check if offset/scale register changed
		if (LatteGPUState.contextNew.PA_SU_POLY_OFFSET_FRONT_OFFSET.getRawValue() != prevPolygonFrontOffsetU32 || LatteGPUState.contextNew.PA_SU_POLY_OFFSET_FRONT_SCALE.getRawValue() != prevPolygonFrontScaleU32 || LatteGPUState.contextNew.PA_SU_POLY_OFFSET_CLAMP.getRawValue() != prevPolygonClampU32)
		{
			float frontScale = LatteGPUState.contextNew.PA_SU_POLY_OFFSET_FRONT_SCALE.get_SCALE();
			float frontOffset = LatteGPUState.contextNew.PA_SU_POLY_OFFSET_FRONT_OFFSET.get_OFFSET();
			float offsetClamp = LatteGPUState.contextNew.PA_SU_POLY_OFFSET_CLAMP.get_CLAMP();

			frontScale /= 16.0f;

			//if( glPolygonOffsetClampEXT )
			//	glPolygonOffsetClampEXT(frontOffset, frontScale, offsetClamp);		
			//else
			glPolygonOffset(frontScale, frontOffset);

			prevPolygonFrontOffsetU32 = LatteGPUState.contextNew.PA_SU_POLY_OFFSET_FRONT_SCALE.getRawValue();
			prevPolygonFrontScaleU32 = LatteGPUState.contextNew.PA_SU_POLY_OFFSET_FRONT_SCALE.getRawValue();
			prevPolygonClampU32 = LatteGPUState.contextNew.PA_SU_POLY_OFFSET_CLAMP.getRawValue();
		}
	}
	// clip control
	catchOpenGLError();
	cemu_assert_debug(LatteGPUState.contextNew.PA_CL_CLIP_CNTL.get_ZCLIP_NEAR_DISABLE() == LatteGPUState.contextNew.PA_CL_CLIP_CNTL.get_ZCLIP_FAR_DISABLE()); // near/far clipping disabled individually
	uint32 zClipEnable = LatteGPUState.contextNew.PA_CL_CLIP_CNTL.get_ZCLIP_NEAR_DISABLE() == false;
	// todo: Mass Effect 3 uses precompiled display lists which seem to write values to PA_CL_CLIP_CNTL which aren't available via the traditional GX2 API
	if (prevZClipEnable != zClipEnable)
	{
		if (HasDepthClamp()) // GLES sem GL_EXT_depth_clamp: a chamada gerava GL_INVALID_ENUM ("capability 34383") a cada troca
		{
			if (zClipEnable)
			{
				// disable depth clamping and enable clipping
				glDisable(GL_DEPTH_CLAMP);
			}
			else
			{
				// enable depth clamping and disable clipping
				glEnable(GL_DEPTH_CLAMP);
			}
		}
		prevZClipEnable = zClipEnable;
	}
	catchOpenGLError();
	// point size
	const auto& pointSizeReg = LatteGPUState.contextNew.PA_SU_POINT_SIZE;
	if (pointSizeReg.getRawValue() != prevPointSizeReg)
	{
		float pointWidth = (float)pointSizeReg.get_WIDTH() / 8.0f;
		float pointHeight = (float)pointSizeReg.get_HEIGHT() / 8.0f;
		if (pointWidth == 0.0f)
			glPointSize(1.0f / 8.0f); // minimum size
		else
			glPointSize(pointWidth);
		prevPointSizeReg = pointSizeReg.getRawValue();
		catchOpenGLError();
	}
	// primitive restart index
	uint32 primitiveRestartIndex = LatteGPUState.contextNew.VGT_MULTI_PRIM_IB_RESET_INDX.get_RESTART_INDEX();
	if (prevPrimitiveRestartIndex != primitiveRestartIndex)
	{
		glPrimitiveRestartIndex(primitiveRestartIndex);
		prevPrimitiveRestartIndex = primitiveRestartIndex;
	}
}

void OpenGLRenderer::renderstate_resetColorControl()
{
	renderstate_setChannelTargetMask(0xF);
	// disable blending
	uint32 blendEnableMask = 0;
	for (uint32 i = 0; i < 8; i++)
	{
		if (((blendEnableMask^prevBlendMask)&(1 << i)) != 0)
		{
			// bit changed -> blend mode was toggled
			if ((blendEnableMask&(1 << i)) != 0)
				glEnablei(GL_BLEND, i);
			else
				glDisablei(GL_BLEND, i);
		}
	}
	prevBlendMask = blendEnableMask;
	// "forget" blend states
	for (uint32 i = 0; i < 8; i++)
	{
		prevBlendControlReg[i] = 0xFFFFFFFF;
	}
	// disable alpha test
	if (prevAlphaTestEnable != 0)
	{
		CapDisableGLES(GL_ALPHA_TEST);
		prevAlphaTestEnable = 0;
	}
	if (prevLogicOp != GL_COPY)
	{
		CapDisableGLES(GL_COLOR_LOGIC_OP);
		glLogicOp(GL_COPY);
		prevLogicOp = GL_COPY;
	}
	// disable culling
	uint32 cullEnable = 0;
	if (prevCullEnable != cullEnable)
	{
		glDisable(GL_CULL_FACE);
		prevCullEnable = 0;
	}
	// disable polygon offset
	if (prevPolygonOffsetFrontEnabled != 0)
	{
		glDisable(GL_POLYGON_OFFSET_FILL);
		prevPolygonOffsetFrontEnabled = 0;
	}
	// disable scissor box
	if (prevScissorEnable != false)
	{
		glDisable(GL_SCISSOR_TEST);
		prevScissorEnable = false;
	}
}

void OpenGLRenderer::renderstate_resetDepthControl()
{
	if (prevDepthEnable)
	{
		glDisable(GL_DEPTH_TEST);
		prevDepthEnable = false;
	}
	if (!prevDepthWriteEnable)
	{
		glDepthMask(GL_TRUE);
		prevDepthWriteEnable = true;
	}
	if (prevStencilEnable)
	{
		glDisable(GL_STENCIL_TEST);
		prevStencilEnable = false;
	}
	//if (prevZClipEnable == 0)
	//{
	//	glDisable(GL_DEPTH_CLAMP);
	//	prevZClipEnable = 1;
	//}

	if (HasDepthClamp())
		glDisable(GL_DEPTH_CLAMP);
	prevZClipEnable = 1;

	if (prevPrimitiveRestartIndex != 0xFFFFFFFF)
	{
		glPrimitiveRestartIndex(0xFFFFFFFF);
		prevPrimitiveRestartIndex = 0xFFFFFFFF;
	}
}

void OpenGLRenderer::renderstate_resetStencilMask()
{
	uint32 stencilWriteMaskFront = 0xFFFFFFFF; // enable front mask
	uint32 stencilWriteMaskBack = 0xFFFFFFFF; // enable back mask
	if (prevStencilWriteMaskFront != stencilWriteMaskFront)
	{
		glStencilMaskSeparate(GL_FRONT, stencilWriteMaskFront);
		prevStencilWriteMaskFront = stencilWriteMaskFront;
	}
	if (prevStencilWriteMaskBack != stencilWriteMaskBack)
	{
		glStencilMaskSeparate(GL_BACK, stencilWriteMaskBack);
		prevStencilWriteMaskBack = stencilWriteMaskBack;
	}
}

void OpenGLRenderer::cleanupAfterFrame()
{
	ReleaseBufferCacheEntries();
}

void OpenGLRenderer::ReleaseBufferCacheEntries()
{
	for (auto& itr : m_destructionQueues.bufferCacheEntries)
		itr.free();
	m_destructionQueues.bufferCacheEntries.clear();
}
