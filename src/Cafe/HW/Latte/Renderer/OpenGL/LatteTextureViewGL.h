#pragma once

#include "Cafe/HW/Latte/Core/LatteTexture.h"
#include "Common/GLInclude/GLInclude.h"

class LatteTextureViewGL : public LatteTextureView
{
public:
	LatteTextureViewGL(class LatteTextureGL* texture, Latte::E_DIM dim, Latte::E_GX2SURFFMT format, sint32 firstMip, sint32 mipCount, sint32 firstSlice, sint32 sliceCount, bool registerView = true, bool forceCreateNewTexId = false);
	~LatteTextureViewGL();

	LatteTextureViewGL* GetAlternativeView();

	// GL specific states
	LatteSamplerState samplerState{};
	uint8 swizzleR;
	uint8 swizzleG;
	uint8 swizzleB;
	uint8 swizzleA;

	GLuint glTexId;
	GLint glTexTarget;
	sint32 glInternalFormat;

	LatteTextureViewGL* m_alternativeView{ nullptr };

	// "View falsa": drivers sem glTextureView (PowerVR do Moto G54, entre outros) recebem uma CPIA da textura base feita na criacao da
	// view. Antes essa copia nunca era atualizada: se o jogo renderizava/atualizava a textura base depois, a view mostrava conteudo
	// velho ou vazio (cores erradas/escuras em Mario Kart 8 e Mario 3D World). Agora a copia e refeita sempre que a textura base
	// mudou, antes da view ser usada num draw. Chamado em OpenGLRenderer::texture_setLatteTexture/texture_bindAndActivate.
	void RefreshFakeViewIfNeeded();
	bool m_isFakeCopyView{ false };
private:
	void CopyFakeViewContent();
	struct { GLuint srcMinLevel; sint32 numMip; sint32 firstSlice; sint32 numSlice; } m_fakeParams{};
	uint64 m_fakeStampWrite{ ~0ull }, m_fakeStampUpdate{ ~0ull };
	uint32 m_fakeStampData{ ~0u };
	void InitAliasView();
};
