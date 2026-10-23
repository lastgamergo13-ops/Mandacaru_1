#pragma once
#include "Common/GLInclude/GLInclude.h"

#include "Cafe/HW/Latte/Renderer/Renderer.h"
#include "Cafe/HW/Latte/Renderer/RendererOuputShader.h"

#define GPU_GL_MAX_NUM_ATTRIBUTE		(16) // Wii U GPU supports more than 16 but not all desktop GPUs do. Have to keep this at 16 until we find a better solution

class OpenGLCanvasCallbacks
{
  public:
	virtual bool HasPadViewOpen() const
	{
		return false;
	}
	virtual bool MakeCurrent(bool padView)
	{
		return false;
	}
	virtual void SwapBuffers(bool swapTV, bool swapDRC) {}
	virtual ~OpenGLCanvasCallbacks() = default;
};

void SetOpenGLCanvasCallbacks(OpenGLCanvasCallbacks* callbacks);
void ClearOpenGLCanvasCallbacks();

bool GLCanvas_HasPadViewOpen();
bool GLCanvas_MakeCurrent(bool padView);
void GLCanvas_SwapBuffers(bool swapTV, bool swapDRC);

class OpenGLRenderer : public Renderer
{
	friend class OpenGLCanvas;
public:
	OpenGLRenderer();
	~OpenGLRenderer();

	static OpenGLRenderer* GetInstance();

	// Real runtime capability checks for texture compression formats - used by the
	// per-format "texture_compression_bcX" experimental options (BC1-BC5 -> ETC2/EAC
	// transcode, see texture_chooseDecodedFormat below and LatteTextureGL.cpp's
	// GetOpenGLFormatInfo, which reads this from another class via GetInstance() and
	// therefore needs it public - same pattern as the Vulkan renderer's equivalent
	// m_supportedFormatInfo). Before this existed, BC1/S3TC support was simply assumed
	// unconditionally; that assumption doesn't hold on most Android GPUs, so it's
	// checked for real now.
	struct GLSupportedFormatInfo_t
	{
		bool fmt_bc1{};
		// BC2 (DXT3) needs the full GL_EXT_texture_compression_s3tc bundle - the
		// DXT1-only extensions checked for fmt_bc1 above don't cover it.
		bool fmt_bc2{};
		// same reasoning as fmt_bc2 - BC3 (DXT5) is part of the same S3TC bundle extension.
		bool fmt_bc3{};
		// BC4/BC5 (RGTC) is a separate extension bundle from S3TC.
		bool fmt_bc4{};
		bool fmt_bc5{};
		// ETC2 is mandatory-core in OpenGL ES 3.0 (any conformant Android GPU) and in
		// desktop OpenGL 4.3+ / GL_ARB_ES3_compatibility, but still checked for real
		// rather than assumed, same reasoning as fmt_bc1 above.
		bool fmt_etc2{};
	};
	GLSupportedFormatInfo_t m_supportedFormatInfo;
	void QuerySupportedFormats();
	static bool IsGLExtensionSupported(std::string_view extensionName);

	// True when this renderer instance is running against an OpenGL ES driver (Android
	// Mali/Adreno/etc via EGL) rather than desktop OpenGL. Set once in QuerySupportedFormats
	// during Init, before any texture/readback work happens. Consulted by the GLES-only
	// codepaths this port needed (1D texture emulation in LatteTextureGL.cpp, the FBO-based
	// readback in TextureReadbackGL.cpp) instead of re-parsing GL_VERSION in each of those
	// places - see the OpenGL-on-Android technical report for why those two areas in
	// particular need to diverge from the desktop code.
	static bool IsGLES() { return s_isGLES; }
	// GL_EXT_texture_shadow_lod (textureLod em sampler2DArrayShadow). Desktop GL 4.x tem isso no core; no GLES so com a extensao.
	static bool HasTextureShadowLod() { return s_hasTextureShadowLod; }
	// Pode a textura BC desta dimensao ser guardada como ETC2/EAC (4-8x menos RAM/VRAM que o RGBA8 decodificado)?
	// Desktop: sempre. GLES: so 2D e 2D array - o GLES 3.x NAO aceita formatos ETC2/EAC em texturas 3D (GL_INVALID_OPERATION),
	// e 1D/cubemap/MSAA passam por caminhos de upload proprios; esses continuam decodificados em RGBA8/R8/RG8 como antes.
	static bool CanUseEtc2Transcode(Latte::E_DIM dim)
	{
		if (!s_isGLES)
			return true;
		return dim == Latte::E_DIM::DIM_2D || dim == Latte::E_DIM::DIM_2D_ARRAY;
	}
	// GL_EXT/OES_texture_border_clamp (GL_CLAMP_TO_BORDER + cor de borda). No GLES 3.2 e core, em 3.0/3.1 so por extensao.
	static bool HasBorderClamp() { return s_hasBorderClamp; }
	// GL_DEPTH_CLAMP: core no GL desktop; no GLES so com GL_EXT_depth_clamp (sem ela glEnable/glDisable(GL_DEPTH_CLAMP) = GL_INVALID_ENUM)
	static bool HasDepthClamp() { return s_hasDepthClamp; }
	// PowerVR (GLES): o compilador avisa "Calls to any function that may require a gradient calculation inside a conditional block may
	// return undefined results" em ~750 shaders do Mario Kart 8. Amostras com LOD implicito dentro de blocos condicionais
	// (predicacao do Wii U) usam derivadas indefinidas; o gerador de GLSL troca por LOD explicito 0 nesses casos.
	static bool UsesSafeConditionalSampling() { return s_isGLES && s_isPowerVR; }
	// true = GLES sem GL_EXT_depth_clamp: o depth clamp e emulado no vertex/geometry shader (uf_depthClampEmu)
	static bool NeedsDepthClampEmulation() { return s_isGLES && !s_hasDepthClamp; }
	float GetDepthClampEmuValue() const { return (NeedsDepthClampEmulation() && prevZClipEnable == 0) ? 1.0f : 0.0f; }

	// True quando o driver GLES nao tem clip control (nem GL_EXT_clip_control): origem Y (UPPER_LEFT) e
	// faixa Z (0..1) sao emuladas nos vertex/geometry shaders (uf_clipFlipY + SET_POSITION) e no front face.
	static bool IsClipControlEmulated() { return s_clipControlEmulated; }
	// +1/-1 aplicado em gl_Position.y pelos shaders (-1 = emulando UPPER_LEFT)
	float GetClipFlipY() const { return (s_clipControlEmulated && !_prevInvertY) ? -1.0f : 1.0f; }
	void ApplyFrontFace();
	bool m_guestFrontFaceCCW = true;

  private:
	static inline bool s_isGLES = false;
	static inline bool s_hasTextureShadowLod = true;
	static inline bool s_hasBorderClamp = true;
	static inline bool s_hasDepthClamp = true;
	static inline bool s_isPowerVR = false;
	static inline bool s_clipControlEmulated = false;

  public:
	// imgui
	bool ImguiBegin(bool mainWindow) override;
	void ImguiEnd() override;
	ImTextureID GenerateTexture(const std::vector<uint8>& data, const Vector2i& size) override;
	void DeleteTexture(ImTextureID id) override;
	void DeleteFontTextures() override;

	void Initialize() override;
	bool IsPadWindowActive() override;

	void Flush(bool waitIdle = false) override;
	void NotifyLatteCommandProcessorIdle() override;

	void EnableDebugMode() override;
	void SwapBuffers(bool swapTV = true, bool swapDRC = true) override;

	bool BeginFrame(bool mainWindow) override;
	void DrawEmptyFrame(bool mainWindow) override;
	void ClearColorbuffer(bool padView) override;
	void HandleScreenshotRequest(LatteTextureView* texView, bool padView) override;

	void DrawBackbufferQuad(LatteTextureView* texView, RendererOutputShader* shader, bool useLinearTexFilter, sint32 imageX, sint32 imageY, sint32 imageWidth, sint32 imageHeight, bool padView, bool clearBackground) override;
	bool CanUseFSR() const override { return true; }
	void DrawBackbufferQuadFSR(LatteTextureView* texView, sint32 imageX, sint32 imageY, sint32 imageWidth, sint32 imageHeight, bool padView, bool clearBackground, bool renderUpsideDown) override;

	void AppendOverlayDebugInfo() override
	{
		// does nothing currently
	}

	// rendertarget
	void renderTarget_setViewport(float x, float y, float width, float height, float nearZ, float farZ, bool halfZ = false) override;
	void renderTarget_setScissor(sint32 scissorX, sint32 scissorY, sint32 scissorWidth, sint32 scissorHeight) override;

	LatteCachedFBO* rendertarget_createCachedFBO(uint64 key) override;
	void rendertarget_deleteCachedFBO(LatteCachedFBO* cfbo) override;
	void rendertarget_bindFramebufferObject(LatteCachedFBO* cfbo) override;

	// renderstate functions
	void renderstate_setChannelTargetMask(uint32 renderTargetMask);
	void renderstate_setAlwaysWriteDepth();
	void renderstate_updateBlendingAndColorControl();
	void renderstate_resetColorControl();
	void renderstate_resetDepthControl();
	void renderstate_resetStencilMask();

	void renderstate_updateTextureSettingsGL(LatteDecompilerShader* shaderContext, LatteTextureView* _hostTextureView, uint32 hostTextureUnit, const Latte::LATTE_SQ_TEX_RESOURCE_WORD4_N texUnitWord4, uint32 texUnitIndex, bool isDepthSampler);

	// texture functions
	void* texture_acquireTextureUploadBuffer(uint32 size) override;
	void texture_releaseTextureUploadBuffer(uint8* mem) override;

	TextureDecoder* texture_chooseDecodedFormat(Latte::E_GX2SURFFMT format, bool isDepth, Latte::E_DIM dim, uint32 width, uint32 height) override;

	void texture_clearSlice(LatteTexture* hostTexture, sint32 sliceIndex, sint32 mipIndex) override;
	void texture_loadSlice(LatteTexture* hostTexture, sint32 width, sint32 height, sint32 depth, void* pixelData, sint32 sliceIndex, sint32 mipIndex, uint32 compressedImageSize) override;
	void texture_clearColorSlice(LatteTexture* hostTexture, sint32 sliceIndex, sint32 mipIndex, float r, float g, float b, float a) override;
	void texture_clearDepthSlice(LatteTexture* hostTexture, uint32 sliceIndex, sint32 mipIndex, bool clearDepth, bool clearStencil, float depthValue, uint32 stencilValue) override;

	LatteTexture* texture_createTextureEx(Latte::E_DIM dim, MPTR physAddress, MPTR physMipAddress, Latte::E_GX2SURFFMT format, uint32 width, uint32 height, uint32 depth, uint32 pitch, uint32 mipLevels, uint32 swizzle, Latte::E_HWTILEMODE tileMode, bool isDepth) override;

	void texture_setLatteTexture(LatteTextureView* textureView, uint32 textureUnit) override;
	void texture_bindAndActivate(LatteTextureView* textureView, uint32 textureUnit);
	void texture_copyImageSubData(LatteTexture* src, sint32 srcMip, sint32 effectiveSrcX, sint32 effectiveSrcY, sint32 srcSlice, LatteTexture* dst, sint32 dstMip, sint32 effectiveDstX, sint32 effectiveDstY, sint32 dstSlice, sint32 effectiveCopyWidth, sint32 effectiveCopyHeight, sint32 srcDepth) override;

	void texture_notifyDelete(LatteTextureView* textureView);

	LatteTextureReadbackInfo* texture_createReadback(LatteTextureView* textureView) override;

	void surfaceCopy_copySurfaceWithFormatConversion(LatteTexture* sourceTexture, sint32 srcMip, sint32 srcSlice, LatteTexture* destinationTexture, sint32 dstMip, sint32 dstSlice, sint32 width, sint32 height) override;

	void attributeStream_reset();
	void bufferCache_init(const sint32 bufferSize) override;
	void bufferCache_upload(uint8* buffer, sint32 size, uint32 bufferOffset) override;
	void bufferCache_copy(uint32 srcOffset, uint32 dstOffset, uint32 size) override;

	void buffer_bindVertexBuffer(uint32 bufferIndex, uint32 offset, uint32 size) override;
	void buffer_bindUniformBuffer(LatteConst::ShaderType shaderType, uint32 bufferIndex, uint32 offset, uint32 size) override;

	void _setupVertexAttributes();

	void attributeStream_bindVertexCacheBuffer();
	void attributeStream_unbindVertexBuffer();

	static void SetAttributeArrayState(uint32 index, bool isEnabled, sint32 aluDivisor);
	static void SetArrayElementBuffer(GLuint arrayElementBuffer);

	// index (not used by OpenGL renderer yet)
	IndexAllocation indexData_reserveIndexMemory(uint32 size) override
	{
		cemu_assert_unimplemented();
		return {};
	}

	void indexData_releaseIndexMemory(IndexAllocation& allocation) override
	{
		cemu_assert_unimplemented();
	}

	void indexData_uploadIndexMemory(IndexAllocation& allocation) override
	{
		cemu_assert_unimplemented();
	}

	// uniform
	void uniformData_update();

	// shader
	RendererShader* shader_create(RendererShader::ShaderType type, uint64 baseHash, uint64 auxHash, const std::string& source, bool isGameShader, bool isGfxPackShader) override;
	void shader_bind(RendererShader* shader);
	void shader_unbind(RendererShader::ShaderType shaderType);

	// streamout
	void streamout_setupXfbBuffer(uint32 bufferIndex, sint32 ringBufferOffset, uint32 rangeAddr, uint32 rangeSize) override;
	void streamout_begin() override;
	void bufferCache_copyStreamoutToMainBuffer(uint32 srcOffset, uint32 dstOffset, uint32 size) override;
	void streamout_rendererFinishDrawcall() override;

	// GLES has no hardware transform feedback layout-qualifier syntax (xfb_buffer/xfb_offset/
	// xfb_stride - GLSL ES never got the desktop GL_ARB_enhanced_layouts equivalent), so on
	// GLES this backend uses the same SSBO-based manual streamout path Vulkan and Metal
	// already use instead of native GL_TRANSFORM_FEEDBACK (see OpenGLRendererStreamout.cpp
	// and LatteDecompilerEmitGLSL[Header].cpp's useSSBOForStreamout branches). Desktop OpenGL
	// keeps using native transform feedback, since it has no such limitation.
	bool UseTFViaSSBO() const override { return IsGLES(); }

	// mirrors VulkanRenderer::m_streamoutState / MetalRenderer's m_state.m_streamoutState -
	// only populated/used when UseTFViaSSBO() is true (GLES)
	struct
	{
		uint32 verticesPerInstance{};
		struct
		{
			sint32 ringBufferOffset{};
		} buffer[LATTE_NUM_STREAMOUT_BUFFER];
	} m_streamoutState{};

	// draw
	void draw_init();

	void draw_beginSequence() override;
	void draw_execute(uint32 baseVertex, uint32 baseInstance, uint32 instanceCount, uint32 count, MPTR indexDataMPTR, Latte::LATTE_VGT_DMA_INDEX_TYPE::E_INDEX_TYPE indexType, const LatteDrawcallContext& drawcallContext) override;
	void draw_endSequence() override;

	template<bool TIsMinimal, bool THasProfiling>
	void draw_genericDrawHandler(uint32 baseVertex, uint32 baseInstance, uint32 instanceCount, uint32 count, MPTR indexDataMPTR, Latte::LATTE_VGT_DMA_INDEX_TYPE::E_INDEX_TYPE indexType);

	// resource garbage collection
	void cleanupAfterFrame();
	void ReleaseBufferCacheEntries();

	// occlusion queries
	LatteQueryObject* occlusionQuery_create() override;
	void occlusionQuery_destroy(LatteQueryObject* queryObj) override;
	void occlusionQuery_flush() override;
	void occlusionQuery_updateState() override {};

private:
	void GetVendorInformation() override;

	void texture_setActiveTextureUnit(sint32 index);

	void texture_syncSliceSpecialBC4(LatteTexture* srcTexture, sint32 srcSliceIndex, sint32 srcMipIndex, LatteTexture* dstTexture, sint32 dstSliceIndex, sint32 dstMipIndex);
	void texture_syncSliceSpecialIntegerToBC3(LatteTexture* srcTexture, sint32 srcSliceIndex, sint32 srcMipIndex, LatteTexture* dstTexture, sint32 dstSliceIndex, sint32 dstMipIndex);

	GLuint m_pipeline = 0;

	bool m_isPadViewContext{};

	// rendertarget viewport
	float prevViewportX = 0;
	float prevViewportY = 0;
	float prevViewportWidth = 0;
	float prevViewportHeight = 0;

	float prevNearZ = -999999.0f;
	float prevFarZ = -9999999.0f;
	bool _prevInvertY = false;
	bool _prevHalfZ = false;

	uint32 prevScissorX = 0;
	uint32 prevScissorY = 0;
	uint32 prevScissorWidth = 0;
	uint32 prevScissorHeight = 0;
	bool prevScissorEnable = false;

	bool m_isXfbActive = false;

	sint32 activeTextureUnit = 0;
	void* m_latteBoundTextures[Latte::GPU_LIMITS::NUM_TEXTURES_PER_STAGE * 3]{};

	// backbuffer blit
	GLuint m_backbufferBlit_uniformBuffer;

	// intermediate texture FSR's EASU pass renders into, before RCAS reads it back and
	// writes the real, final image - see DrawBackbufferQuadFSR(). Lazily (re)created to match
	// whatever output size is currently requested; texture-less (0) until first used.
	GLuint m_fsrIntermediateTexture = 0;
	GLuint m_fsrIntermediateFBO = 0;
	sint32 m_fsrIntermediateWidth = 0;
	sint32 m_fsrIntermediateHeight = 0;

	// attribute stream
	GLuint glAttributeCacheAB{};
	GLuint _boundArrayBuffer{};

	// streamout
	GLuint glStreamoutCacheRingBuffer;

	// cfbo
	GLuint prevBoundFBO = 0;
	GLuint glId_fbo = 0;

	// renderstate
	uint32 prevBlendControlReg[8] = { 0 };
	uint32 prevBlendMask = 0;
	uint32 prevLogicOp = 0;
	uint32 prevBlendColorConstant[4] = { 0 };
	uint8 prevAlphaTestEnable = 0;
	bool prevDepthEnable = 0;
	bool prevDepthWriteEnable = 0;
	Latte::LATTE_DB_DEPTH_CONTROL::E_ZFUNC prevDepthFunc = (Latte::LATTE_DB_DEPTH_CONTROL::E_ZFUNC)-1;
	bool prevStencilEnable = false;
	Latte::LATTE_DB_DEPTH_CONTROL::E_STENCILFUNC prevFrontStencilFunc = (Latte::LATTE_DB_DEPTH_CONTROL::E_STENCILFUNC)-1;
	Latte::LATTE_DB_DEPTH_CONTROL::E_STENCILFUNC prevBackStencilFunc = (Latte::LATTE_DB_DEPTH_CONTROL::E_STENCILFUNC)-1;
	Latte::LATTE_DB_DEPTH_CONTROL::E_STENCILACTION prevFrontStencilZPass = (Latte::LATTE_DB_DEPTH_CONTROL::E_STENCILACTION)-1;
	Latte::LATTE_DB_DEPTH_CONTROL::E_STENCILACTION prevFrontStencilZFail = (Latte::LATTE_DB_DEPTH_CONTROL::E_STENCILACTION)-1;
	Latte::LATTE_DB_DEPTH_CONTROL::E_STENCILACTION prevFrontStencilFail = (Latte::LATTE_DB_DEPTH_CONTROL::E_STENCILACTION)-1;
	Latte::LATTE_DB_DEPTH_CONTROL::E_STENCILACTION prevBackStencilZPass = (Latte::LATTE_DB_DEPTH_CONTROL::E_STENCILACTION)-1;
	Latte::LATTE_DB_DEPTH_CONTROL::E_STENCILACTION prevBackStencilZFail = (Latte::LATTE_DB_DEPTH_CONTROL::E_STENCILACTION)-1;
	Latte::LATTE_DB_DEPTH_CONTROL::E_STENCILACTION prevBackStencilFail = (Latte::LATTE_DB_DEPTH_CONTROL::E_STENCILACTION)-1;
	uint32 prevStencilRefFront = -1;
	uint32 prevStencilCompareMaskFront = -1;
	uint32 prevStencilWriteMaskFront = -1;
	uint32 prevStencilRefBack = -1;
	uint32 prevStencilCompareMaskBack = -1;
	uint32 prevStencilWriteMaskBack = -1;

	uint32 prevTargetColorMask = 0; // RGBA color mask for each render target (4 bit per target, starting from LSB)
	uint32 prevCullEnable = 0;
	uint32 prevCullFront = 0;
	uint32 prevCullBack = 0;
	Latte::LATTE_PA_SU_SC_MODE_CNTL::E_FRONTFACE prevCullFrontFace = Latte::LATTE_PA_SU_SC_MODE_CNTL::E_FRONTFACE::CCW;

	uint32 prevPolygonFrontScaleU32 = 0;
	uint32 prevPolygonFrontOffsetU32 = 0;
	uint32 prevPolygonClampU32 = 0;
	uint32 prevPolygonOffsetFrontEnabled = 0;
	uint32 prevPolygonOffsetBackEnabled = 0;

	uint32 prevZClipEnable = 0;

	uint32 prevPointSizeReg = 0xFFFFFFFF;

	uint32 prevPrimitiveRestartIndex = 0xFFFFFFFF;

	GLuint prevVertexShaderProgram = -1;
	GLuint prevGeometryShaderProgram = -1;
	GLuint prevPixelShaderProgram = -1;

	// occlusion queries
	std::vector<class LatteQueryObjectGL*> list_queryCacheOcclusion; // cache for unused queries

	// resource garbage collection
	struct BufferCacheReleaseQueueEntry
	{
		BufferCacheReleaseQueueEntry(VirtualBufferHeap_t* heap, VirtualBufferHeapEntry_t* entry) : m_heap(heap), m_entry(entry) {};

		void free()
		{
			virtualBufferHeap_free(m_heap, m_entry);
		}

		VirtualBufferHeap_t* m_heap{};
		VirtualBufferHeapEntry_t* m_entry{};
	};

	struct
	{
		sint32 index;
		std::vector<BufferCacheReleaseQueueEntry> bufferCacheEntries;
	}m_destructionQueues;

};
