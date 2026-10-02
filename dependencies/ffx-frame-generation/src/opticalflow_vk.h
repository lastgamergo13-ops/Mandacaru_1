// opticalflow_vk.h
//
// Backend Vulkan "escrito na mao" para o componente Optical Flow do FSR3.
//
// VERSAO 2 - REESCRITA APOS CRASH REAL EM HARDWARE (Adreno 650, signal 6,
// "falha ao criar pipeline para pass 1"). A V1 usava um layout de
// descriptor set GENERICO e UNICO para os 7 passes - isso quebrou porque
// cada pass real da AMD tem um conjunto de recursos (SRV/UAV/CB) e um
// tamanho de thread group DIFERENTE, confirmado lendo os #define no topo
// de cada um dos 7 arquivos .hlsl. Essa versao da a cada pass seu proprio
// VkDescriptorSetLayout e VkPipelineLayout, batendo com o que o .spv de
// cada um realmente espera.
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
	void CreateSampler();
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
	VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
	VkSampler m_colorSampler = VK_NULL_HANDLE;

	// --- recursos intermediarios ---
	// luma preparado (pass 0 escreve, pass 1 le/escreve)
	VkImage m_lumaImage = VK_NULL_HANDLE;
	VkDeviceMemory m_lumaMemory = VK_NULL_HANDLE;
	VkImageView m_lumaView = VK_NULL_HANDLE;

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

	// saida final: vetores de movimento
	VkImage m_opticalFlowVectorImage = VK_NULL_HANDLE;
	VkDeviceMemory m_opticalFlowVectorMemory = VK_NULL_HANDLE;
	VkImageView m_opticalFlowVectorView = VK_NULL_HANDLE;

	// copia do optical flow do "nivel anterior" - os passes 5/6 leem
	// isso como SRV (FILTER le OPTICAL_FLOW_PREVIOUS, SCALE le
	// OPTICAL_FLOW) alem de escrever o resultado do frame atual.
	// Simplificado pra apontar pro mesmo buffer de saida em varios
	// pontos, ja que nao fazemos o laco multi-nivel real.
	VkImageView m_opticalFlowPreviousView = VK_NULL_HANDLE; // alias de m_opticalFlowVectorView nesta versao simplificada

	VkBuffer m_constantBuffer = VK_NULL_HANDLE;
	VkDeviceMemory m_constantBufferMemory = VK_NULL_HANDLE;

	// CB_SPD - segundo constant buffer, so usado pelo pass 1
	VkBuffer m_spdConstantBuffer = VK_NULL_HANDLE;
	VkDeviceMemory m_spdConstantBufferMemory = VK_NULL_HANDLE;
};
