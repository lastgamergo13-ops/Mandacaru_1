#include "Cafe/HW/Latte/Renderer/OpenGL/RendererShaderGL.h"
#include "Cafe/HW/Latte/Renderer/OpenGL/OpenGLRenderer.h"

#include "Cafe/HW/Latte/Core/LattePerformanceMonitor.h"
#include "Cafe/HW/Latte/Core/LatteShader.h"

#include "Cafe/OS/libs/gx2/GX2.h" // todo - remove dependency

void OpenGLRenderer::streamout_setupXfbBuffer(uint32 bufferIndex, sint32 ringBufferOffset, uint32 rangeAddr, uint32 rangeSize)
{
	if (UseTFViaSSBO())
	{
		// GLES path - no native transform feedback buffer binding at all here; the ring
		// buffer offset is only recorded and later fed to the shader as the
		// uf_streamoutBufferBase{} uniform (see draw_updateStreamoutUniforms() in
		// OpenGLRendererCore.cpp), matching VulkanRenderer::streamout_setupXfbBuffer.
		// The actual GL_SHADER_STORAGE_BUFFER binding of glStreamoutCacheRingBuffer happens
		// once per draw in that same place, since it depends on which shader is bound
		// (resourceMappingGL.tfStorageBindingPoint is per-shader).
		m_streamoutState.buffer[bufferIndex].ringBufferOffset = ringBufferOffset;
		return;
	}
	catchOpenGLError();
	glBindBufferRange(GL_TRANSFORM_FEEDBACK_BUFFER, bufferIndex, glStreamoutCacheRingBuffer, ringBufferOffset, rangeSize);
	catchOpenGLError();
}

void OpenGLRenderer::streamout_begin()
{
	if (UseTFViaSSBO())
	{
		// GLES path - manual SSBO writes happen as an ordinary side effect of the ongoing
		// (rasterizer-discarded, see LatteStreamoutGPU.cpp/OpenGLRendererCore.cpp) draw call
		// itself; there is no separate hardware transform-feedback session to begin, exactly
		// like VulkanRenderer::streamout_begin() (which is also empty for this reason).
		m_isXfbActive = true;
		return;
	}
	// get primitive mode
	GLenum glTransformFeedbackPrimitiveMode;
	auto primitiveMode = LatteGPUState.contextNew.VGT_PRIMITIVE_TYPE.get_PRIMITIVE_MODE();
	if (primitiveMode == Latte::LATTE_VGT_PRIMITIVE_TYPE::E_PRIMITIVE_TYPE::POINTS)
		glTransformFeedbackPrimitiveMode = GL_POINTS;
	else if(primitiveMode == Latte::LATTE_VGT_PRIMITIVE_TYPE::E_PRIMITIVE_TYPE::TRIANGLES)
		glTransformFeedbackPrimitiveMode = GL_POINTS;
	else if (primitiveMode == Latte::LATTE_VGT_PRIMITIVE_TYPE::E_PRIMITIVE_TYPE::QUADS)
		glTransformFeedbackPrimitiveMode = GL_POINTS;
	else
	{
		debug_printf("Unsupported streamout primitive mode 0x%02x\n", primitiveMode);
		cemu_assert_debug(false);
	}
	cemu_assert_debug(m_isXfbActive == false);
	glEnable(GL_RASTERIZER_DISCARD_EXT);
	glBeginTransformFeedback(glTransformFeedbackPrimitiveMode);
	catchOpenGLError();
	m_isXfbActive = true;
}

void OpenGLRenderer::bufferCache_copyStreamoutToMainBuffer(uint32 srcOffset, uint32 dstOffset, uint32 size)
{
	if (glCopyNamedBufferSubData)
		glCopyNamedBufferSubData(glStreamoutCacheRingBuffer, glAttributeCacheAB, srcOffset, dstOffset, size);
	else
	{
		// glCopyNamedBufferSubData has no bind-based legacy equivalent before glCopyBufferSubData
		// (GL 3.1+ / GLES 3.0+, core on both) - that one takes bind targets instead of names.
		GLint prevRead, prevWrite;
		glGetIntegerv(GL_COPY_READ_BUFFER_BINDING, &prevRead);
		glGetIntegerv(GL_COPY_WRITE_BUFFER_BINDING, &prevWrite);
		glBindBuffer(GL_COPY_READ_BUFFER, glStreamoutCacheRingBuffer);
		glBindBuffer(GL_COPY_WRITE_BUFFER, glAttributeCacheAB);
		glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, srcOffset, dstOffset, size);
		glBindBuffer(GL_COPY_READ_BUFFER, prevRead);
		glBindBuffer(GL_COPY_WRITE_BUFFER, prevWrite);
	}
}

void OpenGLRenderer::streamout_rendererFinishDrawcall()
{
	if (m_isXfbActive)
	{
		if (!UseTFViaSSBO())
		{
			glEndTransformFeedback();
			glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, 0);
			glDisable(GL_RASTERIZER_DISCARD_EXT);
		}
		m_isXfbActive = false;
	}

}
