// opticalflow_vk.h
//
// Backend Vulkan "escrito na mao" (sem o backend oficial da AMD, que
// so existe pra DX12) para o componente Optical Flow do FSR3.
//
// ESCOPO DESTA PRIMEIRA VERSAO (leia antes de usar):
//   - Implementa os 7 passes do Optical Flow, na ordem correta.
//   - Roda em UMA UNICA resolucao (sem a piramide de mip de 6 niveis
//     que a estrutura interna original da AMD usa para robustez em
//     movimento rapido). Ver TODO no .cpp.
//   - Objetivo desta versao: validar que os .spv carregam, os
//     descriptor sets batem com os shaders, e o pipeline roda sem
//     erro de validacao do Vulkan. Depois disso funcionando, a
//     proxima iteracao adiciona a piramide de mip completa.
//
// Local no projeto: dependencies/ffx-frame-generation/src/

#pragma once

#if defined(__ANDROID__) && !defined(VK_USE_PLATFORM_ANDROID_KHR)
#define VK_USE_PLATFORM_ANDROID_KHR
#endif

#include <vulkan/vulkan.h>
#include <cstdint>

// Forward-declare para nao acoplar este header ao VulkanRenderer inteiro
class VulkanRenderer;
class VKRMemoryManager;

class OpticalFlowVk
{
public:
	// --- os 7 passes, na ordem do enum FfxOpticalflowPass original ---
	enum class Pass : uint32_t
	{
		PrepareLuma = 0,
		ComputeLuminancePyramid,     // TODO: hoje so 1 nivel, falta piramide de 6 niveis
		GenerateScdHistogram,
		ComputeScdDivergence,
		ComputeOpticalFlowAdvancedV5, // nucleo do algoritmo, mais pesado
		FilterOpticalFlowV5,
		ScaleOpticalFlowAdvancedV5,
		Count
	};

	// resolutionWidth/Height: resolucao do backbuffer do jogo (a
	// mesma resolucao que o Cemu esta renderizando internamente).
	OpticalFlowVk(VulkanRenderer* renderer, uint32_t resolutionWidth, uint32_t resolutionHeight);
	~OpticalFlowVk();

	// Nao copiavel (possui handles Vulkan)
	OpticalFlowVk(const OpticalFlowVk&) = delete;
	OpticalFlowVk& operator=(const OpticalFlowVk&) = delete;

	// Executa o pipeline completo de Optical Flow para o frame atual.
	//   commandBuffer   - command buffer ja em estado "recording"
	//   currentColorView  - VkImageView do backbuffer atual (cor, sRGB/UNORM)
	//   previousColorView - VkImageView do backbuffer do frame anterior
	//   reset           - true no primeiro frame ou apos um corte de cena
	//                      (troca de fase, carregamento, etc.) - o Optical
	//                      Flow ignora o frame anterior nesse caso.
	// Resultado fica em GetOpticalFlowVectorView() apos a chamada,
	// pronto para ser consumido pelo Frame Interpolation.
	void Dispatch(VkCommandBuffer commandBuffer, VkImageView currentColorView, VkImageView previousColorView, bool reset);

	// View do buffer de vetores de movimento gerado - e o que o
	// FrameInterpolationVk vai consumir como entrada.
	VkImageView GetOpticalFlowVectorView() const { return m_opticalFlowVectorView; }

private:
	void CreateShaderModules();
	void CreatePipelines();
	void CreateIntermediateResources(uint32_t width, uint32_t height);
	void CreateDescriptorSets();
	void CreateSampler();
	void UpdateDescriptorSets(VkImageView currentColorView);
	void DestroyAll();

	// Helper: cria um VkShaderModule a partir de um .spv em shaders_spv/
	VkShaderModule LoadShaderModule(const char* spvFileName) const;

	// Helper: barreira de memoria generica entre passes de compute
	// (leitura apos escrita de uma imagem/buffer em outro dispatch)
	static void ComputeToComputeBarrier(VkCommandBuffer cb);

	VulkanRenderer* m_renderer = nullptr;
	VKRMemoryManager* m_memoryManager = nullptr;

	uint32_t m_width = 0;
	uint32_t m_height = 0;

	bool m_resourcesTransitioned = false; // controla a transicao de layout (ver Dispatch())

	VkShaderModule m_shaderModules[(uint32_t)Pass::Count] = {};
	VkPipeline m_pipelines[(uint32_t)Pass::Count] = {};
	VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
	VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
	// um descriptor set por pass (cada shader le/escreve um conjunto
	// diferente de recursos - ver ffx_opticalflow_callbacks_hlsl.h)
	VkDescriptorSet m_descriptorSets[(uint32_t)Pass::Count] = {};

	// --- recursos intermediarios (versao simplificada, 1 nivel) ---
	// luma preparado a partir da cor de entrada
	VkImage m_lumaImage = VK_NULL_HANDLE;
	VkDeviceMemory m_lumaMemory = VK_NULL_HANDLE;
	VkImageView m_lumaView = VK_NULL_HANDLE;

	// histograma de scene-change-detection (buffer, nao imagem)
	VkBuffer m_scdHistogramBuffer = VK_NULL_HANDLE;
	VkDeviceMemory m_scdHistogramMemory = VK_NULL_HANDLE;

	// saida final: vetores de movimento (RG16F tipicamente)
	VkImage m_opticalFlowVectorImage = VK_NULL_HANDLE;
	VkDeviceMemory m_opticalFlowVectorMemory = VK_NULL_HANDLE;
	VkImageView m_opticalFlowVectorView = VK_NULL_HANDLE;

	// buffer de constantes (equivalente ao cbuffer CB_COMMON do HLSL)
	VkBuffer m_constantBuffer = VK_NULL_HANDLE;
	VkDeviceMemory m_constantBufferMemory = VK_NULL_HANDLE;

	// sampler usado para ler a imagem de cor de entrada (binding 1)
	VkSampler m_colorSampler = VK_NULL_HANDLE;
};
