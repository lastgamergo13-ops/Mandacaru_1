#include <vector>
#include "Cemu/Logging/CemuLogging.h"
#include "Cafe/HW/Latte/Renderer/Renderer.h"
#include "Cafe/HW/Latte/Renderer/OpenGL/OpenGLRenderer.h"
#include "Cafe/HW/Latte/Renderer/OpenGL/OpenGLTextureReadback.h"
#include "Cafe/HW/Latte/Renderer/OpenGL/LatteTextureViewGL.h"

LatteTextureReadbackInfoGL::LatteTextureReadbackInfoGL(LatteTextureView* textureView)
	: LatteTextureReadbackInfo(textureView)
{
	LatteTexture* baseTexture = textureView->baseTexture;
	// handle format
	if (textureView->format == Latte::E_GX2SURFFMT::R8_G8_B8_A8_UNORM)
	{
		m_image_size = baseTexture->width*baseTexture->height * 4;
		m_texFormatGL = GL_RGBA;
		m_texDataTypeGL = GL_UNSIGNED_BYTE;
	}
	else if (textureView->format == Latte::E_GX2SURFFMT::R8_G8_B8_A8_SRGB)
	{
		m_image_size = baseTexture->width*baseTexture->height * 4;
		m_texFormatGL = GL_RGBA;
		m_texDataTypeGL = GL_UNSIGNED_BYTE;
	}
	else if (textureView->format == Latte::E_GX2SURFFMT::R32_G32_B32_A32_FLOAT)
	{
		m_image_size = baseTexture->width*baseTexture->height * 16;
		m_texFormatGL = GL_RGBA;
		m_texDataTypeGL = GL_FLOAT;
	}
	else if (textureView->format == Latte::E_GX2SURFFMT::R32_FLOAT)
	{
		if (baseTexture->isDepth)
		{
			m_image_size = baseTexture->width*baseTexture->height * 4;
			m_texFormatGL = GL_DEPTH_COMPONENT;
			m_texDataTypeGL = GL_FLOAT;
		}
		else
		{
			m_image_size = baseTexture->width*baseTexture->height * 4;
			m_texFormatGL = GL_RED;
			m_texDataTypeGL = GL_FLOAT;
		}
	}
	else if (textureView->format == Latte::E_GX2SURFFMT::R16_UNORM)
	{
		if (baseTexture->isDepth)
		{
			m_image_size = baseTexture->width*baseTexture->height * 2;
			m_texFormatGL = GL_DEPTH_COMPONENT;
			m_texDataTypeGL = GL_UNSIGNED_SHORT;
			cemu_assert_unimplemented();
		}
		else
		{
			m_image_size = baseTexture->width*baseTexture->height * 2;
			m_texFormatGL = GL_RED;
			m_texDataTypeGL = GL_UNSIGNED_SHORT;
		}
	}
	else if (textureView->format == Latte::E_GX2SURFFMT::R16_G16_B16_A16_FLOAT)
	{
		m_image_size = baseTexture->width*baseTexture->height * 8;
		m_texFormatGL = GL_RGBA;
		m_texDataTypeGL = GL_HALF_FLOAT;
	}
	else if (textureView->format == Latte::E_GX2SURFFMT::R8_G8_UNORM)
	{
		m_image_size = baseTexture->width*baseTexture->height * 2;
		m_texFormatGL = GL_RG;
		m_texDataTypeGL = GL_UNSIGNED_BYTE;
	}
	else if (textureView->format == Latte::E_GX2SURFFMT::R16_G16_B16_A16_UNORM)
	{
		m_image_size = baseTexture->width*baseTexture->height * 8;
		m_texFormatGL = GL_RGBA;
		m_texDataTypeGL = GL_UNSIGNED_SHORT;
	}
	else
	{
		cemuLog_logDebug(LogType::Force, "Unsupported texture readback format {:04x}", (uint32)textureView->format);
		return;
	}
}

LatteTextureReadbackInfoGL::~LatteTextureReadbackInfoGL()
{
	if(imageCopyFinSync != 0)
		glDeleteSync(imageCopyFinSync);

	if(texImageBufferGL)
		glDeleteBuffers(1, &texImageBufferGL);

	if (m_readbackFBO)
		glDeleteFramebuffers(1, &m_readbackFBO);
}

// Reads the source texture back via a temporary FBO + glReadPixels instead of glGetTexImage,
// which has no equivalent anywhere in the GLES line (core or extension) - see Etapa 5 of the
// OpenGL-on-Android technical report. glReadPixels + a bound GL_PIXEL_PACK_BUFFER works
// identically to the old glGetTexImage-into-PBO approach on both desktop GL and GLES 3.0+,
// so this same path is now used unconditionally on both, rather than branching by platform.
//
// Honest caveat: reading a depth attachment via glReadPixels(GL_DEPTH_COMPONENT, ...) is
// allowed by the GLES 3.x spec only when the implementation exposes it (it is not one of the
// two combinations every implementation is required to support), so depth readback here may
// legitimately fail/return garbage on some GLES drivers even though the exact same call
// always worked on desktop GL. There is no portable alternative within core GLES 3.x for
// this - it would require a driver-specific extension query.
void LatteTextureReadbackInfoGL::StartTransfer()
{
	cemu_assert(m_textureView);
	auto textureViewGL = (LatteTextureViewGL*)m_textureView;
	LatteTexture* baseTexture = m_textureView->baseTexture;
	// V11.60: guarda aqui o tamanho do buffer zerado de emergencia (m_textureView vira nullptr no fim desta funcao)
	m_fallbackBytes = (size_t)baseTexture->width * (size_t)baseTexture->height * 16;
	if (m_fallbackBytes < (size_t)m_image_size)
		m_fallbackBytes = (size_t)m_image_size;
	if (m_fallbackBytes < 4096)
		m_fallbackBytes = 4096;

	if (!m_readbackFBO)
		glGenFramebuffers(1, &m_readbackFBO);

	GLint previousReadFBO = 0;
	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFBO);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, m_readbackFBO);

	GLenum attachment = (m_texFormatGL == GL_DEPTH_COMPONENT) ? GL_DEPTH_ATTACHMENT : GL_COLOR_ATTACHMENT0;
	if (textureViewGL->glTexTarget == GL_TEXTURE_2D)
		glFramebufferTexture2D(GL_READ_FRAMEBUFFER, attachment, GL_TEXTURE_2D, textureViewGL->glTexId, m_textureView->firstMip);
	else
		glFramebufferTextureLayer(GL_READ_FRAMEBUFFER, attachment, textureViewGL->glTexId, m_textureView->firstMip, m_textureView->firstSlice);
	if (attachment == GL_COLOR_ATTACHMENT0)
		glReadBuffer(GL_COLOR_ATTACHMENT0);
	else
		glReadBuffer(GL_NONE);

	// create unsynchronized buffer
	glGenBuffers(1, &texImageBufferGL);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, texImageBufferGL);
	glBufferData(GL_PIXEL_PACK_BUFFER, m_image_size, NULL, GL_DYNAMIC_READ);
	// request texture read into buffer (pixels=nullptr means "into the bound PBO", same as before)
	GLint previousPackAlignment = 4;
	glGetIntegerv(GL_PACK_ALIGNMENT, &previousPackAlignment);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, baseTexture->width, baseTexture->height, m_texFormatGL, m_texDataTypeGL, nullptr);
	glPixelStorei(GL_PACK_ALIGNMENT, previousPackAlignment);
	glFlush();

	glBindFramebuffer(GL_READ_FRAMEBUFFER, previousReadFBO);

	// create fence sync (so we can check if the image copy operation finished)
	imageCopyFinSync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
	m_textureView = nullptr;
}

bool LatteTextureReadbackInfoGL::IsFinished()
{
	GLenum status = glClientWaitSync(imageCopyFinSync, 0, 0);
		if (status == GL_TIMEOUT_EXPIRED)
			return false;
		else if (status == GL_ALREADY_SIGNALED || status == GL_SIGNALED || status == GL_CONDITION_SATISFIED)
			return true; // V11.47: GL_CONDITION_SATISFIED (0x911c) e a resposta normal da Adreno e antes derrubava o app (Zelda BOTW)
		else
		{
			// V11.47: GL_WAIT_FAILED (contexto perdido / driver recusou o fence) derrubava o app inteiro com
			// std::runtime_error (crash do Zelda BOTW). Agora so avisa uma vez e considera a transferencia terminada.
			static bool s_warned = false;
			if (!s_warned)
			{
				s_warned = true;
				cemuLog_log(LogType::Force, "[GL] glClientWaitSync do readback falhou (status=0x{:x}); tratando como concluido em vez de encerrar o app", (uint32)status);
			}
			return true;
		}
}

static thread_local std::vector<uint8> s_zeroFallback;
static thread_local bool s_usingZeroFallback = false;

uint8* LatteTextureReadbackInfoGL::GetData()
{
		glBindBuffer(GL_PIXEL_PACK_BUFFER, texImageBufferGL);
		// glMapBuffer doesn't exist on GLES (only glMapBufferRange, GLES 3.0+) - desktop GL
		// has supported glMapBufferRange since 3.0 too, so this now always uses it instead of
		// branching, same reasoning as the StartTransfer() unification above.
		uint8* mapped = (uint8*)glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, m_image_size, GL_MAP_READ_BIT);
		if (!mapped)
		{
			// V11.47: o mapeamento falhou (formato nao suportado no readback / buffer vazio). Devolver nullptr causava
			// signal 11 em LatteTextureLoader_writeReadbackTextureToMemory (Zelda BOTW); devolve zeros no lugar.
			static bool s_warned = false;
			if (!s_warned)
			{
				s_warned = true;
				const GLenum glErr = glGetError();
				cemuLog_log(LogType::Force, "[GL] glMapBufferRange do readback devolveu nulo (tamanho={}, buffer={}, glGetError=0x{:x}); usando buffer zerado", (uint32)m_image_size, (uint32)texImageBufferGL, (uint32)glErr);
			}
			// V11.60: NAO usar m_textureView aqui: StartTransfer() ja o zerou (acesso em nulo+8 = signal 11 no Zelda, GetData()+0xc0)
			size_t need = m_fallbackBytes ? m_fallbackBytes : (size_t)((m_image_size > 4096) ? m_image_size : 4096);
			s_zeroFallback.assign(need, 0);
			s_usingZeroFallback = true;
			return s_zeroFallback.data();
		}
		s_usingZeroFallback = false;
		return mapped;
}



void LatteTextureReadbackInfoGL::ReleaseData()
{
	if (s_usingZeroFallback)
	{
		s_usingZeroFallback = false;
		return;
	}
	glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
}
