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

#if defined(__ANDROID__) && !defined(VK_USE_PLATFORM_ANDROID_KHR)
#define VK_USE_PLATFORM_ANDROID_KHR
#endif

#include <vulkan/vulkan.h>
#include <cstdint>
#include <memory>
#include <chrono>

class VulkanRenderer;
class VKRMemoryManager;
class OpticalFlowVk;
class FrameInterpolationVk;
struct SwapchainInfoVk;

class SwapchainFrameGenVk
{
public:
	SwapchainFrameGenVk(VulkanRenderer* renderer, uint32_t width, uint32_t height, uint32_t graphicsQueueFamilyIndex, VkFormat backbufferFormat);
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
	//   reset             - true no primeiro frame ou corte de cena
	// O tempo entre frames (deltaTime) e calculado internamente via
	// relogio de parede (std::chrono) - nao precisa ser passado pelo
	// chamador, e assim reflete o tempo REAL de tela, nao o tempo
	// emulado do console (que e o que o resto do Cemu usa pra outras
	// coisas, mas nao e o que importa pro pacing da interpolacao).
	// Retorna false se pulou a geracao (desabilitado, ou ainda nao tem
	// 2 frames reais capturados pra interpolar entre eles).
	bool GenerateAndPresentInterpolatedFrame(SwapchainInfoVk& chainInfo, VkQueue presentQueue, bool reset);

private:
	void CreateBackbufferCopies(uint32_t width, uint32_t height, VkFormat backbufferFormat);
	void DestroyAll();

	VulkanRenderer* m_renderer = nullptr;
	VKRMemoryManager* m_memoryManager = nullptr;

	std::unique_ptr<OpticalFlowVk> m_opticalFlow;
	std::unique_ptr<FrameInterpolationVk> m_frameInterpolation;

	uint32_t m_width = 0;
	uint32_t m_height = 0;
	bool m_enabled = true;

	// Le a opcao fsr3_frame_gen da config A CADA FRAME (liga/desliga em runtime).
	// Ao ligar, descarta o historico antigo pra nao interpolar com frame velho.
	bool m_configWasOn = false;
	bool UpdateActiveState();

	// true depois que CaptureBackbuffer() foi chamado pelo menos 2x -
	// antes disso nao ha frame "anterior" pra interpolar.
	bool m_hasPreviousFrame = false;

	// par de copias do backbuffer (ping-pong entre atual/anterior a
	// cada chamada de CaptureBackbuffer)
	VkImage m_backbufferCopyImages[2] = {};
	VkDeviceMemory m_backbufferCopyMemory[2] = {};
	VkImageView m_backbufferCopyViews[2] = {};
	uint32_t m_currentCopyIndex = 0; // alterna entre 0 e 1 a cada captura

	// Controla se cada slot ja recebeu uma captura antes - usado pra
	// saber o layout de ORIGEM correto na transicao de CaptureBackbuffer:
	// UNDEFINED na primeira vez que o slot e usado, SHADER_READ_ONLY_OPTIMAL
	// nas vezes seguintes (ja que o Optical Flow leu dele como textura
	// no meio tempo).
	bool m_backbufferCopyInitialized[2] = { false, false };

	// comando reutilizavel pra fazer o blit do resultado interpolado
	// dentro da imagem recem-adquirida do swapchain
	VkCommandPool m_commandPool = VK_NULL_HANDLE;

	// Sincronizacao SEM bloquear a GPU inteira (ver GenerateAndPresentInterpolatedFrame):
	// fence sinaliza quando a GPU terminou de executar o command buffer
	// do frame gerado; semaforo e o que o present() espera antes de
	// mostrar a imagem na tela, no lugar de um vkQueueWaitIdle.
	VkFence m_inFlightFence = VK_NULL_HANDLE;
	VkSemaphore m_renderFinishedSemaphore = VK_NULL_HANDLE;
	VkCommandBuffer m_pendingCommandBuffer = VK_NULL_HANDLE; // liberado no INICIO da proxima chamada, depois de confirmar que a GPU ja terminou com ele

	// Relogio de parede pra calcular o deltaTime real entre dois
	// frames gerados (ver GenerateAndPresentInterpolatedFrame).
	std::chrono::steady_clock::time_point m_lastGeneratedFrameTime{};
	bool m_hasLastGeneratedFrameTime = false;
};
