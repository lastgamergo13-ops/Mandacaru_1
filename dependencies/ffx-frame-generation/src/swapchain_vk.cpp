// swapchain_vk.cpp
//
// Ver o comentario no topo de swapchain_vk.h antes de mexer aqui.

#include "swapchain_vk.h"
#include "opticalflow_vk.h"
#include "frameinterpolation_vk.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanRenderer.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VKRMemoryManager.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/SwapchainInfoVk.h"

#include "Cemu/Logging/CemuLogging.h"

#include <stdexcept>

SwapchainFrameGenVk::SwapchainFrameGenVk(VulkanRenderer* renderer, uint32_t width, uint32_t height, uint32_t graphicsQueueFamilyIndex, VkFormat backbufferFormat)
	: m_renderer(renderer), m_width(width), m_height(height)
{
	m_memoryManager = renderer->GetMemoryManager();

	m_opticalFlow = std::make_unique<OpticalFlowVk>(renderer, width, height);
	m_frameInterpolation = std::make_unique<FrameInterpolationVk>(renderer, width, height);

	CreateBackbufferCopies(width, height, backbufferFormat);
	cemuLog_log(LogType::Force, "[FrameGen] criado: {}x{}, formato backbuffer={}", width, height, (int)backbufferFormat);

	VkCommandPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	poolInfo.queueFamilyIndex = graphicsQueueFamilyIndex;
	if (vkCreateCommandPool(m_renderer->GetLogicalDevice(), &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS)
		throw std::runtime_error("SwapchainFrameGenVk: falha ao criar command pool");

	// Fence comeca SINALIZADA de proposito - na primeira chamada de
	// GenerateAndPresentInterpolatedFrame() nao existe frame anterior
	// pra esperar, entao o vkWaitForFences() ali precisa retornar na
	// hora em vez de travar esperando algo que nunca vai acontecer.
	VkFenceCreateInfo fenceInfo{};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
	if (vkCreateFence(m_renderer->GetLogicalDevice(), &fenceInfo, nullptr, &m_inFlightFence) != VK_SUCCESS)
		throw std::runtime_error("SwapchainFrameGenVk: falha ao criar fence");

	VkSemaphoreCreateInfo semaphoreInfo{};
	semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	if (vkCreateSemaphore(m_renderer->GetLogicalDevice(), &semaphoreInfo, nullptr, &m_renderFinishedSemaphore) != VK_SUCCESS)
		throw std::runtime_error("SwapchainFrameGenVk: falha ao criar semaforo");
}

SwapchainFrameGenVk::~SwapchainFrameGenVk()
{
	DestroyAll();
}

void SwapchainFrameGenVk::CreateBackbufferCopies(uint32_t width, uint32_t height, VkFormat backbufferFormat)
{
	VkDevice device = m_renderer->GetLogicalDevice();

	for (int i = 0; i < 2; i++)
	{
		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.extent = { width, height, 1 };
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.format = backbufferFormat; // agora vem do chainInfo.m_surfaceFormat.format real, nao mais um palpite fixo
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

		if (vkCreateImage(device, &imageInfo, nullptr, &m_backbufferCopyImages[i]) != VK_SUCCESS)
			throw std::runtime_error("SwapchainFrameGenVk: falha ao criar copia de backbuffer");

		VkMemoryRequirements memReq;
		vkGetImageMemoryRequirements(device, m_backbufferCopyImages[i], &memReq);

		uint32_t memoryTypeIndex = 0;
		if (!m_memoryManager->FindMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memoryTypeIndex))
			throw std::runtime_error("SwapchainFrameGenVk: nao achei memory type compativel");

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memReq.size;
		allocInfo.memoryTypeIndex = memoryTypeIndex;
		if (vkAllocateMemory(device, &allocInfo, nullptr, &m_backbufferCopyMemory[i]) != VK_SUCCESS)
			throw std::runtime_error("SwapchainFrameGenVk: vkAllocateMemory falhou");
		vkBindImageMemory(device, m_backbufferCopyImages[i], m_backbufferCopyMemory[i], 0);

		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = m_backbufferCopyImages[i];
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = backbufferFormat;
		viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		if (vkCreateImageView(device, &viewInfo, nullptr, &m_backbufferCopyViews[i]) != VK_SUCCESS)
			throw std::runtime_error("SwapchainFrameGenVk: falha ao criar image view da copia");
	}
}

void SwapchainFrameGenVk::CaptureBackbuffer(VkCommandBuffer commandBuffer, VkImage swapchainImage)
{
	if (!m_enabled)
		return;

	// Alterna o slot de destino - o slot que acabou de virar "atual"
	// nesta chamada sera o "anterior" na proxima.
	m_currentCopyIndex = 1 - m_currentCopyIndex;
	VkImage destImage = m_backbufferCopyImages[m_currentCopyIndex];

	// Transiciona a imagem de destino para TRANSFER_DST antes da copia.
	// Na primeira vez que este slot e usado ela esta UNDEFINED; nas
	// vezes seguintes ja fica em SHADER_READ_ONLY_OPTIMAL (de quando
	// foi lida como textura pelo Optical Flow no frame anterior) -
	// m_backbufferCopyInitialized[] rastreia qual dos dois casos vale
	// pra cada slot.
	bool slotAlreadyUsed = m_backbufferCopyInitialized[m_currentCopyIndex];

	VkImageMemoryBarrier toDst{};
	toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	toDst.oldLayout = slotAlreadyUsed ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
	toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toDst.image = destImage;
	toDst.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	toDst.srcAccessMask = slotAlreadyUsed ? VK_ACCESS_SHADER_READ_BIT : 0;
	toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

	vkCmdPipelineBarrier(commandBuffer,
		(slotAlreadyUsed ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT), VK_PIPELINE_STAGE_TRANSFER_BIT,
		0, 0, nullptr, 0, nullptr, 1, &toDst);

	m_backbufferCopyInitialized[m_currentCopyIndex] = true;

	VkImageCopy copyRegion{};
	copyRegion.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
	copyRegion.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
	copyRegion.extent = { m_width, m_height, 1 };

	// NOTA: isto assume que swapchainImage ja esta em
	// VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL (ou PRESENT_SRC_KHR, que
	// tambem e aceito como origem de copia na pratica em varios
	// drivers, mas nao e garantido pela spec) no momento desta
	// chamada. Se o Vulkan Validation Layer acusar erro de layout
	// aqui, adicione uma transicao equivalente a de destino acima,
	// mas para a swapchainImage, ANTES do vkCmdCopyImage.
	vkCmdCopyImage(commandBuffer,
		swapchainImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		destImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1, &copyRegion);

	// Transiciona a copia para SHADER_READ_ONLY, pronta pro Optical Flow ler
	VkImageMemoryBarrier toRead = toDst;
	toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

	vkCmdPipelineBarrier(commandBuffer,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		0, 0, nullptr, 0, nullptr, 1, &toRead);

	if (m_currentCopyIndex == 1)
		m_hasPreviousFrame = true; // depois de 2 capturas (indices 0 e 1), ja temos par completo
}

bool SwapchainFrameGenVk::GenerateAndPresentInterpolatedFrame(SwapchainInfoVk& chainInfo, VkQueue presentQueue, bool reset)
{
	if (!m_enabled || !m_hasPreviousFrame)
		return false;

	VkDevice device = m_renderer->GetLogicalDevice();

	// Calcula o tempo real (relogio de parede) desde a ultima vez que
	// geramos um frame - isso e o que da ao Frame Interpolation uma
	// nocao correta de "quanto tempo passou", em vez do valor fixo
	// 1/60 chutado nas versoes anteriores.
	auto now = std::chrono::steady_clock::now();
	float deltaTimeSeconds = 1.0f / 60.0f; // chute inicial, so usado na toa primeira chamada (sem referencia anterior)
	if (m_hasLastGeneratedFrameTime)
	{
		deltaTimeSeconds = std::chrono::duration<float>(now - m_lastGeneratedFrameTime).count();
		// Protecao contra valores absurdos (ex: app ficou em pausa/
		// background por varios segundos entre um frame e outro) -
		// sem isso, o Frame Interpolation tentaria "preencher" um
		// buraco de varios segundos como se fosse 1 frame, o que nao
		// faz sentido nenhum.
		if (deltaTimeSeconds <= 0.0f || deltaTimeSeconds > 0.5f)
			deltaTimeSeconds = 1.0f / 60.0f;
	}
	m_lastGeneratedFrameTime = now;
	m_hasLastGeneratedFrameTime = true;

	// 0) Espera a GPU terminar o command buffer do frame GERADO
	// anterior (se houver) antes de reutilizar/liberar ele - isto e o
	// que substitui o vkQueueWaitIdle antigo: em vez de travar a fila
	// inteira TODA VEZ, so esperamos (na pior das hipoteses) o
	// trabalho do frame gerado anterior, e normalmente nem isso, ja
	// que a GPU costuma ter terminado havia tempo quando chegamos
	// aqui de novo.
	if (m_pendingCommandBuffer != VK_NULL_HANDLE)
	{
		vkWaitForFences(device, 1, &m_inFlightFence, VK_TRUE, UINT64_MAX);
		vkFreeCommandBuffers(device, m_commandPool, 1, &m_pendingCommandBuffer);
		m_pendingCommandBuffer = VK_NULL_HANDLE;
	}
	vkResetFences(device, 1, &m_inFlightFence);

	// 1) Adquire uma NOVA imagem do swapchain pro frame interpolado
	//    (reaproveita o metodo que o proprio Cemu ja usa pro frame real)
	if (!chainInfo.AcquireImage())
		return false; // swapchain fora de data ou minimizado - pula este frame gerado

	VkSemaphore acquireSemaphore = chainInfo.ConsumeAcquireSemaphore();
	uint32_t imageIndex = chainInfo.swapchainImageIndex;
	VkImage targetImage = chainInfo.m_swapchainImages[imageIndex];

	// 2) Grava um command buffer proprio (fora do command buffer
	// principal do Cemu, que ja foi submetido junto com o frame real)
	VkCommandBufferAllocateInfo cbAllocInfo{};
	cbAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cbAllocInfo.commandPool = m_commandPool;
	cbAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cbAllocInfo.commandBufferCount = 1;

	VkCommandBuffer cb;
	vkAllocateCommandBuffers(device, &cbAllocInfo, &cb);

	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer(cb, &beginInfo);

	uint32_t currentIdx = m_currentCopyIndex;
	uint32_t previousIdx = 1 - m_currentCopyIndex;

	m_opticalFlow->Dispatch(cb, m_backbufferCopyViews[currentIdx], m_backbufferCopyViews[previousIdx], reset);
	m_frameInterpolation->Dispatch(cb, m_backbufferCopyViews[currentIdx], m_backbufferCopyViews[previousIdx], *m_opticalFlow, deltaTimeSeconds, reset);

	// 3) Transiciona a imagem recem-adquirida do swapchain para receber o blit
	VkImageMemoryBarrier toDst{};
	toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toDst.image = targetImage;
	toDst.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	toDst.srcAccessMask = 0;
	toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

	vkCmdPipelineBarrier(cb,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
		0, 0, nullptr, 0, nullptr, 1, &toDst);

	// Transiciona a imagem interpolada de GENERAL (layout de storage
	// image, onde o ultimo pass do FrameInterpolationVk a deixou apos
	// escrever nela) para TRANSFER_SRC_OPTIMAL, exigido como origem de
	// um vkCmdBlitImage.
	VkImageMemoryBarrier interpolatedToSrc{};
	interpolatedToSrc.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	interpolatedToSrc.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
	interpolatedToSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	interpolatedToSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	interpolatedToSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	interpolatedToSrc.image = m_frameInterpolation->GetInterpolatedFrameImage();
	interpolatedToSrc.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	interpolatedToSrc.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	interpolatedToSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

	vkCmdPipelineBarrier(cb,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
		0, 0, nullptr, 0, nullptr, 1, &interpolatedToSrc);

	// 4) Blita o frame interpolado (RGBA16F) pra dentro da imagem do
	// swapchain (normalmente B8G8R8A8_UNORM) - usa blit em vez de copy
	// porque os formatos sao diferentes; se os formatos baterem no seu
	// caso, vkCmdCopyImage e mais barato que blit.
	VkImageBlit blitRegion{};
	blitRegion.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
	blitRegion.srcOffsets[1] = { (int32_t)m_width, (int32_t)m_height, 1 };
	blitRegion.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
	blitRegion.dstOffsets[1] = { (int32_t)m_width, (int32_t)m_height, 1 };

	vkCmdBlitImage(cb,
		m_frameInterpolation->GetInterpolatedFrameImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		targetImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1, &blitRegion, VK_FILTER_LINEAR);

	// Devolve a imagem interpolada pra GENERAL, pronta pro proximo
	// Dispatch() do FrameInterpolationVk escrever nela de novo no
	// proximo frame gerado.
	VkImageMemoryBarrier interpolatedBackToGeneral = interpolatedToSrc;
	interpolatedBackToGeneral.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	interpolatedBackToGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;
	interpolatedBackToGeneral.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	interpolatedBackToGeneral.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

	vkCmdPipelineBarrier(cb,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		0, 0, nullptr, 0, nullptr, 1, &interpolatedBackToGeneral);

	VkImageMemoryBarrier toPresent = toDst;
	toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	toPresent.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	toPresent.dstAccessMask = 0;

	vkCmdPipelineBarrier(cb,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		0, 0, nullptr, 0, nullptr, 1, &toPresent);

	vkEndCommandBuffer(cb);

	// 5) Submete (sinalizando m_renderFinishedSemaphore e m_inFlightFence
	// em vez de bloquear a CPU esperando a GPU) e apresenta assim que o
	// present() puder (esperando so o semaforo, nao a fila inteira).
	VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &cb;
	if (acquireSemaphore != VK_NULL_HANDLE)
	{
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &acquireSemaphore;
		submitInfo.pWaitDstStageMask = &waitStage;
	}
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &m_renderFinishedSemaphore;

	VkResult submitResult = vkQueueSubmit(presentQueue, 1, &submitInfo, m_inFlightFence);

	VkPresentInfoKHR presentInfo{};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &m_renderFinishedSemaphore;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &chainInfo.m_swapchain;
	presentInfo.pImageIndices = &imageIndex;

	VkResult result = vkQueuePresentKHR(presentQueue, &presentInfo);

	// Diagnostico: loga os 5 primeiros frames gerados e depois 1 a cada 300.
	{
		static uint32_t s_generatedCount = 0;
		s_generatedCount++;
		if (s_generatedCount <= 5 || (s_generatedCount % 300) == 0)
			cemuLog_log(LogType::Force, "[FrameGen] frame gerado #{} dt={:.4f}s submit={} present={}", s_generatedCount, deltaTimeSeconds, (int)submitResult, (int)result);
	}

	// NAO libera o command buffer agora - a GPU pode ainda estar
	// executando ele. Guarda ele em m_pendingCommandBuffer e libera no
	// INICIO da proxima chamada (passo 0 acima), depois de confirmar
	// via m_inFlightFence que a GPU realmente terminou.
	m_pendingCommandBuffer = cb;

	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
		chainInfo.m_shouldRecreate = true;

	chainInfo.swapchainImageIndex = (uint32)-1;
	return result >= 0;
}

void SwapchainFrameGenVk::DestroyAll()
{
	VkDevice device = m_renderer->GetLogicalDevice();
	vkDeviceWaitIdle(device); // garante que a GPU ja terminou tudo, inclusive o ultimo m_pendingCommandBuffer

	if (m_pendingCommandBuffer != VK_NULL_HANDLE)
	{
		vkFreeCommandBuffers(device, m_commandPool, 1, &m_pendingCommandBuffer);
		m_pendingCommandBuffer = VK_NULL_HANDLE;
	}
	if (m_inFlightFence) vkDestroyFence(device, m_inFlightFence, nullptr);
	if (m_renderFinishedSemaphore) vkDestroySemaphore(device, m_renderFinishedSemaphore, nullptr);

	m_frameInterpolation.reset();
	m_opticalFlow.reset();

	for (int i = 0; i < 2; i++)
	{
		if (m_backbufferCopyViews[i]) vkDestroyImageView(device, m_backbufferCopyViews[i], nullptr);
		if (m_backbufferCopyImages[i]) vkDestroyImage(device, m_backbufferCopyImages[i], nullptr);
		if (m_backbufferCopyMemory[i]) vkFreeMemory(device, m_backbufferCopyMemory[i], nullptr);
	}

	if (m_commandPool) vkDestroyCommandPool(device, m_commandPool, nullptr);
}
