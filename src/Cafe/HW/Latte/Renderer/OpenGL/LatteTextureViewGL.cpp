#include "Cafe/HW/Latte/Renderer/OpenGL/LatteTextureViewGL.h"
#include "Cafe/HW/Latte/Renderer/OpenGL/LatteTextureGL.h"
#include "Cafe/HW/Latte/Renderer/Renderer.h"
#include "Cafe/HW/Latte/Renderer/OpenGL/OpenGLRenderer.h"
#include "config/LaunchSettings.h"
#include <algorithm>

LatteTextureViewGL::LatteTextureViewGL(LatteTextureGL* texture, Latte::E_DIM dim, Latte::E_GX2SURFFMT format, sint32 firstMip, sint32 mipCount, sint32 firstSlice, sint32 sliceCount, bool registerView, bool forceCreateNewTexId)
	: LatteTextureView(texture, firstMip, mipCount, firstSlice, sliceCount, dim, format, registerView)
{
	if (dim != texture->dim || format != texture->format ||
		firstSlice != 0 || firstMip != 0 || mipCount != texture->mipLevels || sliceCount != texture->depth ||	
		forceCreateNewTexId)
	{
		LatteTextureGL::GenerateEmptyTextureFromGX2Dim(dim, glTexId, glTexTarget, false);
		this->glInternalFormat = 0;
		InitAliasView();
	}
	else
	{
		glTexId = texture->glId_texture;
		glTexTarget = texture->glTexTarget;
		glInternalFormat = texture->glInternalFormat;
	}
	// mark all sampler properties as undefined
	samplerState.maxAniso = 0xFF;
	samplerState.filterMin = 0xFFFFFFFF;
	samplerState.filterMag = 0xFFFFFFFF;
	samplerState.maxMipLevels = 0xFF;
	samplerState.borderType = 0xFF;
	samplerState.borderColor[0] = 9999.0f;
	samplerState.borderColor[1] = 9999.0f;
	samplerState.borderColor[2] = 9999.0f;
	samplerState.borderColor[3] = 9999.0f;
	samplerState.clampS = 0xFF;
	samplerState.clampT = 0xFF;
	samplerState.clampR = 0xFF;
	samplerState.minLod = 0xFFFF;
	samplerState.maxLod = 0xFFFF;
	samplerState.lodBias = 0x7FFF;
	samplerState.depthCompareMode = 0xFF;
	samplerState.depthCompareFunc = 0xFF;
	swizzleR = 0xFF;
	swizzleG = 0xFF;
	swizzleB = 0xFF;
	swizzleA = 0xFF;
}

// views falsas com escrita da GPU ainda nao devolvida a textura base (so a thread da GPU mexe nisto)
static std::vector<LatteTextureViewGL*> s_pendingFakeViews;

LatteTextureViewGL::~LatteTextureViewGL()
{
	if (!m_fakeDirty.empty())
	{
		auto it = std::find(s_pendingFakeViews.begin(), s_pendingFakeViews.end(), this);
		if (it != s_pendingFakeViews.end())
			s_pendingFakeViews.erase(it);
	}
	delete m_alternativeView;
	((OpenGLRenderer*)g_renderer.get())->texture_notifyDelete(this);
	glDeleteTextures(1, &glTexId);
}

void LatteTextureViewGL::InitAliasView()
{
	const auto texture = (LatteTextureGL*)baseTexture;
	// compute internal format
	if(texture->overwriteInfo.hasFormatOverwrite)
	{
		cemu_assert_debug(format == texture->format);
		glInternalFormat = texture->glInternalFormat; // for format overwrite no aliasing is allowed and thus we always inherit the internal format of the base texture
	}
	else if (baseTexture->isDepth)
	{
		// depth is handled differently
		cemu_assert(format == texture->format); // is depth alias with different format intended?
		glInternalFormat = texture->glInternalFormat;
	}
	else
	{
		LatteTextureGL::FormatInfoGL glFormatInfo;
		LatteTextureGL::GetOpenGLFormatInfo(baseTexture->isDepth, format, dim, &glFormatInfo);
		glInternalFormat = glFormatInfo.glInternalFormat;
	}

	catchOpenGLError();
	GLuint srcMinLevel = (firstMip >= texture->maxPossibleMipLevels) ? (texture->maxPossibleMipLevels - 1) : firstMip;
	if (firstMip >= texture->maxPossibleMipLevels)
		cemuLog_logDebug(LogType::Force, "InitAliasView(): Out of bounds mip level requested");

	bool hasRealView = glTextureViewWrapper(glTexId, glTexTarget, texture->glId_texture, glInternalFormat, srcMinLevel, numMip, firstSlice, numSlice);
	if (!hasRealView)
	{
		// No glTextureView/OES/EXT on this device (see the wrapper's comment in
		// GLInclude.h) - fall back to a one-time GPU-side copy of the requested mip/slice
		// range into the texture object InitAliasView's caller already allocated for us.
		// glTexId was created via GenerateEmptyTextureFromGX2Dim just above and still needs
		// real backing storage before glCopyImageSubData can target it.
		sint32 viewWidth = std::max(1, texture->width >> srcMinLevel);
		sint32 viewHeight = std::max(1, texture->height >> srcMinLevel);
		LatteTextureGL::AllocateStorage(glTexId, dim, glInternalFormat, viewWidth, viewHeight, numSlice, numMip);
		m_isFakeCopyView = true;
		m_fakeParams = { srcMinLevel, (sint32)numMip, (sint32)firstSlice, (sint32)numSlice };
		CopyFakeViewContent();
	}

	catchOpenGLError();

	if (glTextureParameteri)
	{
		glTextureParameteri(glTexId, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTextureParameteri(glTexId, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTextureParameteri(glTexId, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTextureParameteri(glTexId, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTextureParameteri(glTexId, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
		glTextureParameteri(glTexId, GL_TEXTURE_COMPARE_MODE, GL_NONE);
	}
	else
	{
		// todo - fallback for when DSA isn't supported
	}

	// set debug name
	bool useGLDebugNames = false;
#ifdef CEMU_DEBUG_ASSERT
	useGLDebugNames = true;
#endif
	if (LaunchSettings::NSightModeEnabled())
		useGLDebugNames = true;
	if (useGLDebugNames)
	{
		char textureDebugLabel[512];
		sprintf(textureDebugLabel, "%08x_f%04x_p%04x_viewFMT%04x%s_org%d", baseTexture->physAddress, (uint32)baseTexture->format, baseTexture->pitch, (uint32)this->format, baseTexture->isDepth?"_d":"", texture->glId_texture);
		glObjectLabel(GL_TEXTURE, glTexId, -1, textureDebugLabel);
	}
}

LatteTextureViewGL* LatteTextureViewGL::GetAlternativeView()
{
	if (!m_alternativeView)
		m_alternativeView = new LatteTextureViewGL((LatteTextureGL*)baseTexture, dim, format, firstMip, numMip, firstSlice, numSlice, false, true);
	return m_alternativeView;
}

// Copia (de novo) o conteudo da textura base para a view falsa. Ver o comentario em LatteTextureViewGL.h.
void LatteTextureViewGL::CopyFakeViewContent()
{
	const auto texture = (LatteTextureGL*)baseTexture;
	const sint32 viewWidth = std::max(1, texture->width >> m_fakeParams.srcMinLevel);
	const sint32 viewHeight = std::max(1, texture->height >> m_fakeParams.srcMinLevel);
	for (sint32 i = 0; i < m_fakeParams.numMip; i++)
	{
		uint32 mipWidth = std::max(1, viewWidth >> i);
		uint32 mipHeight = std::max(1, viewHeight >> i);
		if (!glCopyImageSubDataWrapper(texture->glId_texture, texture->glTexTarget, m_fakeParams.srcMinLevel + i, 0, 0, m_fakeParams.firstSlice,
				glTexId, glTexTarget, i, 0, 0, 0,
				mipWidth, mipHeight, m_fakeParams.numSlice))
		{
			cemuLog_log(LogType::Force, "LatteTextureViewGL: no texture view mechanism (glTextureView/OES/EXT/glCopyImageSubData) available on this GPU driver - this view will be blank");
			break;
		}
	}
	m_fakeStampWrite = texture->lastWriteEventCounter;
	m_fakeStampUpdate = texture->lastUpdateEventCounter;
	m_fakeStampData = texture->lastDataUpdateFrameCounter;
}

void LatteTextureViewGL::RefreshFakeViewIfNeeded()
{
	// qualquer view (real ou falsa) que va ser lida: antes devolve a base o que views falsas dela desenharam
	FlushPendingFakeWritebacks(baseTexture);
	if (!m_isFakeCopyView)
		return;
	const auto texture = (LatteTextureGL*)baseTexture;
	if (m_fakeStampWrite == texture->lastWriteEventCounter && m_fakeStampUpdate == texture->lastUpdateEventCounter && m_fakeStampData == texture->lastDataUpdateFrameCounter)
		return; // a textura base nao mudou desde a ultima copia
	CopyFakeViewContent();
}

// A GPU escreveu (desenho/clear/copia) neste slice/mip da view. Numa view falsa o desenho foi para a COPIA, entao ela passa a ser a versao mais nova
// desse pedaco: guarda o pedaco como sujo e alinha os carimbos com a base (que o motor acabou de marcar como escrita) para que
// RefreshFakeViewIfNeeded() NAO a sobrescreva com o conteudo velho da base.
void LatteTextureViewGL::NotifyContentWrittenByGPU(sint32 relSlice, sint32 relMip)
{
	if (!m_isFakeCopyView)
		return;
	const uint32 key = ((uint32)relSlice << 8) | ((uint32)relMip & 0xFF);
	if (std::find(m_fakeDirty.begin(), m_fakeDirty.end(), key) == m_fakeDirty.end())
		m_fakeDirty.push_back(key);
	if (std::find(s_pendingFakeViews.begin(), s_pendingFakeViews.end(), this) == s_pendingFakeViews.end())
		s_pendingFakeViews.push_back(this);
	const auto texture = (LatteTextureGL*)baseTexture;
	m_fakeStampWrite = texture->lastWriteEventCounter;
	m_fakeStampUpdate = texture->lastUpdateEventCounter;
	m_fakeStampData = texture->lastDataUpdateFrameCounter;
}

// Copia os pedacos sujos da view falsa de volta para a textura base.
void LatteTextureViewGL::WriteBackFakeViewToBase()
{
	const auto texture = (LatteTextureGL*)baseTexture;
	const sint32 viewWidth = std::max(1, texture->width >> m_fakeParams.srcMinLevel);
	const sint32 viewHeight = std::max(1, texture->height >> m_fakeParams.srcMinLevel);
	for (const uint32 key : m_fakeDirty)
	{
		const sint32 relSlice = (sint32)(key >> 8);
		const sint32 relMip = (sint32)(key & 0xFF);
		const uint32 mipWidth = std::max(1, viewWidth >> relMip);
		const uint32 mipHeight = std::max(1, viewHeight >> relMip);
		if (!glCopyImageSubDataWrapper(glTexId, glTexTarget, relMip, 0, 0, relSlice,
				texture->glId_texture, texture->glTexTarget, m_fakeParams.srcMinLevel + relMip, 0, 0, m_fakeParams.firstSlice + relSlice,
				mipWidth, mipHeight, 1))
		{
			static bool s_warned = false;
			if (!s_warned)
			{
				s_warned = true;
				cemuLog_log(LogType::Force, "[GL] view falsa: nao foi possivel devolver o desenho a textura base (glCopyImageSubData falhou; formatos incompativeis?)");
			}
			break;
		}
	}
	m_fakeDirty.clear();
}

// Grava de volta todas as views falsas sujas de 'base'. Depois avanca o contador da base (as OUTRAS views falsas se atualizam na proxima leitura)
// e realinha os carimbos das views gravadas (elas ja tem o conteudo certo).
void LatteTextureViewGL::FlushPendingFakeWritebacks(LatteTexture* base)
{
	if (s_pendingFakeViews.empty())
		return;
	std::vector<LatteTextureViewGL*> flushed;
	for (size_t i = 0; i < s_pendingFakeViews.size();)
	{
		LatteTextureViewGL* v = s_pendingFakeViews[i];
		if (v->baseTexture != base)
		{
			i++;
			continue;
		}
		v->WriteBackFakeViewToBase();
		s_pendingFakeViews.erase(s_pendingFakeViews.begin() + i);
		flushed.push_back(v);
	}
	if (flushed.empty())
		return;
	base->lastWriteEventCounter = LatteTexture_getNextUpdateEventCounter();
	for (LatteTextureViewGL* v : flushed)
	{
		v->m_fakeStampWrite = base->lastWriteEventCounter;
		v->m_fakeStampUpdate = base->lastUpdateEventCounter;
		v->m_fakeStampData = base->lastDataUpdateFrameCounter;
	}
}
