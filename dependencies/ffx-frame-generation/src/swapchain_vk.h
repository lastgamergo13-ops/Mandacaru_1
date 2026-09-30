// swapchain_vk.h
//
// Camada que insere o frame gerado (Optical Flow + Frame Interpolation)
// entre dois frames reais na apresentacao.
//
// ESCOPO DESTA PRIMEIRA VERSAO (leia antes de usar):
//   - Estrategia: em vez de reescrever a SwapBuffer() inteira do Cemu
//     (arriscado demais pra gerar as cegas - ela ja lida com TV+DRC
//     duplo, fences, present_wait), esta classe reaproveita os
//     metodos que JA EXISTEM em SwapchainInfoVk (AcquireImage,
//     ConsumeAcquireSemaphore) pra fazer um SEGUNDO acquire+present
//     "extra", com o frame interpolado.
//   - Padrao: apresenta o frame real (fluxo existente do Cemu,
//     inalterado) -> esta classe entra em acao -> adquire OUTRA
//     imagem do swapchain -> desenha o frame interpolado nela ->
//     apresenta essa tambem. E o mesmo padrao usado por
//     implementacoes equivalentes (ex. lsfg-vk).
//   - AINDA NAO FEITO (ver TODO no .cpp): a captura do backbuffer
//     atual/anterior (CaptureBackbuffer) precisa ser chamada pelo
//     integrador logo APOS o Cemu terminar de desenhar o frame real,
//     ANTES do present real acontecer - isso exige uma linha extra
//     dentro de SwapBuffer() no VulkanRenderer.cpp (ver instrucoes
//     de integracao que acompanham este arquivo).
//
// Local no projeto: dependencies/ffx-frame-generation/src/

#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <memory>

class VulkanRenderer;
class VKRMemoryManager;
class OpticalFlowVk;
class FrameInterpolationVk;
struct SwapchainInfoVk;

class SwapchainFrameGenVk
{
public:
	SwapchainFrameGenVk(VulkanRenderer* renderer, uint32_t width, uint32_t height, uint32_t graphicsQueueFamilyIndex);
	~SwapchainFrameGenVk();

	SwapchainFrameGenVk(const SwapchainFrameGenVk&) = delete;
	SwapchainFrameGenVk& operator=(const SwapchainFrameGenVk&) = delete;

	// Liga/desliga o recurso em runtime (opcao "Ativar/Desativar" do
	// relatorio de integracao, secao de opcoes pro usuario).
	void SetEnabled(bool enabled) { m_enabled = enabled; }
	bool IsEnabled() const { return m_enabled; }

	// Chame isso UMA VEZ POR FRAME REAL, logo depois que o Cemu
	// terminou de desenhar o frame (backbuffer pronto), ANTES do
	// vkQueuePresentKHR real acontecer. Copia o conteudo do backbuffer
	// pra dentro do par atual/anterior usado pelo Optical Flow.
	//   commandBuffer  - command buffer ainda em recording (o mesmo
	//                    que o Cemu usa pra desenhar o frame)
	//   swapchainImage - a imagem do swapchain que sera apresentada
	//                    (chainInfo.m_swapchainImages[chainInfo.swapchainImageIndex])
	void CaptureBackbuffer(VkCommandBuffer commandBuffer, VkImage swapchainImage);

	// Chame isso LOGO APOS o present real (o vkQueuePresentKHR original
	// do Cemu ja ter sido chamado com sucesso para o frame real).
	// Faz: dispatch do Optical Flow + Frame Interpolation, adquire uma
	// nova imagem do swapchain, desenha o resultado nela e apresenta.
	//   chainInfo         - a SwapchainInfoVk do Cemu (TV ou DRC) que
	//                       acabou de apresentar o frame real
	//   presentQueue      - a fila de apresentacao (m_presentQueue do
	//                       VulkanRenderer)
	//   deltaTimeSeconds  - tempo entre o frame anterior e este
	//   reset             - true no primeiro frame ou corte de cena
	// Retorna false se pulou a geracao (desabilitado, ou ainda nao tem
	// 2 frames reais capturados pra interpolar entre eles).
	bool GenerateAndPresentInterpolatedFrame(SwapchainInfoVk& chainInfo, VkQueue presentQueue, float deltaTimeSeconds, bool reset);

private:
	void CreateBackbufferCopies(uint32_t width, uint32_t height);
	void DestroyAll();

	VulkanRenderer* m_renderer = nullptr;
	VKRMemoryManager* m_memoryManager = nullptr;

	std::unique_ptr<OpticalFlowVk> m_opticalFlow;
	std::unique_ptr<FrameInterpolationVk> m_frameInterpolation;

	uint32_t m_width = 0;
	uint32_t m_height = 0;
	bool m_enabled = true;

	// true depois que CaptureBackbuffer() foi chamado pelo menos 2x -
	// antes disso nao ha frame "anterior" pra interpolar.
	bool m_hasPreviousFrame = false;

	// par de copias do backbuffer (ping-pong entre atual/anterior a
	// cada chamada de CaptureBackbuffer)
	VkImage m_backbufferCopyImages[2] = {};
	VkDeviceMemory m_backbufferCopyMemory[2] = {};
	VkImageView m_backbufferCopyViews[2] = {};
	uint32_t m_currentCopyIndex = 0; // alterna entre 0 e 1 a cada captura

	// comando reutilizavel pra fazer o blit do resultado interpolado
	// dentro da imagem recem-adquirida do swapchain
	VkCommandPool m_commandPool = VK_NULL_HANDLE;
};
