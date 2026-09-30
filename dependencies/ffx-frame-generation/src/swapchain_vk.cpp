// swapchain_vk.cpp
//
// Ver o comentario no topo de swapchain_vk.h antes de mexer aqui.

#include "swapchain_vk.h"
#include "opticalflow_vk.h"
#include "frameinterpolation_vk.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanRenderer.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VKRMemoryManager.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/SwapchainInfoVk.h"

#include <stdexcept>

SwapchainFrameGenVk::SwapchainFrameGenVk(VulkanRenderer* renderer, uint32_t width, uint32_t height, uint32_t graphicsQueueFamilyIndex)
	: m_renderer(renderer), m_width(width), m_height(height)
{
	m_memoryManager = renderer->GetMemoryManager();

	m_opticalFlow = std::make_unique<OpticalFlowVk>(renderer, width, height);
	m_frameInterpolation = std::make_unique<FrameInterpolationVk>(renderer, width, height);

	CreateBackbufferCopies(width, height);

	VkCommandPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	poolInfo.queueFamilyIndex = graphicsQueueFamilyIndex;
	if (vkCreateCommandPool(m_renderer->GetLogicalDevice(), &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS)
		throw std::runtime_error("SwapchainFrameGenVk: falha ao criar command pool");
}

SwapchainFrameGenVk::~SwapchainFrameGenVk()
{
	DestroyAll();
}

void SwapchainFrameGenVk::CreateBackbufferCopies(uint32_t width, uint32_t height)
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
		// TODO: confirme que o formato do swapchain do Cemu e mesmo
		// B8G8R8A8_UNORM antes de compilar - se for outro formato
		// (ex. sRGB), troque aqui para bater exatamente, ou o
		// vkCmdCopyImage abaixo vai falhar validacao.
		imageInfo.format = VK_FORMAT_B8G8R8A8_UNORM;
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
		viewInfo.format = VK_FORMAT_B8G8R8A8_UNORM;
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

	// Transiciona a imagem de destino para TRANSFER_DST antes da copia
	// (na primeira chamada ela esta UNDEFINED; nas seguintes, ja fica
	// em SHADER_READ_ONLY de quando foi lida pelo Optical Flow - ver
	// TODO abaixo sobre isso nao estar 100% coberto ainda).
	VkImageMemoryBarrier toDst{};
	toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; // TODO: deveria alternar entre UNDEFINED (1a vez) e
	                                              // SHADER_READ_ONLY_OPTIMAL (demais vezes) - simplificado
	                                              // aqui usando UNDEFINED sempre, o que descarta o
	                                              // conteudo anterior da imagem em vez de preservar;
	                                              // como estamos SOBRESCREVENDO com vkCmdCopyImage logo
	                                              // em seguida mesmo, isso e inofensivo na pratica, so
	                                              // nao e o mais "correto" em termos de sincronizacao.
	toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toDst.image = destImage;
	toDst.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	toDst.srcAccessMask = 0;
	toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

	vkCmdPipelineBarrier(commandBuffer,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
		0, 0, nullptr, 0, nullptr, 1, &toDst);

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

bool SwapchainFrameGenVk::GenerateAndPresentInterpolatedFrame(SwapchainInfoVk& chainInfo, VkQueue presentQueue, float deltaTimeSeconds, bool reset)
{
	if (!m_enabled || !m_hasPreviousFrame)
		return false;

	VkDevice device = m_renderer->GetLogicalDevice();

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
	// TODO: isto assume que a imagem interpolada ja esta em
	// VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL no momento deste blit. No
	// FrameInterpolationVk::Dispatch() ela fica em GENERAL (layout de
	// storage image) apos o ultimo pass escrever nela - falta uma
	// transicao de GENERAL -> TRANSFER_SRC_OPTIMAL entre o Dispatch()
	// acima e este vkCmdBlitImage. Adicione essa barreira aqui antes de
	// testar em hardware real, ou o Vulkan Validation Layer vai acusar.

	VkImageMemoryBarrier toPresent = toDst;
	toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	toPresent.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	toPresent.dstAccessMask = 0;

	vkCmdPipelineBarrier(cb,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		0, 0, nullptr, 0, nullptr, 1, &toPresent);

	vkEndCommandBuffer(cb);

	// 5) Submete e apresenta
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

	// TODO: usar uma fence/semaforo proprio em vez de vkQueueWaitIdle -
	// isto FUNCIONA mas serializa a GPU (espera tudo terminar antes de
	// continuar), custando performance. Deixado simples de proposito
	// nesta primeira versao pra reduzir chance de bug de sincronizacao;
	// otimizar depois que estiver funcionando corretamente.
	vkQueueSubmit(presentQueue, 1, &submitInfo, VK_NULL_HANDLE);
	vkQueueWaitIdle(presentQueue);

	VkPresentInfoKHR presentInfo{};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &chainInfo.m_swapchain;
	presentInfo.pImageIndices = &imageIndex;

	VkResult result = vkQueuePresentKHR(presentQueue, &presentInfo);
	vkFreeCommandBuffers(device, m_commandPool, 1, &cb);

	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
		chainInfo.m_shouldRecreate = true;

	chainInfo.swapchainImageIndex = (uint32)-1;
	return result >= 0;
}

void SwapchainFrameGenVk::DestroyAll()
{
	VkDevice device = m_renderer->GetLogicalDevice();
	vkDeviceWaitIdle(device);

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
