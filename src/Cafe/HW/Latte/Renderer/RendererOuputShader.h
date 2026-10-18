#pragma once

#include "Cafe/HW/Latte/Renderer/RendererShader.h"
#include "util/math/vector2.h"

#include "Cafe/HW/Latte/Core/LatteTexture.h"

class RendererOutputShader
{
public:
	struct OutputUniformVariables
	{
		Vector2f textureSrcResolution;
		Vector2f nativeResolution;
		Vector2f outputResolution;
		uint32 applySRGBEncoding;
		float targetGamma;
		float displayGamma;
		// FSR RCAS sharpening amount, in "stops" (0.0 = sharpest, higher = softer) - see
		// FsrRcasCon() in AMD's ffx_fsr1.h. Unused by every other filter; a fixed default is
		// used for now rather than a user-facing slider - see s_fsr_rcas_shader_source.
		float fsrRcasSharpness;
	};
	enum Shader
	{
		kCopy,
		kBicubic,
		kHermit,
	};
	RendererOutputShader(const std::string& vertex_source, const std::string& fragment_source);
	virtual ~RendererOutputShader() = default;

	OutputUniformVariables FillUniformBlockBuffer(const LatteTextureView& texture_view, const Vector2i& output_res, const bool padView) const;

	RendererShader* GetVertexShader() const
	{
		return m_vertex_shader.get();
	}

	RendererShader* GetFragmentShader() const
	{
		return m_fragment_shader.get();
	}

	static void InitializeStatic();
	static void ShutdownStatic();

	static RendererOutputShader* s_copy_shader;
	static RendererOutputShader* s_copy_shader_ud;

	static RendererOutputShader* s_bicubic_shader;
	static RendererOutputShader* s_bicubic_shader_ud;

	static RendererOutputShader* s_hermit_shader;
	static RendererOutputShader* s_hermit_shader_ud;

	// AMD FidelityFX Super Resolution 1.0 (EASU edge-adaptive upsample + RCAS sharpen),
	// ported from the official ffx_fsr1.h/ffx_a.h reference as two chained fragment-shader
	// passes reusing this same RendererOutputShader/DrawBackbufferQuad machinery - see
	// Renderer::DrawBackbufferQuadFSR() for how the two passes and the intermediate texture
	// between them are driven, and RendererOuputShader.cpp for the shader source itself.
	static RendererOutputShader* s_fsr_easu_shader;
	static RendererOutputShader* s_fsr_rcas_shader;
	static RendererOutputShader* s_fsr_rcas_shader_ud;

	static std::string GetOpenGlVertexSource(bool render_upside_down);
	static std::string GetVulkanVertexSource(bool render_upside_down);
	static std::string GetMetalVertexSource(bool render_upside_down);

	static std::string PrependFragmentPreamble(const std::string& shaderSrc);
	static std::string PrependFragmentPreambleImpl(const std::string& shaderSrc);

protected:
	std::unique_ptr<RendererShader> m_vertex_shader;
	std::unique_ptr<RendererShader> m_fragment_shader;


private:
	static const std::string s_copy_shader_source;
	static const std::string s_bicubic_shader_source;
	static const std::string s_hermite_shader_source;

	static const std::string s_fsr_easu_shader_source;
	static const std::string s_fsr_rcas_shader_source;

	static const std::string s_bicubic_shader_source_vk;
	static const std::string s_hermite_shader_source_vk;

	static const std::string s_copy_shader_source_mtl;
	static const std::string s_bicubic_shader_source_mtl;
	static const std::string s_hermite_shader_source_mtl;
};
