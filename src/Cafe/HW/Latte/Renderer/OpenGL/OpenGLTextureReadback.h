#pragma once
#include "Cafe/HW/Latte/Core/LatteTextureReadbackInfo.h"
#include "Common/GLInclude/GLInclude.h"

class LatteTextureReadbackInfoGL : public LatteTextureReadbackInfo
{
public:
	LatteTextureReadbackInfoGL(LatteTextureView* textureView);

	~LatteTextureReadbackInfoGL();

	void StartTransfer() override;
	bool IsFinished() override;

	uint8* GetData() override;
	void ReleaseData() override;

private:
	GLuint m_texFormatGL;
	GLuint m_texDataTypeGL;

	// buffer
	GLuint texImageBufferGL = 0;
	// sync
	GLsync imageCopyFinSync = nullptr;
	// scratch FBO used to read the source texture through glReadPixels instead of
	// glGetTexImage, which does not exist on GLES - see StartTransfer() in the .cpp.
	// Created lazily on first use, destroyed in the destructor.
	GLuint m_readbackFBO = 0;
};

