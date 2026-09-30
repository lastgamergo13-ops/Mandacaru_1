// frameinterpolation_vk.h
//
// Backend Vulkan "escrito na mao" para o componente Frame
// Interpolation do FSR3. Consome a saida do OpticalFlowVk.
//
// ESCOPO DESTA PRIMEIRA VERSAO (leia antes de usar):
//   - Implementa 7 dos 9 dispatches reais, na ordem CONFIRMADA no
//     ffx_frameinterpolation.cpp de referencia (nao no enum, que nao
//     reflete a ordem de execucao - ver comentario no .cpp).
//   - Pulados de proposito (dependem de motion vectors do jogo, que o
//     Cemu nao tem): GAME_MOTION_VECTOR_FIELD, GAME_VECTOR_FIELD_INPAINTING_PYRAMID.
//   - Pulado por falta de confirmacao (ver TODO no .cpp):
//     RECONSTRUCT_AND_DILATE. No .cpp de referencia esse pass so
//     aparece dentro de uma funcao de debug-check, nao no caminho
//     principal de dispatch - ainda nao confirmei se ele e
//     disparado por outro componente (o Upscaler) antes de chamar o
//     Frame Interpolation, ou se falta mesmo no caminho principal.
//     Ate confirmar, a imagem de profundidade/velocidade dilatada
//     fica com um valor constante (ver TODO em CreateIntermediateResources).
//   - Sem piramide de mip completa no InpaintingPyramid (mesma
//     simplificacao do OpticalFlowVk).
//
// Local no projeto: dependencies/ffx-frame-generation/src/

#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>

class VulkanRenderer;
class VKRMemoryManager;
class OpticalFlowVk;

class FrameInterpolationVk
{
public:
	FrameInterpolationVk(VulkanRenderer* renderer, uint32_t resolutionWidth, uint32_t resolutionHeight);
	~FrameInterpolationVk();

	FrameInterpolationVk(const FrameInterpolationVk&) = delete;
	FrameInterpolationVk& operator=(const FrameInterpolationVk&) = delete;

	// Executa o pipeline completo de Frame Interpolation para o frame atual.
	//   commandBuffer        - command buffer ja em estado "recording"
	//   currentColorView     - backbuffer atual (o frame "real" mais recente)
	//   previousColorView    - backbuffer do frame anterior (previousInterpolationSource)
	//   opticalFlow          - instancia do OpticalFlowVk ja despachada
	//                          para este par de frames (Dispatch() dela
	//                          deve ser chamado ANTES deste, no mesmo
	//                          command buffer)
	//   deltaTimeSeconds     - tempo entre o frame anterior e o atual
	//   reset                - true no primeiro frame ou apos corte de cena
	// Resultado fica em GetInterpolatedFrameView() apos a chamada.
	void Dispatch(VkCommandBuffer commandBuffer,
		VkImageView currentColorView,
		VkImageView previousColorView,
		const OpticalFlowVk& opticalFlow,
		float deltaTimeSeconds,
		bool reset);

	// View do frame interpolado gerado - e o que o swapchain_vk vai
	// apresentar entre dois frames reais.
	VkImageView GetInterpolatedFrameView() const { return m_interpolatedFrameView; }
	VkImage GetInterpolatedFrameImage() const { return m_interpolatedFrameImage; }

	// --- os 7 passes confirmados, NA ORDEM REAL de dispatch (ver aviso no topo) ---
	// Publico porque o .cpp precisa dele fora da classe (tamanho de array).
	enum class Pass : uint32_t
	{
		Setup = 0,
		ReconstructPreviousDepth,
		OpticalFlowVectorField,
		DisocclusionMask,
		Interpolation,          // pass final - combina tudo (ffx_frameinterpolation_pass.hlsl)
		InpaintingPyramid,
		Inpainting,
		Count
	};

private:
	void CreateShaderModules();
	void CreatePipelines();
	void CreateIntermediateResources(uint32_t width, uint32_t height);
	void CreateDescriptorSets();
	void CreateSampler();
	void UpdateDescriptorSets(VkImageView currentColorView, VkImageView previousColorView, VkImageView opticalFlowVectorView);
	void DestroyAll();

	VkShaderModule LoadShaderModule(const char* spvFileName) const;
	static void ComputeToComputeBarrier(VkCommandBuffer cb);

	VulkanRenderer* m_renderer = nullptr;
	VKRMemoryManager* m_memoryManager = nullptr;

	uint32_t m_width = 0;
	uint32_t m_height = 0;
	bool m_resourcesTransitioned = false;

	// --- os 7 passes, na ordem definida publicamente acima ---
	VkShaderModule m_shaderModules[(uint32_t)Pass::Count] = {};

	VkPipeline m_pipelines[(uint32_t)Pass::Count] = {};
	VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
	VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
	VkDescriptorSet m_descriptorSets[(uint32_t)Pass::Count] = {};
	VkSampler m_colorSampler = VK_NULL_HANDLE;

	// --- recursos intermediarios ---
	// profundidade reconstruida do frame anterior (R32F)
	VkImage m_reconstructedPrevDepthImage = VK_NULL_HANDLE;
	VkDeviceMemory m_reconstructedPrevDepthMemory = VK_NULL_HANDLE;
	VkImageView m_reconstructedPrevDepthView = VK_NULL_HANDLE;

	// TODO: velocidade dilatada (dilatedVelocity) - deveria vir do pass
	// RECONSTRUCT_AND_DILATE, que nao esta sendo despachado nesta
	// versao (ver aviso no topo do arquivo). Por enquanto esta imagem
	// fica alocada mas SEM SER ESCRITA por nenhum dispatch - os passes
	// que a leem vao ver zero/lixo ate isso ser resolvido.
	VkImage m_dilatedVelocityImage = VK_NULL_HANDLE;
	VkDeviceMemory m_dilatedVelocityMemory = VK_NULL_HANDLE;
	VkImageView m_dilatedVelocityView = VK_NULL_HANDLE;

	// mascara de disoclusao (R8 ou R32F, 1 canal)
	VkImage m_disocclusionMaskImage = VK_NULL_HANDLE;
	VkDeviceMemory m_disocclusionMaskMemory = VK_NULL_HANDLE;
	VkImageView m_disocclusionMaskView = VK_NULL_HANDLE;

	// piramide de inpainting (RGBA32F, 1 nivel nesta versao - ver TODO)
	VkImage m_inpaintingPyramidImage = VK_NULL_HANDLE;
	VkDeviceMemory m_inpaintingPyramidMemory = VK_NULL_HANDLE;
	VkImageView m_inpaintingPyramidView = VK_NULL_HANDLE;

	// mascara de inpainting (R32F, lida E escrita - precisa de formato
	// explicito, ver Grupo C do plano de correcao OpenGL - aqui no
	// Vulkan isso nao e problema, mas mantenha o formato real
	// consistente se algum dia migrar pro OpenGL)
	VkImage m_inpaintingMaskImage = VK_NULL_HANDLE;
	VkDeviceMemory m_inpaintingMaskMemory = VK_NULL_HANDLE;
	VkImageView m_inpaintingMaskView = VK_NULL_HANDLE;

	// saida final: o frame interpolado (RGBA16F)
	VkImage m_interpolatedFrameImage = VK_NULL_HANDLE;
	VkDeviceMemory m_interpolatedFrameMemory = VK_NULL_HANDLE;
	VkImageView m_interpolatedFrameView = VK_NULL_HANDLE;

	// buffer de constantes (cbFI - bem maior que o do optical flow)
	VkBuffer m_constantBuffer = VK_NULL_HANDLE;
	VkDeviceMemory m_constantBufferMemory = VK_NULL_HANDLE;

	uint32_t m_frameIndex = 0;
};
