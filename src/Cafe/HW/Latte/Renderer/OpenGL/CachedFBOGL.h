#pragma once
#include <atomic>
#include "Cafe/HW/Latte/Renderer/OpenGL/LatteTextureViewGL.h"
#include "Cafe/HW/Latte/Core/LatteCachedFBO.h"
#include "Common/GLInclude/GLInclude.h"

class CachedFBOGL : public LatteCachedFBO
{
public:
	CachedFBOGL(uint64 key)
		: LatteCachedFBO(key)
	{
		// generate framebuffer
		if (glCreateFramebuffers && false)
			glCreateFramebuffers(1, &glId_fbo);
		else
			glGenFramebuffers(1, &glId_fbo);
		// bind framebuffer
		((OpenGLRenderer*)g_renderer.get())->rendertarget_bindFramebufferObject(this);
		// V11.64 (informativo): sem glTextureView (ex.: PowerVR) as views de alias sao COPIAS. O desenho feito numa copia e devolvido a textura base pela
		// emulacao bidirecional (LatteTextureViewGL::NotifyContentWrittenByGPU / FlushPendingFakeWritebacks). Registra, no maximo 12 vezes, quando isso acontece.
		auto logFakeViewTarget = [](LatteTextureViewGL* v, const char* kind, sint32 index)
		{
			static std::atomic<int> s_logged{ 0 };
			if (!v || !v->m_isFakeCopyView || s_logged.load() >= 12)
				return;
			if (s_logged.fetch_add(1) >= 12)
				return;
			cemuLog_log(LogType::Force, "[GL] view FALSA (copia, driver sem texture_view) usada como alvo de renderizacao ({} {}): fmt=0x{:04x} dim={} {}x{} mip={} slice={} - emulacao bidirecional ativa: o desenho e devolvido a textura base antes de ela ser lida",
				kind, index, (uint32)v->baseTexture->format, (uint32)v->dim, v->baseTexture->width, v->baseTexture->height, (uint32)v->firstMip, (uint32)v->firstSlice);
		};
		// setup framebuffer
		for (sint32 i = 0; i < 8; i++)
		{
			LatteTextureViewGL* colorTexViewGL = (LatteTextureViewGL*)colorBuffer[i].texture;
			logFakeViewTarget(colorTexViewGL, "cor", i);
			if (!colorTexViewGL)
				glFramebufferTexture2D(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT + i, GL_TEXTURE_2D, 0, 0);
			else if (colorTexViewGL->dim == Latte::E_DIM::DIM_2D)
				glFramebufferTexture2D(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT + i, GL_TEXTURE_2D, colorTexViewGL->glTexId, 0);
			else if (colorTexViewGL->dim == Latte::E_DIM::DIM_3D)
				glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0_EXT + i, colorTexViewGL->glTexId, 0, 0);
			else if (colorTexViewGL->dim == Latte::E_DIM::DIM_2D_ARRAY)
				glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0_EXT + i, colorTexViewGL->glTexId, 0, 0);
			else if (colorTexViewGL->dim == Latte::E_DIM::DIM_CUBEMAP)
				glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0_EXT + i, colorTexViewGL->glTexId, 0, 0);
			else
			{
				cemu_assert_suspicious();
			}
		}

		if (depthBuffer.texture)
		{
			LatteTextureViewGL* depthTexViewGL = (LatteTextureViewGL*)depthBuffer.texture;
			logFakeViewTarget(depthTexViewGL, "profundidade", 0);
			if (depthTexViewGL->dim == Latte::E_DIM::DIM_2D)
				glFramebufferTexture2D(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTexViewGL->glTexId, 0);
			else if (depthTexViewGL->dim == Latte::E_DIM::DIM_2D_ARRAY)
				glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, depthTexViewGL->glTexId, 0, 0);
			else
				cemu_assert_suspicious();
			if (depthBuffer.hasStencil)
			{
				if (depthTexViewGL->dim == Latte::E_DIM::DIM_2D)
					glFramebufferTexture2D(GL_FRAMEBUFFER_EXT, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, depthTexViewGL->glTexId, 0);
				else if (depthTexViewGL->dim == Latte::E_DIM::DIM_2D_ARRAY)
					glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, depthTexViewGL->glTexId, 0, 0);
				else
					cemu_assert_suspicious();
			}
			else
			{
				glFramebufferTexture2D(GL_FRAMEBUFFER_EXT, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
			}
		}
		else
		{
			glFramebufferTexture2D(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
			glFramebufferTexture2D(GL_FRAMEBUFFER_EXT, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
		}
		SetDrawBuffers();
	}

private:
	void SetDrawBuffers()
	{
		GLenum buffers[8];
		uint32 bufferCount = 0;
		for (uint32 i = 0; i < 8; i++)
		{
			if (colorBuffer[i].texture)
			{
				buffers[bufferCount] = GL_COLOR_ATTACHMENT0_EXT + i;
				bufferCount++;
			}
			else
			{
				// pad with GL_NONE entries to make sure that draw buffer indices match up with color attachment indices
				buffers[bufferCount] = GL_NONE;
				bufferCount++;
			}
		}
		// trim trailing GL_NONE
		while (bufferCount > 0 && buffers[bufferCount - 1] == GL_NONE)
			bufferCount--;

		if (bufferCount == 0)
		{
			// glDrawBuffer (singular) has no core GLES equivalent - glDrawBuffers (plural)
			// exists on both desktop GL and GLES 3.0+ and is used unconditionally here now.
			GLenum none = GL_NONE;
			glDrawBuffers(1, &none);
		}
		else
			glDrawBuffers(bufferCount, buffers);
	}

public:
	GLuint glId_fbo{};
};
