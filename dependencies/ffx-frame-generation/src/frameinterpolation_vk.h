// frameinterpolation_vk.h  (V6 - interpolacao simples com fluxo optico real)
//
// POR QUE ESTA VERSAO MUDOU: o Frame Interpolation original da AMD (7 passes:
// setup, reconstruct_previous_depth, optical_flow_vector_field, disocclusion_mask,
// frameinterpolation, inpainting_pyramid, inpainting) precisa de DEPTH e MOTION
// VECTORS do jogo - o Cemu nao tem nenhum dos dois. Alem disso o pass
// inpainting_pyramid (SPD) nao cria pipeline no Adreno 650 (quad ops).
// Resultado: no modo "so optical flow" aqueles passes leem imagens zeradas.
//
// Esta versao usa UM shader proprio (mandacaru_frameinterpolation_simple.hlsl)
// que consome o fluxo optico REAL do OpticalFlowVk (AMD FSR3) e gera o frame
// intermediario por warp bidirecional + checagem de consistencia de cor.
//
// A API publica (Dispatch / GetInterpolatedFrame*) e a mesma de antes: o
// swapchain_vk.cpp nao muda.
//
// Local no projeto: dependencies/ffx-frame-generation/src/

#pragma once

#if defined(__ANDROID__) && !defined(VK_USE_PLATFORM_ANDROID_KHR)
#define VK_USE_PLATFORM_ANDROID_KHR
#endif

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

	// commandBuffer: ja em gravacao; OpticalFlowVk::Dispatch() deve ter sido
	// chamado ANTES, no mesmo command buffer. Resultado em GetInterpolatedFrame*().
	// progress (0..1): posicao do frame gerado DEPOIS do frame real atual, em fracao do intervalo
	// entre frames reais (ex.: multiplicador 3x -> 0.333 e 0.667). Se o shader V7 estiver
	// disponivel o frame e EXTRAPOLADO a partir do atual (ordem temporal correta); senao
	// (so o shader antigo) cai para interpolacao entre anterior e atual com t = progress.
	void Dispatch(VkCommandBuffer commandBuffer,
		VkImageView currentColorView,
		VkImageView previousColorView,
		const OpticalFlowVk& opticalFlow,
		float deltaTimeSeconds,
		bool reset,
		float progress = 0.5f);

	bool SupportsExtrapolation() const { return m_supportsExtrapolation; }

	VkImageView GetInterpolatedFrameView() const { return m_interpolatedFrameView; }
	VkImage GetInterpolatedFrameImage() const { return m_interpolatedFrameImage; }

private:
	void CreateResources();
	void CreatePipeline();
	void CreateDescriptors();
	void DestroyAll();

	VulkanRenderer* m_renderer = nullptr;
	VKRMemoryManager* m_memoryManager = nullptr;

	uint32_t m_width = 0;
	uint32_t m_height = 0;
	bool m_resourcesTransitioned = false;
	bool m_supportsExtrapolation = false; // true se carregou mandacaru_frameinterpolation_v7.spv

	VkShaderModule m_shaderModule = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
	VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
	VkPipeline m_pipeline = VK_NULL_HANDLE;
	VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
	VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;

	// saida: frame interpolado (RGBA16F, storage + transfer src)
	VkImage m_interpolatedFrameImage = VK_NULL_HANDLE;
	VkDeviceMemory m_interpolatedFrameMemory = VK_NULL_HANDLE;
	VkImageView m_interpolatedFrameView = VK_NULL_HANDLE;

	VkBuffer m_constantBuffer = VK_NULL_HANDLE;
	VkDeviceMemory m_constantBufferMemory = VK_NULL_HANDLE;
};
