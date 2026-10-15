// Frame Generation para OpenGL ES (Android). Espelha a logica do swapchain_vk.cpp / opticalflow_vk.cpp /
// frameinterpolation_vk.cpp, mas com compute shaders GLSL ES e texturas GL.
//
// Fluxo por frame REAL (OpenGLRenderer::SwapBuffers):
//   CaptureRealFrame()          -> copia o backbuffer para tex[atual] (+ copia reduzida se flow scale < 100%)
//   eglSwapBuffers (real)       -> o jogo continua normalmente
//   GenerateAfterRealSwap()     -> para k = 1..mult-1: [k==1: Optical Flow] + extrapolacao v8 + desenho + swap
//
// Este arquivo NAO inclui cabecalhos GL do Cemu (evita conflito de tipos): tem seus proprios ponteiros GLES
// carregados por dlsym(libGLESv3.so) / eglGetProcAddress, e suas proprias constantes.
#include "Cafe/HW/Latte/Renderer/OpenGL/FrameGenGL.h"

#if defined(__ANDROID__)

#include "Cafe/HW/Latte/Core/FrameGenSettings.h"
#include "Cafe/HW/Latte/Core/FrameGenStats.h"
#include "config/ActiveSettings.h"
#include "Cemu/Logging/CemuLogging.h"

#include <dlfcn.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace FrameGenGL
{
	namespace
	{
		// ---------------------------------------------------------------------------------------
		// Tipos e constantes GLES (proprios)
		// ---------------------------------------------------------------------------------------
		using GLuint = uint32_t;
		using GLint = int32_t;
		using GLsizei = int32_t;
		using GLenum = uint32_t;
		using GLbitfield = uint32_t;
		using GLboolean = uint8_t;
		using GLintptr = intptr_t;
		using GLsizeiptr = intptr_t;
		using GLchar = char;

		constexpr GLenum GL_TEXTURE_2D = 0x0DE1, GL_TEXTURE_MIN_FILTER = 0x2801, GL_TEXTURE_MAG_FILTER = 0x2800;
		constexpr GLenum GL_TEXTURE_WRAP_S = 0x2802, GL_TEXTURE_WRAP_T = 0x2803, GL_CLAMP_TO_EDGE = 0x812F;
		constexpr GLenum GL_NEAREST = 0x2600, GL_LINEAR = 0x2601, GL_TEXTURE_MAX_LEVEL = 0x813D;
		constexpr GLenum GL_RGBA8 = 0x8058, GL_RGBA16F = 0x881A, GL_R32UI = 0x8236, GL_R32I = 0x8235, GL_R32F = 0x822E;
		constexpr GLenum GL_RGBA = 0x1908, GL_UNSIGNED_BYTE = 0x1401;
		constexpr GLenum GL_RED = 0x1903, GL_RED_INTEGER = 0x8D94, GL_UNSIGNED_INT = 0x1405, GL_FLOAT = 0x1406;
		constexpr GLenum GL_READ_WRITE = 0x88BA, GL_WRITE_ONLY = 0x88B9;
		constexpr GLbitfield GL_ALL_BARRIER_BITS = 0xFFFFFFFFu;
		constexpr GLenum GL_COMPUTE_SHADER = 0x91B9, GL_VERTEX_SHADER = 0x8B31, GL_FRAGMENT_SHADER = 0x8B30;
		constexpr GLenum GL_COMPILE_STATUS = 0x8B81, GL_LINK_STATUS = 0x8B82, GL_INFO_LOG_LENGTH = 0x8B84;
		constexpr GLenum GL_UNIFORM_BUFFER = 0x8A11, GL_COPY_WRITE_BUFFER = 0x8F37, GL_DYNAMIC_DRAW = 0x88E8;
		constexpr GLenum GL_PIXEL_UNPACK_BUFFER = 0x88EC, GL_PIXEL_UNPACK_BUFFER_BINDING = 0x88EF;
		constexpr GLenum GL_FRAMEBUFFER = 0x8D40, GL_READ_FRAMEBUFFER = 0x8CA8, GL_DRAW_FRAMEBUFFER = 0x8CA9;
		constexpr GLenum GL_FRAMEBUFFER_BINDING = 0x8CA6, GL_READ_FRAMEBUFFER_BINDING = 0x8CAA;
		constexpr GLenum GL_COLOR_ATTACHMENT0 = 0x8CE0, GL_FRAMEBUFFER_COMPLETE = 0x8CD5;
		constexpr GLbitfield GL_COLOR_BUFFER_BIT = 0x4000;
		constexpr GLenum GL_CURRENT_PROGRAM = 0x8B8D, GL_ACTIVE_TEXTURE = 0x84E0, GL_TEXTURE0 = 0x84C0;
		constexpr GLenum GL_TEXTURE_BINDING_2D = 0x8069, GL_SAMPLER_BINDING = 0x8919, GL_VERTEX_ARRAY_BINDING = 0x85B5;
		constexpr GLenum GL_VIEWPORT = 0x0BA2, GL_COLOR_WRITEMASK = 0x0C23;
		constexpr GLenum GL_UNPACK_ROW_LENGTH = 0x0CF2, GL_UNPACK_SKIP_ROWS = 0x0CF3, GL_UNPACK_SKIP_PIXELS = 0x0CF4, GL_UNPACK_ALIGNMENT = 0x0CF5;
		constexpr GLenum GL_TRIANGLES = 0x0004, GL_VERSION = 0x1F02, GL_EXTENSIONS = 0x1F03, GL_MAX_IMAGE_UNITS = 0x8F38;
		constexpr GLenum kCaps[] = { 0x0B71 /*DEPTH_TEST*/, 0x0B90 /*STENCIL_TEST*/, 0x0BE2 /*BLEND*/, 0x0B44 /*CULL_FACE*/,
			0x0C11 /*SCISSOR_TEST*/, 0x0BD0 /*DITHER*/, 0x8C89 /*RASTERIZER_DISCARD*/, 0x8D69 /*PRIMITIVE_RESTART_FIXED_INDEX*/,
			0x809E /*SAMPLE_ALPHA_TO_COVERAGE*/, 0x8037 /*POLYGON_OFFSET_FILL*/ };
		constexpr int kNumCaps = (int)(sizeof(kCaps) / sizeof(kCaps[0]));

		constexpr int EGL_DRAW = 0x3059, EGL_WIDTH = 0x3057, EGL_HEIGHT = 0x3056;

		constexpr GLuint kUboBinding = 15; // binding alto: nao colide com os UBOs do Cemu (shaders sao reescritos em runtime)
		constexpr uint32_t kBlockSize = 8;
		constexpr uint32_t kHistogramElementCount = 256 * 3 * 3;
		constexpr uint32_t kHistogramsPerDim = 3;
		constexpr uint32_t kHistogramShifts = 3;
		constexpr int kNumUnitsSaved = 3;

		struct Fn
		{
			void (*glGenTextures)(GLsizei, GLuint*);
			void (*glDeleteTextures)(GLsizei, const GLuint*);
			void (*glBindTexture)(GLenum, GLuint);
			void (*glActiveTexture)(GLenum);
			void (*glTexParameteri)(GLenum, GLenum, GLint);
			void (*glTexStorage2D)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
			void (*glTexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*);
			void (*glCopyTexSubImage2D)(GLenum, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei);
			void (*glBindImageTexture)(GLuint, GLuint, GLint, GLboolean, GLint, GLenum, GLenum);
			void (*glMemoryBarrier)(GLbitfield);
			void (*glDispatchCompute)(GLuint, GLuint, GLuint);
			GLuint (*glCreateShader)(GLenum);
			void (*glShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*);
			void (*glCompileShader)(GLuint);
			void (*glGetShaderiv)(GLuint, GLenum, GLint*);
			void (*glGetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
			void (*glDeleteShader)(GLuint);
			GLuint (*glCreateProgram)();
			void (*glAttachShader)(GLuint, GLuint);
			void (*glLinkProgram)(GLuint);
			void (*glGetProgramiv)(GLuint, GLenum, GLint*);
			void (*glGetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
			void (*glDeleteProgram)(GLuint);
			void (*glUseProgram)(GLuint);
			GLint (*glGetUniformLocation)(GLuint, const GLchar*);
			void (*glUniform1i)(GLint, GLint);
			void (*glGenBuffers)(GLsizei, GLuint*);
			void (*glDeleteBuffers)(GLsizei, const GLuint*);
			void (*glBindBuffer)(GLenum, GLuint);
			void (*glBufferData)(GLenum, GLsizeiptr, const void*, GLenum);
			void (*glBufferSubData)(GLenum, GLintptr, GLsizeiptr, const void*);
			void (*glBindBufferBase)(GLenum, GLuint, GLuint);
			void (*glGenFramebuffers)(GLsizei, GLuint*);
			void (*glDeleteFramebuffers)(GLsizei, const GLuint*);
			void (*glBindFramebuffer)(GLenum, GLuint);
			void (*glFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
			GLenum (*glCheckFramebufferStatus)(GLenum);
			void (*glBlitFramebuffer)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
			void (*glGenVertexArrays)(GLsizei, GLuint*);
			void (*glDeleteVertexArrays)(GLsizei, const GLuint*);
			void (*glBindVertexArray)(GLuint);
			void (*glDrawArrays)(GLenum, GLint, GLsizei);
			void (*glViewport)(GLint, GLint, GLsizei, GLsizei);
			void (*glEnable)(GLenum);
			void (*glDisable)(GLenum);
			GLboolean (*glIsEnabled)(GLenum);
			void (*glColorMask)(GLboolean, GLboolean, GLboolean, GLboolean);
			void (*glGetIntegerv)(GLenum, GLint*);
			void (*glGetBooleanv)(GLenum, GLboolean*);
			void (*glBindSampler)(GLuint, GLuint);
			void (*glPixelStorei)(GLenum, GLint);
			const uint8_t* (*glGetString)(GLenum);
			GLenum (*glGetError)();
			void (*glGetIntegeri_v)(GLenum, GLuint, GLint*);
			void (*glGetInteger64i_v)(GLenum, GLuint, long long*);
			void (*glBindBufferRange)(GLenum, GLuint, GLuint, GLintptr, GLsizeiptr);
			void (*glReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
			// EGL
			void* (*eglGetCurrentDisplay)();
			void* (*eglGetCurrentSurface)(int);
			unsigned (*eglQuerySurface)(void*, void*, int, int*);
		};

		Fn g_fn{};
		bool g_fnLoaded = false;
		bool g_fnOk = false;

		void* ResolveSym(void* lib, void* libEGL, const char* name)
		{
			void* p = lib ? dlsym(lib, name) : nullptr;
			if (!p && libEGL)
			{
				using GetProc = void* (*)(const char*);
				GetProc getProc = (GetProc)dlsym(libEGL, "eglGetProcAddress");
				if (getProc)
					p = getProc(name);
			}
			return p;
		}

		bool LoadFunctions()
		{
			if (g_fnLoaded)
				return g_fnOk;
			g_fnLoaded = true;
			void* libGL = dlopen("libGLESv3.so", RTLD_NOW);
			void* libEGL = dlopen("libEGL.so", RTLD_NOW);
			if (!libGL || !libEGL)
			{
				cemuLog_log(LogType::Force, "[FrameGenGL] dlopen falhou (GLESv3={}, EGL={})", libGL != nullptr, libEGL != nullptr);
				return false;
			}
			std::string missing;
#define LOADFN(name) do { g_fn.name = (decltype(g_fn.name))ResolveSym(libGL, libEGL, #name); if (!g_fn.name) { missing += #name; missing += ' '; } } while (0)
			LOADFN(glGenTextures); LOADFN(glDeleteTextures); LOADFN(glBindTexture); LOADFN(glActiveTexture); LOADFN(glTexParameteri);
			LOADFN(glTexStorage2D); LOADFN(glTexSubImage2D); LOADFN(glCopyTexSubImage2D); LOADFN(glBindImageTexture);
			LOADFN(glMemoryBarrier); LOADFN(glDispatchCompute); LOADFN(glCreateShader); LOADFN(glShaderSource);
			LOADFN(glCompileShader); LOADFN(glGetShaderiv); LOADFN(glGetShaderInfoLog); LOADFN(glDeleteShader);
			LOADFN(glCreateProgram); LOADFN(glAttachShader); LOADFN(glLinkProgram); LOADFN(glGetProgramiv);
			LOADFN(glGetProgramInfoLog); LOADFN(glDeleteProgram); LOADFN(glUseProgram); LOADFN(glGetUniformLocation);
			LOADFN(glUniform1i); LOADFN(glGenBuffers); LOADFN(glDeleteBuffers); LOADFN(glBindBuffer); LOADFN(glBufferData);
			LOADFN(glBufferSubData); LOADFN(glBindBufferBase); LOADFN(glGenFramebuffers); LOADFN(glDeleteFramebuffers);
			LOADFN(glBindFramebuffer); LOADFN(glFramebufferTexture2D); LOADFN(glCheckFramebufferStatus);
			LOADFN(glBlitFramebuffer); LOADFN(glGenVertexArrays); LOADFN(glDeleteVertexArrays); LOADFN(glBindVertexArray);
			LOADFN(glDrawArrays); LOADFN(glViewport); LOADFN(glEnable); LOADFN(glDisable); LOADFN(glIsEnabled);
			LOADFN(glColorMask); LOADFN(glGetIntegerv); LOADFN(glGetBooleanv); LOADFN(glBindSampler); LOADFN(glPixelStorei);
			LOADFN(glGetString); LOADFN(glGetError);
			LOADFN(glGetIntegeri_v); LOADFN(glGetInteger64i_v); LOADFN(glBindBufferRange); LOADFN(glReadPixels);
			LOADFN(eglGetCurrentDisplay); LOADFN(eglGetCurrentSurface); LOADFN(eglQuerySurface);
#undef LOADFN
			if (!missing.empty())
			{
				cemuLog_log(LogType::Force, "[FrameGenGL] funcoes GLES/EGL ausentes: {} - frame gen desativado", missing);
				return false;
			}
			g_fnOk = true;
			return true;
		}

		// ---------------------------------------------------------------------------------------
		// Salva/restaura o estado GL que tocamos (o renderer do Cemu mantem caches de estado).
		// ---------------------------------------------------------------------------------------
		struct StateGuard
		{
			GLint program = 0, activeTex = 0, vao = 0, drawFbo = 0, readFbo = 0, unpackBuf = 0, copyWriteBuf = 0;
			GLint viewport[4] = {};
			GLboolean colorMask[4] = {};
			GLint texBind[kNumUnitsSaved] = {};
			GLint samplerBind[kNumUnitsSaved] = {};
			GLboolean caps[kNumCaps] = {};
			GLint unpack[4] = {};
			GLint uboBuf = 0; long long uboOffset = 0, uboSize = 0; // binding kUboBinding (o Cemu pode ter um UBO proprio la)

			StateGuard()
			{
				auto& f = g_fn;
				f.glGetIntegerv(GL_CURRENT_PROGRAM, &program);
				f.glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTex);
				f.glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
				f.glGetIntegerv(GL_FRAMEBUFFER_BINDING, &drawFbo);
				f.glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFbo);
				f.glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpackBuf);
				f.glGetIntegerv(GL_COPY_WRITE_BUFFER, &copyWriteBuf);
				f.glGetIntegerv(GL_VIEWPORT, viewport);
				f.glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
				for (int i = 0; i < kNumUnitsSaved; i++)
				{
					f.glActiveTexture(GL_TEXTURE0 + i);
					f.glGetIntegerv(GL_TEXTURE_BINDING_2D, &texBind[i]);
					f.glGetIntegerv(GL_SAMPLER_BINDING, &samplerBind[i]);
				}
				f.glActiveTexture((GLenum)activeTex);
				for (int i = 0; i < kNumCaps; i++)
					caps[i] = f.glIsEnabled(kCaps[i]);
				f.glGetIntegerv(GL_UNPACK_ROW_LENGTH, &unpack[0]);
				f.glGetIntegerv(GL_UNPACK_SKIP_ROWS, &unpack[1]);
				f.glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &unpack[2]);
				f.glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpack[3]);
				f.glGetIntegeri_v(0x8A28 /*UNIFORM_BUFFER_BINDING*/, kUboBinding, &uboBuf);
				f.glGetInteger64i_v(0x8A29 /*UNIFORM_BUFFER_START*/, kUboBinding, &uboOffset);
				f.glGetInteger64i_v(0x8A2A /*UNIFORM_BUFFER_SIZE*/, kUboBinding, &uboSize);

				// estado "neutro" para o nosso trabalho
				f.glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
				f.glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
				f.glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
				f.glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
				f.glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
				for (int i = 0; i < kNumCaps; i++)
					f.glDisable(kCaps[i]);
				f.glColorMask(1, 1, 1, 1);
			}

			~StateGuard()
			{
				auto& f = g_fn;
				f.glUseProgram((GLuint)program);
				for (int i = 0; i < kNumUnitsSaved; i++)
				{
					f.glActiveTexture(GL_TEXTURE0 + i);
					f.glBindTexture(GL_TEXTURE_2D, (GLuint)texBind[i]);
					f.glBindSampler(i, (GLuint)samplerBind[i]);
				}
				f.glActiveTexture((GLenum)activeTex);
				f.glBindVertexArray((GLuint)vao);
				f.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)drawFbo);
				f.glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)readFbo);
				f.glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
				f.glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
				for (int i = 0; i < kNumCaps; i++)
				{
					if (caps[i]) f.glEnable(kCaps[i]);
					else f.glDisable(kCaps[i]);
				}
				f.glBindBuffer(GL_COPY_WRITE_BUFFER, (GLuint)copyWriteBuf);
				f.glPixelStorei(GL_UNPACK_ROW_LENGTH, unpack[0]);
				f.glPixelStorei(GL_UNPACK_SKIP_ROWS, unpack[1]);
				f.glPixelStorei(GL_UNPACK_SKIP_PIXELS, unpack[2]);
				f.glPixelStorei(GL_UNPACK_ALIGNMENT, unpack[3]);
				f.glBindBuffer(GL_PIXEL_UNPACK_BUFFER, (GLuint)unpackBuf);
				if (uboBuf != 0)
					f.glBindBufferRange(GL_UNIFORM_BUFFER, kUboBinding, (GLuint)uboBuf, (GLintptr)uboOffset, (GLsizeiptr)uboSize);
				else
					f.glBindBufferBase(GL_UNIFORM_BUFFER, kUboBinding, 0);
			}
		};

		uint32_t DivRoundUp(uint32_t v, uint32_t d) { return (v + d - 1) / d; }

		// ---------------------------------------------------------------------------------------
		// Constantes (mesmo layout dos cbuffers do Vulkan)
		// ---------------------------------------------------------------------------------------
		struct OpticalFlowConstants
		{
			int32_t iInputLumaResolution[2];
			uint32_t uOpticalFlowPyramidLevel;
			uint32_t uOpticalFlowPyramidLevelCount;
			uint32_t iFrameIndex;
			uint32_t backbufferTransferFunction;
			float minMaxLuminance[2];
		};
		static_assert(sizeof(OpticalFlowConstants) == 32, "cbOF");

		struct InterpConstants
		{
			int32_t iSize[2];
			int32_t iFlowSize[2];
			float fFlowSign;
			float fFlowScale;
			float fT;
			float fErrThreshold;
			int32_t iBlock;
			int32_t pad[3];
		};
		static_assert(sizeof(InterpConstants) == 48, "cbInterp");

		enum PassId { P_PrepareLuma, P_Histogram, P_Divergence, P_Search, P_Filter, P_Interp, P_Count };
		const char* kPassFile[P_Count] = {
			"ffx_opticalflow_prepare_luma_pass",
			"ffx_opticalflow_generate_scd_histogram_pass",
			"ffx_opticalflow_compute_scd_divergence_pass",
			"ffx_opticalflow_compute_optical_flow_advanced_pass_v5",
			"ffx_opticalflow_filter_optical_flow_pass_v5",
			"mandacaru_frameinterpolation_v8",
		};

		struct SamplerAssign { const char* tex; int unit; };

		const char* kDisplayVS =
			"#version 300 es\n"
			"void main(){ vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));"
			" gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }\n";
		const char* kDisplayFS =
			"#version 300 es\n"
			"precision highp float;\n"
			"uniform highp sampler2D uTex;\n"
			"out vec4 oColor;\n"
			"void main(){ oColor = vec4(texelFetch(uTex, ivec2(gl_FragCoord.xy), 0).rgb, 1.0); }\n";

		// ---------------------------------------------------------------------------------------
		// Estado do Frame Gen
		// ---------------------------------------------------------------------------------------
		struct Tex { GLuint id = 0; uint32_t w = 0, h = 0; };

		struct State
		{
			bool initTried = false;
			bool failed = false;       // erro permanente: frame gen GL desligado
			bool resourcesReady = false;
			bool configWasOn = false;
			bool hasPrev = false;
			bool lumaPrevValid = false;

			uint32_t width = 0, height = 0;       // backbuffer
			uint32_t flowInW = 0, flowInH = 0;    // entrada do Optical Flow
			uint32_t flowW = 0, flowH = 0;        // textura de vetores
			int appliedFlowScale = 100;
			int es = 310;
			bool useScaled = false;

			Tex color[2];     // copias do backbuffer (atual/anterior alternando)
			Tex scaled[2];    // copias reduzidas (flow scale < 100)
			Tex luma, lumaPrev;
			Tex hist, prevHist, temp, scdOut;
			Tex flowRaw, flowFinal;
			Tex out;          // frame gerado (rgba16f)
			GLuint fboRead = 0, fboDraw = 0, vao = 0, ubo[2] = {}; // ubo[0]=cbOF ubo[1]=cbInterp
			GLuint prog[P_Count] = {};
			GLuint displayProg = 0;
			int curIdx = 1;
			uint64_t frameCounter = 0;
			bool firstFrameHist = true;

			// pacing
			std::chrono::steady_clock::time_point lastCapture{}, lastGenerated{}, limiterNext{};
			double realInterval = 1.0 / 30.0, lastRawInterval = 0.0;
			bool hasLastGen = false;
			int multiplier = 2, baseFpsLimit = 0;

			// estatisticas
			std::chrono::steady_clock::time_point statStart{};
			uint32_t statReal = 0, statGen = 0;
			// desenho/erro (log limitado)
			int logBudget = 8;
		};
		State S;

		void GlCheck(const char* where)
		{
			GLenum e = g_fn.glGetError();
			if (e != 0 && S.logBudget > 0)
			{
				S.logBudget--;
				cemuLog_log(LogType::Force, "[FrameGenGL] glGetError=0x{:x} em {}", (uint32_t)e, where);
			}
		}

		bool QuerySurfaceSize(uint32_t& w, uint32_t& h)
		{
			void* dpy = g_fn.eglGetCurrentDisplay();
			void* surf = g_fn.eglGetCurrentSurface(EGL_DRAW);
			if (!dpy || !surf)
				return false;
			int iw = 0, ih = 0;
			if (!g_fn.eglQuerySurface(dpy, surf, EGL_WIDTH, &iw) || !g_fn.eglQuerySurface(dpy, surf, EGL_HEIGHT, &ih))
				return false;
			if (iw <= 0 || ih <= 0)
				return false;
			w = (uint32_t)iw;
			h = (uint32_t)ih;
			return true;
		}

		void MakeTex(Tex& t, GLenum fmt, uint32_t w, uint32_t h, GLenum filter)
		{
			auto& f = g_fn;
			f.glGenTextures(1, &t.id);
			f.glActiveTexture(GL_TEXTURE0);
			f.glBindTexture(GL_TEXTURE_2D, t.id);
			f.glTexStorage2D(GL_TEXTURE_2D, 1, fmt, (GLsizei)w, (GLsizei)h);
			f.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint)filter);
			f.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLint)filter);
			f.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (GLint)GL_CLAMP_TO_EDGE);
			f.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (GLint)GL_CLAMP_TO_EDGE);
			f.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
			t.w = w;
			t.h = h;
		}

		void FreeTex(Tex& t)
		{
			if (t.id)
				g_fn.glDeleteTextures(1, &t.id);
			t = Tex{};
		}

		std::string ReadFileText(const std::string& path)
		{
			std::ifstream in(path, std::ios::binary);
			if (!in.is_open())
				return {};
			std::stringstream ss;
			ss << in.rdbuf();
			return ss.str();
		}

		GLuint CompileProgramSrc(const char* label, const std::string* csSrc, const char* vsSrc, const char* fsSrc)
		{
			auto& f = g_fn;
			auto compile = [&](GLenum type, const char* src) -> GLuint
			{
				GLuint sh = f.glCreateShader(type);
				f.glShaderSource(sh, 1, &src, nullptr);
				f.glCompileShader(sh);
				GLint ok = 0;
				f.glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
				if (!ok)
				{
					GLint len = 0;
					f.glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &len);
					std::string log((size_t)std::max(len, 1), '\0');
					f.glGetShaderInfoLog(sh, (GLsizei)log.size(), nullptr, log.data());
					cemuLog_log(LogType::Force, "[FrameGenGL] shader '{}' NAO compilou: {}", label, log.c_str());
					f.glDeleteShader(sh);
					return 0;
				}
				return sh;
			};
			GLuint prog = f.glCreateProgram();
			GLuint a = 0, b = 0;
			if (csSrc)
			{
				a = compile(GL_COMPUTE_SHADER, csSrc->c_str());
				if (!a) { f.glDeleteProgram(prog); return 0; }
				f.glAttachShader(prog, a);
			}
			else
			{
				a = compile(GL_VERTEX_SHADER, vsSrc);
				b = a ? compile(GL_FRAGMENT_SHADER, fsSrc) : 0;
				if (!a || !b)
				{
					if (a) f.glDeleteShader(a);
					f.glDeleteProgram(prog);
					return 0;
				}
				f.glAttachShader(prog, a);
				f.glAttachShader(prog, b);
			}
			f.glLinkProgram(prog);
			GLint ok = 0;
			f.glGetProgramiv(prog, GL_LINK_STATUS, &ok);
			if (a) f.glDeleteShader(a);
			if (b) f.glDeleteShader(b);
			if (!ok)
			{
				GLint len = 0;
				f.glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
				std::string log((size_t)std::max(len, 1), '\0');
				f.glGetProgramInfoLog(prog, (GLsizei)log.size(), nullptr, log.data());
				cemuLog_log(LogType::Force, "[FrameGenGL] programa '{}' NAO linkou: {}", label, log.c_str());
				f.glDeleteProgram(prog);
				return 0;
			}
			return prog;
		}

		void ReplaceAll(std::string& s, const std::string& from, const std::string& to)
		{
			size_t pos = 0;
			while ((pos = s.find(from, pos)) != std::string::npos)
			{
				s.replace(pos, from.size(), to);
				pos += to.size();
			}
		}

		bool DetectEsVersionAndExt()
		{
			const char* ver = (const char*)g_fn.glGetString(GL_VERSION);
			const char* ext = (const char*)g_fn.glGetString(GL_EXTENSIONS);
			int major = 0, minor = 0;
			if (ver)
			{
				const char* p = std::strstr(ver, "OpenGL ES ");
				if (p)
					std::sscanf(p + 10, "%d.%d", &major, &minor);
			}
			GLint imageUnits = 0;
			g_fn.glGetIntegerv(GL_MAX_IMAGE_UNITS, &imageUnits);
			cemuLog_log(LogType::Force, "[FrameGenGL] GL_VERSION='{}' (ES {}.{}) unidades de imagem={}", ver ? ver : "?", major, minor, (int)imageUnits);
			if (major < 3 || (major == 3 && minor < 1))
			{
				cemuLog_log(LogType::Force, "[FrameGenGL] precisa de OpenGL ES 3.1+ (compute shaders) - desativado");
				return false;
			}
			S.es = (major > 3 || minor >= 2) ? 320 : 310;
			if (S.es == 310)
			{
				const bool hasAtomic = ext && std::strstr(ext, "GL_OES_shader_image_atomic");
				if (!hasAtomic)
				{
					cemuLog_log(LogType::Force, "[FrameGenGL] ES 3.1 sem GL_OES_shader_image_atomic (necessaria ao detector de troca de cena) - desativado");
					return false;
				}
			}
			return true;
		}

		void DestroyResources()
		{
			if (!g_fnOk)
				return;
			auto& f = g_fn;
			for (auto& t : S.color) FreeTex(t);
			for (auto& t : S.scaled) FreeTex(t);
			FreeTex(S.luma); FreeTex(S.lumaPrev); FreeTex(S.hist); FreeTex(S.prevHist); FreeTex(S.temp); FreeTex(S.scdOut);
			FreeTex(S.flowRaw); FreeTex(S.flowFinal); FreeTex(S.out);
			if (S.fboRead) f.glDeleteFramebuffers(1, &S.fboRead);
			if (S.fboDraw) f.glDeleteFramebuffers(1, &S.fboDraw);
			if (S.vao) f.glDeleteVertexArrays(1, &S.vao);
			if (S.ubo[0] || S.ubo[1]) f.glDeleteBuffers(2, S.ubo);
			S.fboRead = S.fboDraw = S.vao = 0;
			S.ubo[0] = S.ubo[1] = 0;
			S.resourcesReady = false;
			S.hasPrev = false;
			S.lumaPrevValid = false;
			S.curIdx = 1;
			S.firstFrameHist = true;
		}

		bool CreatePrograms()
		{
			auto& f = g_fn;
			const std::string dir = "resources/ffx-frame-generation/shaders_glsl/" + std::string(S.es == 320 ? "es320" : "es310") + "/";
			const SamplerAssign kSamplers[P_Count][3] = {
				/*PrepareLuma*/ { { "r_input_color", 0 }, { nullptr, 0 }, { nullptr, 0 } },
				/*Histogram  */ { { "r_optical_flow_input", 0 }, { nullptr, 0 }, { nullptr, 0 } },
				/*Divergence */ { { nullptr, 0 }, { nullptr, 0 }, { nullptr, 0 } },
				/*Search     */ { { "r_optical_flow_input", 0 }, { "r_optical_flow_previous_input", 1 }, { nullptr, 0 } },
				/*Filter     */ { { "r_optical_flow_previous", 0 }, { nullptr, 0 }, { nullptr, 0 } },
				/*Interp     */ { { "r_prev", 0 }, { "r_cur", 1 }, { "r_flow", 2 } },
			};
			for (int i = 0; i < P_Count; i++)
			{
				const std::string path = ActiveSettings::GetDataPath("{}{}.comp.glsl", dir, kPassFile[i]).string();
				std::string src = ReadFileText(path);
				if (src.empty())
				{
					cemuLog_log(LogType::Force, "[FrameGenGL] nao consegui ler o shader {}", path);
					return false;
				}
				// move o UBO para um binding alto (nao interfere nos UBOs do renderer)
				ReplaceAll(src, "layout(binding = 0, std140) uniform", "layout(binding = 15, std140) uniform");
				S.prog[i] = CompileProgramSrc(kPassFile[i], &src, nullptr, nullptr);
				if (!S.prog[i])
					return false;
				f.glUseProgram(S.prog[i]);
				for (const auto& sa : kSamplers[i])
				{
					if (!sa.tex)
						continue;
					const std::string name = std::string("SPIRV_Cross_Combined") + sa.tex + "SPIRV_Cross_DummySampler";
					GLint loc = f.glGetUniformLocation(S.prog[i], name.c_str());
					if (loc < 0)
					{
						cemuLog_log(LogType::Force, "[FrameGenGL] sampler '{}' nao encontrado em {}", name, kPassFile[i]);
						return false;
					}
					f.glUniform1i(loc, sa.unit);
				}
			}
			S.displayProg = CompileProgramSrc("display", nullptr, kDisplayVS, kDisplayFS);
			if (!S.displayProg)
				return false;
			f.glUseProgram(S.displayProg);
			f.glUniform1i(f.glGetUniformLocation(S.displayProg, "uTex"), 0);
			return true;
		}

		bool CreateResources(uint32_t w, uint32_t h, int flowScalePercent)
		{
			auto& f = g_fn;
			S.width = w;
			S.height = h;
			flowScalePercent = std::clamp(flowScalePercent, 25, 100);
			if (flowScalePercent >= 100)
			{
				S.flowInW = w;
				S.flowInH = h;
				flowScalePercent = 100;
			}
			else
			{
				S.flowInW = std::max<uint32_t>(64, ((w * (uint32_t)flowScalePercent) / 100) & ~7u);
				S.flowInH = std::max<uint32_t>(64, ((h * (uint32_t)flowScalePercent) / 100) & ~7u);
			}
			S.useScaled = flowScalePercent < 100;
			S.appliedFlowScale = flowScalePercent;
			S.flowW = DivRoundUp(S.flowInW, kBlockSize);
			S.flowH = DivRoundUp(S.flowInH, kBlockSize);

			for (int i = 0; i < 2; i++)
			{
				MakeTex(S.color[i], GL_RGBA8, w, h, GL_NEAREST);
				if (S.useScaled)
					MakeTex(S.scaled[i], GL_RGBA8, S.flowInW, S.flowInH, GL_NEAREST);
			}
			MakeTex(S.luma, GL_R32UI, S.flowInW, S.flowInH, GL_NEAREST);
			MakeTex(S.lumaPrev, GL_R32UI, S.flowInW, S.flowInH, GL_NEAREST);
			MakeTex(S.hist, GL_R32UI, kHistogramElementCount, 1, GL_NEAREST);
			MakeTex(S.prevHist, GL_R32F, kHistogramElementCount, 1, GL_NEAREST);
			MakeTex(S.temp, GL_R32UI, kHistogramElementCount, 1, GL_NEAREST);
			MakeTex(S.scdOut, GL_R32UI, kHistogramElementCount, 1, GL_NEAREST);
			MakeTex(S.flowRaw, GL_R32I, S.flowW, S.flowH, GL_NEAREST);
			MakeTex(S.flowFinal, GL_R32I, S.flowW, S.flowH, GL_NEAREST);
			MakeTex(S.out, GL_RGBA16F, w, h, GL_LINEAR);

			f.glGenFramebuffers(1, &S.fboRead);
			f.glGenFramebuffers(1, &S.fboDraw);
			f.glGenVertexArrays(1, &S.vao);
			f.glGenBuffers(2, S.ubo);
			f.glBindBuffer(GL_COPY_WRITE_BUFFER, S.ubo[0]);
			f.glBufferData(GL_COPY_WRITE_BUFFER, sizeof(OpticalFlowConstants), nullptr, GL_DYNAMIC_DRAW);
			f.glBindBuffer(GL_COPY_WRITE_BUFFER, S.ubo[1]);
			f.glBufferData(GL_COPY_WRITE_BUFFER, sizeof(InterpConstants), nullptr, GL_DYNAMIC_DRAW);
			f.glActiveTexture(GL_TEXTURE0);
			f.glBindTexture(GL_TEXTURE_2D, 0);
			GlCheck("CreateResources");
			S.resourcesReady = true;
			cemuLog_log(LogType::Force, "[FrameGenGL] recursos criados: tela {}x{}, entrada do Optical Flow {}x{} (flow scale {}%), vetores {}x{}, shaders {} es",
				w, h, S.flowInW, S.flowInH, flowScalePercent, S.flowW, S.flowH, S.es);
			return true;
		}

		// Ativa/desativa conforme config e (re)cria recursos quando preciso. Retorna true se ativo.
		bool UpdateActive()
		{
			if (S.failed)
				return false;
			const auto cfg = FrameGenSettings::Get();
			const bool on = cfg.enabled;
			if (on != S.configWasOn)
			{
				cemuLog_log(LogType::Force, "[FrameGenGL] {}", on ? "ativado" : "desativado");
				S.configWasOn = on;
				if (on)
				{
					S.hasPrev = false;
					S.hasLastGen = false;
					S.curIdx = 1;
					S.lastCapture = {};
					S.limiterNext = {};
					S.lumaPrevValid = false;
				}
			}
			if (!on)
			{
				FrameGenStats::active.store(false, std::memory_order_relaxed);
				FrameGenStats::realFps.store(0.0f, std::memory_order_relaxed);
				FrameGenStats::generatedFps.store(0.0f, std::memory_order_relaxed);
				FrameGenStats::gpuMs.store(0.0f, std::memory_order_relaxed);
				S.statReal = S.statGen = 0;
				S.statStart = {};
				return false;
			}
			if (!S.initTried)
			{
				S.initTried = true;
				if (!LoadFunctions() || !DetectEsVersionAndExt())
				{
					S.failed = true;
					return false;
				}
			}
			S.multiplier = cfg.multiplier;
			S.baseFpsLimit = cfg.baseFpsLimit;

			uint32_t w = 0, h = 0;
			if (!QuerySurfaceSize(w, h))
				return false;
			const int wantScale = cfg.flowScalePercent;
			if (S.resourcesReady && (w != S.width || h != S.height || std::clamp(wantScale, 25, 100) != S.appliedFlowScale))
			{
				StateGuard guard;
				DestroyResources();
			}
			if (!S.resourcesReady)
			{
				StateGuard guard;
				if (!S.prog[0] && !CreatePrograms())
				{
					cemuLog_log(LogType::Force, "[FrameGenGL] falha ao criar os programas - frame gen GL desativado ate reiniciar o jogo");
					S.failed = true;
					return false;
				}
				if (!CreateResources(w, h, wantScale))
				{
					S.failed = true;
					return false;
				}
			}
			return true;
		}

		void TickStats(bool real)
		{
			const auto now = std::chrono::steady_clock::now();
			if (S.statStart == std::chrono::steady_clock::time_point{})
				S.statStart = now;
			if (real) S.statReal++; else S.statGen++;
			const double el = std::chrono::duration<double>(now - S.statStart).count();
			if (el < 1.0)
				return;
			const float r = (float)(S.statReal / el), g = (float)(S.statGen / el);
			FrameGenStats::active.store(true, std::memory_order_relaxed);
			FrameGenStats::realFps.store(r, std::memory_order_relaxed);
			FrameGenStats::generatedFps.store(g, std::memory_order_relaxed);
			cemuLog_log(LogType::Force, "[FrameGen] FPS real={:.1f} gerado={:.1f} total={:.1f} (OpenGL ES)", r, g, r + g);
			S.statReal = S.statGen = 0;
			S.statStart = now;
		}

		void UploadUbo(GLuint buf, const void* data, size_t size)
		{
			g_fn.glBindBuffer(GL_COPY_WRITE_BUFFER, buf);
			g_fn.glBufferSubData(GL_COPY_WRITE_BUFFER, 0, (GLsizeiptr)size, data);
		}

		void Barrier() { g_fn.glMemoryBarrier(GL_ALL_BARRIER_BITS); }

		void BindTexUnit(int unit, GLuint tex)
		{
			g_fn.glActiveTexture(GL_TEXTURE0 + unit);
			g_fn.glBindSampler(unit, 0);
			g_fn.glBindTexture(GL_TEXTURE_2D, tex);
		}

		void BindImg(int unit, const Tex& t, GLenum fmt, GLenum access)
		{
			g_fn.glBindImageTexture((GLuint)unit, t.id, 0, 0, 0, access, fmt);
		}

		void ZeroTex(const Tex& t, GLenum format, GLenum type)
		{
			static std::vector<uint32_t> zeros(kHistogramElementCount, 0u);
			g_fn.glActiveTexture(GL_TEXTURE0);
			g_fn.glBindTexture(GL_TEXTURE_2D, t.id);
			g_fn.glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, (GLsizei)t.w, (GLsizei)t.h, format, type, zeros.data());
		}

		void RunOpticalFlow(int curIdx, int prevIdx)
		{
			auto& f = g_fn;
			const Tex& inCur = S.useScaled ? S.scaled[curIdx] : S.color[curIdx];
			const Tex& inPrev = S.useScaled ? S.scaled[prevIdx] : S.color[prevIdx];

			OpticalFlowConstants c{};
			c.iInputLumaResolution[0] = (int32_t)S.flowInW;
			c.iInputLumaResolution[1] = (int32_t)S.flowInH;
			c.uOpticalFlowPyramidLevel = 0;
			c.uOpticalFlowPyramidLevelCount = 1;
			c.iFrameIndex = 0;
			c.backbufferTransferFunction = 0;
			c.minMaxLuminance[0] = 0.0f;
			c.minMaxLuminance[1] = 1.0f;
			UploadUbo(S.ubo[0], &c, sizeof(c));
			f.glBindBufferBase(GL_UNIFORM_BUFFER, kUboBinding, S.ubo[0]);

			// zera os acumuladores do SCD a cada frame (o "previous histogram" so no inicio)
			ZeroTex(S.hist, GL_RED_INTEGER, GL_UNSIGNED_INT);
			ZeroTex(S.temp, GL_RED_INTEGER, GL_UNSIGNED_INT);
			ZeroTex(S.scdOut, GL_RED_INTEGER, GL_UNSIGNED_INT);
			if (S.firstFrameHist)
			{
				ZeroTex(S.prevHist, GL_RED, GL_FLOAT);
				S.firstFrameHist = false;
			}
			Barrier();

			// 1) luma do frame atual e do anterior
			const uint32_t lgx = DivRoundUp(DivRoundUp(S.flowInW, 2), 16), lgy = DivRoundUp(DivRoundUp(S.flowInH, 2), 16);
			f.glUseProgram(S.prog[P_PrepareLuma]);
			BindTexUnit(0, inCur.id);
			BindImg(0, S.luma, GL_R32UI, GL_WRITE_ONLY);
			f.glDispatchCompute(lgx, lgy, 1);
			Barrier();
			BindTexUnit(0, inPrev.id);
			BindImg(0, S.lumaPrev, GL_R32UI, GL_WRITE_ONLY);
			f.glDispatchCompute(lgx, lgy, 1);
			Barrier();

			// 2) detector de troca de cena
			const uint32_t strataWidth = (S.flowInW / 4) / kHistogramsPerDim;
			f.glUseProgram(S.prog[P_Histogram]);
			BindTexUnit(0, S.luma.id);
			BindImg(0, S.hist, GL_R32UI, GL_READ_WRITE);
			f.glDispatchCompute(DivRoundUp(strataWidth, 32), 16, kHistogramsPerDim * kHistogramsPerDim);
			Barrier();

			f.glUseProgram(S.prog[P_Divergence]);
			BindImg(0, S.hist, GL_R32UI, GL_READ_WRITE);
			BindImg(1, S.prevHist, GL_R32F, GL_READ_WRITE);
			BindImg(2, S.temp, GL_R32UI, GL_READ_WRITE);
			BindImg(3, S.scdOut, GL_R32UI, GL_READ_WRITE);
			f.glDispatchCompute(kHistogramsPerDim * kHistogramsPerDim, kHistogramShifts, 1);
			Barrier();

			// 3) busca de movimento (nivel unico): luma atual (unit0) e anterior (unit1) -> vetores brutos
			f.glUseProgram(S.prog[P_Search]);
			BindTexUnit(0, S.luma.id);
			BindTexUnit(1, S.lumaPrev.id);
			BindImg(0, S.flowRaw, GL_R32I, GL_READ_WRITE);
			BindImg(1, S.scdOut, GL_R32UI, GL_READ_WRITE);
			f.glDispatchCompute((DivRoundUp(S.flowInW, 4) * 16 + 63) / 64, DivRoundUp(S.flowInH, 16), 1);
			Barrier();

			// 4) filtro -> vetores finais
			f.glUseProgram(S.prog[P_Filter]);
			BindTexUnit(0, S.flowRaw.id);
			BindImg(0, S.flowFinal, GL_R32I, GL_READ_WRITE);
			f.glDispatchCompute(DivRoundUp(S.flowW, 16), DivRoundUp(S.flowH, 4), 1);
			Barrier();
			GlCheck("RunOpticalFlow");
		}

		void RunInterpolation(int curIdx, int prevIdx, float progress)
		{
			auto& f = g_fn;
			InterpConstants c{};
			c.iSize[0] = (int32_t)S.width;
			c.iSize[1] = (int32_t)S.height;
			c.iFlowSize[0] = (int32_t)S.flowW;
			c.iFlowSize[1] = (int32_t)S.flowH;
			c.fFlowSign = 1.0f;
			c.fFlowScale = (float)S.width / (float)S.flowInW;
			c.fT = 1.0f + progress; // v8 extrapola a partir do frame atual
			c.fErrThreshold = 0.12f;
			c.iBlock = (int32_t)kBlockSize;
			UploadUbo(S.ubo[1], &c, sizeof(c));
			f.glBindBufferBase(GL_UNIFORM_BUFFER, kUboBinding, S.ubo[1]);

			f.glUseProgram(S.prog[P_Interp]);
			BindTexUnit(0, S.color[prevIdx].id);
			BindTexUnit(1, S.color[curIdx].id);
			BindTexUnit(2, S.flowFinal.id);
			BindImg(0, S.out, GL_RGBA16F, GL_WRITE_ONLY);
			f.glDispatchCompute(DivRoundUp(S.width, 8), DivRoundUp(S.height, 8), 1);
			Barrier();
			GlCheck("RunInterpolation");
		}

		// desenha S.out no backbuffer (framebuffer 0)
		void DrawOutputToBackbuffer()
		{
			auto& f = g_fn;
			// Preferencia: blit direto S.out -> backbuffer. Copia linha a linha, sem depender da origem do clip
			// (UPPER_LEFT/LOWER_LEFT) nem de gl_FragCoord, entao a orientacao fica igual a do frame real capturado.
			f.glBindFramebuffer(GL_READ_FRAMEBUFFER, S.fboRead);
			f.glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, S.out.id, 0);
			f.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
			if (f.glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE)
			{
				f.glBlitFramebuffer(0, 0, (GLint)S.width, (GLint)S.height, 0, 0, (GLint)S.width, (GLint)S.height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
				f.glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
				GlCheck("DrawOutputToBackbuffer(blit)");
				return;
			}
			f.glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
			// fallback: triangulo de tela cheia
			f.glBindFramebuffer(GL_FRAMEBUFFER, 0);
			f.glViewport(0, 0, (GLsizei)S.width, (GLsizei)S.height);
			f.glUseProgram(S.displayProg);
			BindTexUnit(0, S.out.id);
			f.glBindVertexArray(S.vao);
			f.glDrawArrays(GL_TRIANGLES, 0, 3);
			GlCheck("DrawOutputToBackbuffer");
		}

		// Diagnostico: amostra 3 pixels da captura real e da saida gerada (detecta tela preta: captura ou geracao?)
		void DiagSamples()
		{
			static int s_count = 0;
			const int n = s_count++;
			if (!(n < 4 || n % 600 == 0))
				return;
			auto& f = g_fn;
			const uint32_t xs[3] = { S.width / 4, S.width / 2, (S.width * 3) / 4 };
			const uint32_t ys[3] = { S.height / 4, S.height / 2, (S.height * 3) / 4 };
			auto sampleTex = [&](GLuint tex, bool isFloat, float* outAvg)
			{
				outAvg[0] = outAvg[1] = outAvg[2] = -1.0f;
				f.glBindFramebuffer(GL_READ_FRAMEBUFFER, S.fboRead);
				f.glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
				if (f.glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE)
				{
					float acc[3] = {};
					for (int i = 0; i < 3; i++)
					{
						if (isFloat)
						{
							float px[4] = {};
							f.glReadPixels((GLint)xs[i], (GLint)ys[i], 1, 1, GL_RGBA, GL_FLOAT, px);
							acc[0] += px[0]; acc[1] += px[1]; acc[2] += px[2];
						}
						else
						{
							uint8_t px[4] = {};
							f.glReadPixels((GLint)xs[i], (GLint)ys[i], 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
							acc[0] += px[0] / 255.0f; acc[1] += px[1] / 255.0f; acc[2] += px[2] / 255.0f;
						}
					}
					outAvg[0] = acc[0] / 3.0f; outAvg[1] = acc[1] / 3.0f; outAvg[2] = acc[2] / 3.0f;
				}
				f.glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
			};
			float cur[3], prev[3], out[3];
			sampleTex(S.color[S.curIdx].id, false, cur);
			sampleTex(S.color[1 - S.curIdx].id, false, prev);
			sampleTex(S.out.id, true, out);
			cemuLog_log(LogType::Force, "[FrameGenGL] diag #{}: captura atual=({:.2f},{:.2f},{:.2f}) anterior=({:.2f},{:.2f},{:.2f}) saida=({:.2f},{:.2f},{:.2f}) (-1 = FBO incompleto)",
				n, cur[0], cur[1], cur[2], prev[0], prev[1], prev[2], out[0], out[1], out[2]);
		}

		void ApplyBaseFpsLimit()
		{
			if (S.baseFpsLimit <= 0)
			{
				S.limiterNext = {};
				return;
			}
			const auto period = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(1.0 / (double)S.baseFpsLimit));
			const auto now = std::chrono::steady_clock::now();
			if (S.limiterNext == std::chrono::steady_clock::time_point{})
				S.limiterNext = now;
			S.limiterNext += period;
			if (S.limiterNext > now)
				std::this_thread::sleep_until(S.limiterNext);
			else
				S.limiterNext = now;
		}
	} // namespace

	// ---------------------------------------------------------------------------------------------
	void CaptureRealFrame()
	{
		if (!UpdateActive())
			return;
		TickStats(true);

		// intervalo medio entre frames reais (pacing dos frames gerados)
		{
			const auto now = std::chrono::steady_clock::now();
			if (S.lastCapture != std::chrono::steady_clock::time_point{})
			{
				double sample = std::chrono::duration<double>(now - S.lastCapture).count();
				S.lastRawInterval = sample;
				sample = std::clamp(sample, 1.0 / 240.0, 0.2);
				S.realInterval = S.realInterval * 0.8 + sample * 0.2;
			}
			S.lastCapture = now;
		}

		StateGuard guard;
		auto& f = g_fn;
		// erros pendentes vindos do renderer do Cemu (nao sao nossos): limpa e registra uma vez
		{
			int pending = 0;
			GLenum first = 0, e;
			while ((e = f.glGetError()) != 0 && pending < 16)
			{
				if (!first) first = e;
				pending++;
			}
			static bool s_loggedPending = false;
			if (pending && !s_loggedPending)
			{
				s_loggedPending = true;
				cemuLog_log(LogType::Force, "[FrameGenGL] erro GL pendente do renderer ao capturar: 0x{:x} (x{}) - NAO e do frame gen (ver [GLDEBUG])", (uint32_t)first, pending);
			}
		}
		S.curIdx = 1 - S.curIdx;
		const Tex& dst = S.color[S.curIdx];

		// backbuffer -> textura (copia direta)
		f.glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
		f.glActiveTexture(GL_TEXTURE0);
		f.glBindTexture(GL_TEXTURE_2D, dst.id);
		f.glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, (GLsizei)S.width, (GLsizei)S.height);

		// flow scale < 100%: copia REDUZIDA (blit linear) que alimenta o Optical Flow
		if (S.useScaled)
		{
			f.glBindFramebuffer(GL_READ_FRAMEBUFFER, S.fboRead);
			f.glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dst.id, 0);
			f.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, S.fboDraw);
			f.glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, S.scaled[S.curIdx].id, 0);
			if (f.glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE && f.glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE)
			{
				f.glBlitFramebuffer(0, 0, (GLint)S.width, (GLint)S.height, 0, 0, (GLint)S.flowInW, (GLint)S.flowInH, GL_COLOR_BUFFER_BIT, GL_LINEAR);
			}
			else if (S.logBudget > 0)
			{
				S.logBudget--;
				cemuLog_log(LogType::Force, "[FrameGenGL] FBO incompleto na copia reduzida");
			}
			f.glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
			f.glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
		}
		f.glBindTexture(GL_TEXTURE_2D, 0);
		GlCheck("CaptureRealFrame");

		if (S.curIdx == 1)
			S.hasPrev = true; // depois de 2 capturas ja ha par (anterior, atual)
	}

	void GenerateAfterRealSwap(void (*presentMain)())
	{
		if (S.failed || !S.configWasOn || !S.resourcesReady || !S.hasPrev || !presentMain)
			return;
		if (!FrameGenSettings::Get().enabled)
			return;

		// travada/loading: nao gera
		if (S.lastRawInterval > 0.12)
			return;

		const int mult = std::clamp(S.multiplier, 2, 4);
		const int curIdx = S.curIdx, prevIdx = 1 - S.curIdx;
		for (int k = 1; k < mult; k++)
		{
			const float progress = (float)k / (float)mult;
			// SEM sleep: dormir na thread do Latte atrasava o proximo frame real (FPS real caia ~pela metade).
			// O ritmo vem do proprio eglSwapBuffers (v-sync) do swap do frame gerado.

			{
				StateGuard guard;
				if (k == 1)
					RunOpticalFlow(curIdx, prevIdx);
				RunInterpolation(curIdx, prevIdx, progress);
				if (k == 1)
					DiagSamples();
				DrawOutputToBackbuffer();
			}
			presentMain();
			TickStats(false);
		}
		ApplyBaseFpsLimit();
	}

	void Shutdown()
	{
		if (!g_fnOk)
			return;
		StateGuard guard;
		DestroyResources();
		for (auto& p : S.prog)
		{
			if (p) { g_fn.glDeleteProgram(p); p = 0; }
		}
		if (S.displayProg) { g_fn.glDeleteProgram(S.displayProg); S.displayProg = 0; }
	}
}

#else // !__ANDROID__ : sem frame gen GL

namespace FrameGenGL
{
	void CaptureRealFrame() {}
	void GenerateAfterRealSwap(void (*)()) {}
	void Shutdown() {}
}

#endif
