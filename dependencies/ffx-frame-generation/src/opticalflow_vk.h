// opticalflow_vk.h
//
// Backend Vulkan "escrito na mao" para o componente Optical Flow do FSR3.
//
// VERSAO 3 - REESCRITA APOS INSPECIONAR O BINARIO .spv REAL (nao so o
// codigo-fonte .hlsl). A V2 ainda deu crash porque derivar bindings do
// #define do .hlsl nao e confiavel: o dxc (1) elimina por otimizacao
// qualquer constant buffer cujos campos acabem nao afetando o resultado
// final de um pass especifico (passes 1, 3 e 5 NAO TEM cbOF nenhum,
// mesmo o .hlsl declarando), e (2) sem flags de "shift" na compilacao,
// numera bindings de tipos HLSL diferentes (b/t/u) INDEPENDENTEMENTE,
// causando colisao (ex: cbOF, uma textura SRV e uma imagem UAV podem
// todos cair em "binding 0" ao mesmo tempo). Essa versao:
//   1. Exige recompilar os .spv com shift flags (ver .cpp de comando),
//      pra cada tipo de registro HLSL cair numa faixa de binding
//      separada: b+0, t+100, u+200.
//   2. Usa o layout EXATO de cada pass, extraido inspecionando o
//      binario .spv compilado (nao o .hlsl), incluindo quais passes
//      NAO tem o constant buffer comum.
//   3. Remove o VkSampler por completo - confirmado que os 7 passes
//      usam Load() (OpImageFetch), nao Sample(), entao a textura e
//      SAMPLED_IMAGE puro, sem sampler nenhum.
//
// ESCOPO DESTA VERSAO (leia antes de usar):
//   - Bindings de cada pass: fieis ao .hlsl real (tabela completa no .cpp).
//   - Pirmide de mip (pass 1, Luminance Pyramid): o shader EXIGE 6
//     bindings de nivel de mip pra compilar (senao da o mesmo crash de
//     antes) - todos os 6 sao criados, mas niveis 2-6 apontam pra
//     imagens pequenas "dummy" sem dado util (continuamos na
//     simplificacao de "1 nivel" decidida desde o inicio do projeto,
//     agora sem quebrar o pipeline).
//   - Dispatch: as formulas reais da AMD (ffx_opticalflow.cpp) fazem um
//     LACO sobre varios niveis de piramide pros passes 4/5/6
//     (compute_optical_flow_advanced, filter, scale). Esta versao
//     despacha cada um UMA VEZ SO, na resolucao cheia (consistente com
//     "sem piramide completa") - ver comentario detalhado em cada
//     Dispatch*() no .cpp.
//   - Buffers de histograma SCD dimensionados com as constantes reais
//     da AMD (HistogramBins=256, HistogramsPerDim=3) - antes estava
//     chutado em 256 elementos, o certo e 256*9=2304.
//
// Local no projeto: dependencies/ffx-frame-generation/src/

#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>

class VulkanRenderer;
class VKRMemoryManager;

class OpticalFlowVk
{
public:
	OpticalFlowVk(VulkanRenderer* renderer, uint32_t resolutionWidth, uint32_t resolutionHeight);
	~OpticalFlowVk();

	OpticalFlowVk(const OpticalFlowVk&) = delete;
	OpticalFlowVk& operator=(const OpticalFlowVk&) = delete;

	void Dispatch(VkCommandBuffer commandBuffer, VkImageView currentColorView, VkImageView previousColorView, bool reset);

	VkImageView GetOpticalFlowVectorView() const { return m_opticalFlowVectorView; }

	// Os 7 passes, na ordem real de dispatch (ver ffx_opticalflow.cpp:
	// PrepareLuma -> ComputeLuminancePyramid -> GenerateSCDHistogram ->
	// ComputeSCDDivergence -> ComputeOpticalFlowAdvancedV5 ->
	// FilterOpticalFlowV5 -> ScaleOpticalFlowAdvancedV5)
	enum class Pass : uint32_t
	{
		PrepareLuma = 0,
		ComputeLuminancePyramid,
		GenerateScdHistogram,
		ComputeScdDivergence,
		ComputeOpticalFlowAdvancedV5,
		FilterOpticalFlowV5,
		ScaleOpticalFlowAdvancedV5,
		Count
	};

private:
	void CreateShaderModules();
	void CreateIntermediateResources(uint32_t width, uint32_t height);
	void CreatePerPassLayoutsAndPipelines();
	void CreateDescriptorPoolAndSets();
	void UpdateDescriptorSets(VkImageView currentColorView, VkImageView previousColorView);
	void DestroyAll();

	VkShaderModule LoadShaderModule(const char* spvFileName) const;
	static void ComputeToComputeBarrier(VkCommandBuffer cb);

	VulkanRenderer* m_renderer = nullptr;
	VKRMemoryManager* m_memoryManager = nullptr;

	uint32_t m_width = 0;
	uint32_t m_height = 0;
	bool m_resourcesTransitioned = false;

	VkShaderModule m_shaderModules[(uint32_t)Pass::Count] = {};
	// Um layout/pipeline POR PASS agora, nao mais compartilhado.
	VkDescriptorSetLayout m_descriptorSetLayouts[(uint32_t)Pass::Count] = {};
	VkPipelineLayout m_pipelineLayouts[(uint32_t)Pass::Count] = {};
	VkPipeline m_pipelines[(uint32_t)Pass::Count] = {};
	VkDescriptorSet m_descriptorSets[(uint32_t)Pass::Count] = {};
	// Descriptor set EXTRA, so pra segunda chamada do pass PrepareLuma
	// (luma do frame anterior) - usa o MESMO layout/pipeline de
	// Pass::PrepareLuma, so que com recursos diferentes (cor anterior
	// -> luma anterior, em vez de cor atual -> luma atual).
	VkDescriptorSet m_descriptorSetPrepareLumaPrevious = VK_NULL_HANDLE;
	VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
	// SEM VkSampler - confirmado via inspecao do .spv que os 7 passes
	// usam Load() (OpImageFetch), nunca Sample(). Nenhuma textura
	// precisa de sampler nesta versao.

	// --- recursos intermediarios ---
	// luma do frame ATUAL (pass 0 escreve a partir de currentColorView)
	VkImage m_lumaImage = VK_NULL_HANDLE;
	VkDeviceMemory m_lumaMemory = VK_NULL_HANDLE;
	VkImageView m_lumaView = VK_NULL_HANDLE;

	// luma do frame ANTERIOR (pass 0 roda DE NOVO, a partir de
	// previousColorView - os bindings r_optical_flow_previous_input
	// esperam luma ja processado, nao cor crua)
	VkImage m_lumaPreviousImage = VK_NULL_HANDLE;
	VkDeviceMemory m_lumaPreviousMemory = VK_NULL_HANDLE;
	VkImageView m_lumaPreviousView = VK_NULL_HANDLE;

	// niveis 1-6 da piramide de luminancia (pass 1 escreve). Niveis
	// 2-6 sao imagens pequenas "dummy" 4x4 - ver aviso no topo do
	// arquivo sobre a simplificacao de 1 nivel.
	static constexpr int kPyramidLevels = 6;
	VkImage m_pyramidImages[kPyramidLevels] = {};
	VkDeviceMemory m_pyramidMemory[kPyramidLevels] = {};
	VkImageView m_pyramidViews[kPyramidLevels] = {};

	// buffers do histograma SCD - tamanho real: 256 bins * 9 (3x3
	// regioes) = 2304 elementos (antes estava errado, 256 fixo)
	static constexpr uint32_t kHistogramElementCount = 256 * 3 * 3;
	VkBuffer m_scdHistogramBuffer = VK_NULL_HANDLE;
	VkDeviceMemory m_scdHistogramMemory = VK_NULL_HANDLE;
	VkBuffer m_scdPreviousHistogramBuffer = VK_NULL_HANDLE;
	VkDeviceMemory m_scdPreviousHistogramMemory = VK_NULL_HANDLE;
	VkBuffer m_scdTempBuffer = VK_NULL_HANDLE;
	VkDeviceMemory m_scdTempMemory = VK_NULL_HANDLE;
	VkBuffer m_scdOutputBuffer = VK_NULL_HANDLE;
	VkDeviceMemory m_scdOutputMemory = VK_NULL_HANDLE;

	// Pipeline de 3 estagios do calculo do flow em si - imagens
	// SEPARADAS (nao dava pra ler e escrever a mesma, ver pass
	// 4/5/6 no .cpp):
	//   pass 4 escreve RAW -> pass 5 le RAW, escreve FILTERED ->
	//   pass 6 le FILTERED, escreve FINAL (exposto por GetOpticalFlowVectorView())
	VkImage m_opticalFlowRawImage = VK_NULL_HANDLE;
	VkDeviceMemory m_opticalFlowRawMemory = VK_NULL_HANDLE;
	VkImageView m_opticalFlowRawView = VK_NULL_HANDLE;

	VkImage m_opticalFlowFilteredImage = VK_NULL_HANDLE;
	VkDeviceMemory m_opticalFlowFilteredMemory = VK_NULL_HANDLE;
	VkImageView m_opticalFlowFilteredView = VK_NULL_HANDLE;

	VkImage m_opticalFlowVectorImage = VK_NULL_HANDLE; // FINAL
	VkDeviceMemory m_opticalFlowVectorMemory = VK_NULL_HANDLE;
	VkImageView m_opticalFlowVectorView = VK_NULL_HANDLE;

	// Constant buffer unico (cbOF) - confirmado que cbOF_SPD nao e
	// usado por NENHUM dos 7 passes no binario real (sempre otimizado
	// pra fora pelo dxc), entao nao existe mais um segundo CB aqui.
	VkBuffer m_constantBuffer = VK_NULL_HANDLE;
	VkDeviceMemory m_constantBufferMemory = VK_NULL_HANDLE;
};
