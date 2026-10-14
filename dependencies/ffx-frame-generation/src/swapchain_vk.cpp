// swapchain_vk.cpp
//
// Ver o comentario no topo de swapchain_vk.h antes de mexer aqui.

#include "swapchain_vk.h"
#include "opticalflow_vk.h"
#include "frameinterpolation_vk.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanRenderer.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VKRMemoryManager.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/SwapchainInfoVk.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/CrashCrumb.h"

#include "Cemu/Logging/CemuLogging.h"
#include "config/CemuConfig.h"
#include "Cafe/HW/Latte/Core/FrameGenStats.h"

#include "Cafe/HW/Latte/Core/FrameGenSettings.h"

#include <stdexcept>
#include <thread>
#include <vector>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>

// ---------------------------------------------------------------------------------------------
// Thread de apresentacao dos frames gerados
// ---------------------------------------------------------------------------------------------
std::mutex& FrameGenQueueMutex()
{
	static std::mutex s_mutex;
	return s_mutex;
}

namespace
{
	struct PresentJob
	{
		VkQueue queue = VK_NULL_HANDLE;
		VkSwapchainKHR swapchain = VK_NULL_HANDLE;
		uint32_t imageIndex = 0;
		VkSemaphore waitSemaphore = VK_NULL_HANDLE;
		std::chrono::steady_clock::time_point when{};
	};

	class FrameGenPresenter
	{
	public:
		static FrameGenPresenter& Get()
		{
			static FrameGenPresenter s_instance;
			return s_instance;
		}

		~FrameGenPresenter()
		{
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_stop = true;
			}
			m_cvWork.notify_all();
			if (m_thread.joinable())
				m_thread.join();
		}

		void Enqueue(const PresentJob& job)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (!m_thread.joinable())
				m_thread = std::thread(&FrameGenPresenter::Run, this);
			m_queue.push_back(job);
			m_cvWork.notify_one();
		}

		void WaitIdle()
		{
			std::unique_lock<std::mutex> lock(m_mutex);
			m_cvIdle.wait(lock, [&] { return m_queue.empty() && !m_busy; });
		}

		bool ConsumeOutOfDate() { return m_outOfDate.exchange(false); }
		int LastResult() const { return m_lastResult.load(); }

	private:
		void Run()
		{
			std::unique_lock<std::mutex> lock(m_mutex);
			while (true)
			{
				m_cvWork.wait(lock, [&] { return m_stop || !m_queue.empty(); });
				if (m_stop && m_queue.empty())
					return;
				PresentJob job = m_queue.front();
				m_queue.pop_front();
				m_busy = true;
				lock.unlock();

				if (job.when > std::chrono::steady_clock::now())
					std::this_thread::sleep_until(job.when);

				VkPresentInfoKHR presentInfo{};
				presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
				presentInfo.waitSemaphoreCount = 1;
				presentInfo.pWaitSemaphores = &job.waitSemaphore;
				presentInfo.swapchainCount = 1;
				presentInfo.pSwapchains = &job.swapchain;
				presentInfo.pImageIndices = &job.imageIndex;
				CRUMB("FrameGen: vkQueuePresentKHR (frame gerado, thread de apresentacao)");
				VkResult result;
				{
					std::lock_guard<std::mutex> qlock(FrameGenQueueMutex());
					result = vkQueuePresentKHR(job.queue, &presentInfo);
				}
				m_lastResult.store((int)result);
				if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
					cemuLog_log(LogType::Force, "[FrameGen] ERRO vkQueuePresentKHR do frame gerado: {}", (int)result);
				if (result == VK_ERROR_OUT_OF_DATE_KHR)
					m_outOfDate.store(true);

				lock.lock();
				m_busy = false;
				m_cvIdle.notify_all();
			}
		}

		std::mutex m_mutex;
		std::condition_variable m_cvWork, m_cvIdle;
		std::deque<PresentJob> m_queue;
		bool m_busy = false;
		bool m_stop = false;
		std::thread m_thread;
		std::atomic<bool> m_outOfDate{ false };
		std::atomic<int> m_lastResult{ 0 };
	};
}

void FrameGenPresenterWaitIdle()
{
	FrameGenPresenter::Get().WaitIdle();
}

SwapchainFrameGenVk::SwapchainFrameGenVk(VulkanRenderer* renderer, uint32_t width, uint32_t height, uint32_t graphicsQueueFamilyIndex, VkFormat backbufferFormat)
	: m_renderer(renderer), m_width(width), m_height(height)
{
	m_memoryManager = renderer->GetMemoryManager();

	m_backbufferFormat = backbufferFormat;
	m_frameInterpolation = std::make_unique<FrameInterpolationVk>(renderer, width, height);
	CreateBackbufferCopies(width, height, backbufferFormat);
	CreateOpticalFlow(FrameGenSettings::Get().flowScalePercent);
	cemuLog_log(LogType::Force, "[FrameGen] criado: {}x{}, formato backbuffer={}, optical flow {}x{} ({}%), extrapolacao={}",
		width, height, (int)backbufferFormat, m_flowWidth, m_flowHeight, m_appliedFlowScalePercent, m_frameInterpolation->SupportsExtrapolation() ? "sim" : "nao (shader v7 ausente)");

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
	for (uint32_t i = 0; i < kSemaphoreRing; i++)
	{
		if (vkCreateSemaphore(m_renderer->GetLogicalDevice(), &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]) != VK_SUCCESS)
			throw std::runtime_error("SwapchainFrameGenVk: falha ao criar semaforo");
	}
	CreateTimingQueries(graphicsQueueFamilyIndex);
}

void SwapchainFrameGenVk::CreateTimingQueries(uint32_t graphicsQueueFamilyIndex)
{
	VkPhysicalDevice phys = m_renderer->GetPhysicalDevice();
	uint32_t count = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(phys, &count, nullptr);
	std::vector<VkQueueFamilyProperties> props(count);
	vkGetPhysicalDeviceQueueFamilyProperties(phys, &count, props.data());
	if (graphicsQueueFamilyIndex >= count || props[graphicsQueueFamilyIndex].timestampValidBits == 0)
		return; // sem suporte a timestamp: so nao mede
	const uint32_t bits = props[graphicsQueueFamilyIndex].timestampValidBits;
	m_timestampMask = bits >= 64 ? ~0ull : ((1ull << bits) - 1ull);
	VkPhysicalDeviceProperties devProps{};
	vkGetPhysicalDeviceProperties(phys, &devProps);
	m_timestampPeriodNs = devProps.limits.timestampPeriod;
	VkQueryPoolCreateInfo qi{};
	qi.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
	qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
	qi.queryCount = 2;
	if (vkCreateQueryPool(m_renderer->GetLogicalDevice(), &qi, nullptr, &m_queryPool) != VK_SUCCESS)
		m_queryPool = VK_NULL_HANDLE;
}

// Le o resultado do command buffer gerado ANTERIOR (a fence ja garantiu que ele terminou).
void SwapchainFrameGenVk::CollectTiming()
{
	if (!m_queryPending || m_queryPool == VK_NULL_HANDLE)
		return;
	m_queryPending = false;
	uint64_t ts[2] = {};
	if (vkGetQueryPoolResults(m_renderer->GetLogicalDevice(), m_queryPool, 0, 2, sizeof(ts), ts, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) != VK_SUCCESS)
		return;
	const uint64_t delta = ((ts[1] & m_timestampMask) - (ts[0] & m_timestampMask)) & m_timestampMask;
	const double ms = (double)delta * m_timestampPeriodNs / 1.0e6;
	if (ms < 0.0 || ms > 1000.0)
		return;
	m_timingSumMs += ms;
	m_timingMaxMs = std::max(m_timingMaxMs, ms);
	if (++m_timingCount >= 120)
	{
		FrameGenStats::gpuMs.store((float)(m_timingSumMs / m_timingCount), std::memory_order_relaxed);
		cemuLog_log(LogType::Force, "[FrameGen] custo de GPU do frame gerado (OF+interpolacao): media {:.2f} ms, max {:.2f} ms ({} amostras)", m_timingSumMs / m_timingCount, m_timingMaxMs, m_timingCount);
		m_timingSumMs = 0.0;
		m_timingMaxMs = 0.0;
		m_timingCount = 0;
	}
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

bool SwapchainFrameGenVk::UpdateActiveState()
{
	// Valores efetivos: perfil do jogo (se definido) senao a config global. Lidos todo frame.
	const FrameGenSettings::Values cfg = FrameGenSettings::Get();
	const bool configOn = cfg.enabled;
	if (configOn != m_configWasOn)
	{
		cemuLog_log(LogType::Force, "[FrameGen] {}", configOn ? "ativado" : "desativado");
		if (configOn)
		{
			// recomeca o historico: as 2 proximas capturas viram (anterior, atual)
			m_hasPreviousFrame = false;
			m_hasLastGeneratedFrameTime = false;
			m_currentCopyIndex = 1;
			m_lastRealCaptureTime = {};
			m_limiterNext = {};
		}
		m_configWasOn = configOn;
	}
	if (configOn)
	{
		if (cfg.multiplier != m_multiplier || cfg.baseFpsLimit != m_baseFpsLimit)
			cemuLog_log(LogType::Force, "[FrameGen] multiplicador={}x, limite de FPS base={}", cfg.multiplier, cfg.baseFpsLimit);
		m_multiplier = cfg.multiplier;
		m_baseFpsLimit = cfg.baseFpsLimit;
		if (cfg.flowScalePercent != m_appliedFlowScalePercent || cfg.pyramid != m_appliedPyramid)
			ApplyFlowScale(cfg.flowScalePercent);
	}
	const bool isActive = configOn && m_enabled;
	if (!isActive)
	{
		FrameGenStats::active.store(false, std::memory_order_relaxed);
		FrameGenStats::realFps.store(0.0f, std::memory_order_relaxed);
		FrameGenStats::generatedFps.store(0.0f, std::memory_order_relaxed);
		FrameGenStats::gpuMs.store(0.0f, std::memory_order_relaxed);
		m_statRealCount = m_statGenCount = 0;
		m_statWindowStart = {};
	}
	return isActive;
}

void SwapchainFrameGenVk::CaptureBackbuffer(VkCommandBuffer commandBuffer, VkImage swapchainImage)
{
	if (!UpdateActiveState())
		return;
	// garante que o frame gerado anterior ja foi apresentado ANTES do proximo frame real (ordem na tela)
	FrameGenPresenterWaitIdle();
	TickStats(true);

	// intervalo medio entre frames reais (usado no pacing dos frames gerados)
	{
		const auto nowCap = std::chrono::steady_clock::now();
		if (m_lastRealCaptureTime != std::chrono::steady_clock::time_point{})
		{
			double sample = std::chrono::duration<double>(nowCap - m_lastRealCaptureTime).count();
			m_lastRawInterval = sample;
			sample = std::clamp(sample, 1.0 / 240.0, 0.2);
			m_realFrameInterval = m_realFrameInterval * 0.8 + sample * 0.2;
		}
		m_lastRealCaptureTime = nowCap;
	}

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

	// Flow scale < 100%: gera tambem a copia REDUZIDA (blit linear) que alimenta o Optical Flow.
	if (m_appliedFlowScalePercent < 100 && m_scaledImages[m_currentCopyIndex] != VK_NULL_HANDLE)
	{
		const uint32_t si = m_currentCopyIndex;
		const bool used = m_scaledInitialized[si];
		VkImageMemoryBarrier sToDst{};
		sToDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		sToDst.oldLayout = used ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
		sToDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		sToDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		sToDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		sToDst.image = m_scaledImages[si];
		sToDst.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		sToDst.srcAccessMask = used ? VK_ACCESS_SHADER_READ_BIT : 0;
		sToDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		vkCmdPipelineBarrier(commandBuffer,
			(used ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT), VK_PIPELINE_STAGE_TRANSFER_BIT,
			0, 0, nullptr, 0, nullptr, 1, &sToDst);
		m_scaledInitialized[si] = true;

		VkImageBlit blit{};
		blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		blit.srcOffsets[1] = { (int32_t)m_width, (int32_t)m_height, 1 };
		blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		blit.dstOffsets[1] = { (int32_t)m_flowWidth, (int32_t)m_flowHeight, 1 };
		vkCmdBlitImage(commandBuffer,
			swapchainImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			m_scaledImages[si], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &blit, VK_FILTER_LINEAR);

		VkImageMemoryBarrier sToRead = sToDst;
		sToRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		sToRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		sToRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		sToRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		vkCmdPipelineBarrier(commandBuffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			0, 0, nullptr, 0, nullptr, 1, &sToRead);
	}

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
	if (!UpdateActiveState() || !m_hasPreviousFrame)
		return false;

	// Travada/loading (frame real demorou > 120 ms): nao gera frame extra. Gerar durante carregamento so
	// soma carga de GPU (o crash do Mario Kart 8 foi a ~1.7 FPS) e o movimento nao e confiavel.
	if (m_lastRawInterval > 0.12)
		return false;

	const int multiplier = std::clamp(m_multiplier, 2, 4);
	// Referencia de tempo: quando o frame real atual foi capturado (logo antes de ser apresentado).
	// Os frames gerados saem espacados dentro do intervalo medio entre frames reais.
	const auto t0 = (m_lastRealCaptureTime != std::chrono::steady_clock::time_point{}) ? m_lastRealCaptureTime : std::chrono::steady_clock::now();
	bool anyPresented = false;
	for (int k = 1; k < multiplier; k++)
	{
		const float progress = (float)k / (float)multiplier; // posicao dentro do intervalo
		const auto target = t0 + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(m_realFrameInterval * progress));
		// NAO dorme aqui: a thread do Latte precisa seguir para o proximo frame real. O horario (target) vai para a
		// thread de apresentacao, que espera e chama o vkQueuePresentKHR.
		if (!PresentGeneratedFrame(chainInfo, presentQueue, reset && k == 1, k == 1, progress, target))
			break;
		anyPresented = true;
	}
	ApplyBaseFpsLimit();
	return anyPresented;
}

// Limita o FPS REAL do jogo (antes de gerar): dorme o que faltar ate completar o periodo.
void SwapchainFrameGenVk::ApplyBaseFpsLimit()
{
	if (m_baseFpsLimit <= 0)
	{
		m_limiterNext = {};
		return;
	}
	const auto period = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(1.0 / (double)m_baseFpsLimit));
	const auto now = std::chrono::steady_clock::now();
	if (m_limiterNext == std::chrono::steady_clock::time_point{})
		m_limiterNext = now;
	m_limiterNext += period;
	if (m_limiterNext > now)
		std::this_thread::sleep_until(m_limiterNext);
	else
		m_limiterNext = now; // atrasou: ressincroniza em vez de tentar "compensar"
}

bool SwapchainFrameGenVk::PresentGeneratedFrame(SwapchainInfoVk& chainInfo, VkQueue presentQueue, bool reset, bool runOpticalFlow, float progress, std::chrono::steady_clock::time_point presentAt)
{
	if (FrameGenPresenter::Get().ConsumeOutOfDate())
		chainInfo.m_shouldRecreate = true;
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
		CollectTiming();
	}
	vkResetFences(device, 1, &m_inFlightFence);

	// 1) Adquire uma NOVA imagem do swapchain pro frame interpolado
	//    (reaproveita o metodo que o proprio Cemu ja usa pro frame real)
	if (!chainInfo.AcquireImage())
		return false; // swapchain fora de data ou minimizado - pula este frame gerado

	// Igual ao fluxo normal do Cemu (VulkanRenderer::SwapBuffers): espera o fence do acquire. Sem isso
	// o fence ficava "pendente" e o proximo AcquireImage() do frame REAL fazia vkResetFences nele
	// ainda em uso (comportamento indefinido -> VK_ERROR_DEVICE_LOST no Adreno sob carga).
	chainInfo.WaitAvailableFence();

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
	const bool timing = (m_queryPool != VK_NULL_HANDLE);
	if (timing)
	{
		vkCmdResetQueryPool(cb, m_queryPool, 0, 2);
		vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_queryPool, 0);
	}

	uint32_t currentIdx = m_currentCopyIndex;
	uint32_t previousIdx = 1 - m_currentCopyIndex;

	if (runOpticalFlow)
	{
		// Flow scale < 100%: o Optical Flow le as copias REDUZIDAS (m_scaledViews).
		const bool scaled = m_appliedFlowScalePercent < 100 && m_scaledViews[currentIdx] != VK_NULL_HANDLE;
		CRUMB("FrameGen: gravando OpticalFlow");
	m_opticalFlow->Dispatch(cb,
			scaled ? m_scaledViews[currentIdx] : m_backbufferCopyViews[currentIdx],
			scaled ? m_scaledViews[previousIdx] : m_backbufferCopyViews[previousIdx], reset);
	}
	else
	{
		// frames extras (multiplicador > 2): reaproveita os vetores ja calculados neste frame real.
		// Barreira p/ garantir que as escritas do OF (submit anterior) estao visiveis ao FI.
		VkMemoryBarrier mb{};
		mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			0, 1, &mb, 0, nullptr, 0, nullptr);
	}
	CRUMB("FrameGen: gravando Interpolacao");
	m_frameInterpolation->Dispatch(cb, m_backbufferCopyViews[currentIdx], m_backbufferCopyViews[previousIdx], *m_opticalFlow, deltaTimeSeconds, reset, progress);
	if (timing)
		vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, m_queryPool, 1);

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

	// srcStage = TRANSFER: tem que bater com o waitStage do semaforo de acquire (abaixo), senao a
	// transicao/blit podia comecar ANTES da imagem do swapchain estar realmente liberada.
	vkCmdPipelineBarrier(cb,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
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
	// O primeiro uso da imagem adquirida e o blit (TRANSFER), nao color attachment.
	VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
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
	VkSemaphore renderFinishedSemaphore = m_renderFinishedSemaphores[m_semaphoreIndex];
	m_semaphoreIndex = (m_semaphoreIndex + 1) % kSemaphoreRing;
	submitInfo.pSignalSemaphores = &renderFinishedSemaphore;

	CRUMB("FrameGen: vkQueueSubmit (frame gerado)");
	VkResult submitResult;
	{
		std::lock_guard<std::mutex> qlock(FrameGenQueueMutex());
		submitResult = vkQueueSubmit(presentQueue, 1, &submitInfo, m_inFlightFence);
	}
	if (submitResult != VK_SUCCESS)
		cemuLog_log(LogType::Force, "[FrameGen] ERRO vkQueueSubmit do frame gerado: {}", (int)submitResult);

	// O present (que espera o horario) vai para a thread de apresentacao - a thread do Latte nao bloqueia.
	PresentJob job;
	job.queue = presentQueue;
	job.swapchain = chainInfo.m_swapchain;
	job.imageIndex = imageIndex;
	job.waitSemaphore = renderFinishedSemaphore;
	job.when = presentAt;
	if (submitResult == VK_SUCCESS)
		FrameGenPresenter::Get().Enqueue(job);
	const VkResult result = (submitResult == VK_SUCCESS) ? VK_SUCCESS : submitResult;

	if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR)
		TickStats(false);

	// Diagnostico: loga os 5 primeiros frames gerados e depois 1 a cada 300.
	{
		static uint32_t s_generatedCount = 0;
		s_generatedCount++;
		if (s_generatedCount <= 5 || (s_generatedCount % 300) == 0)
			cemuLog_log(LogType::Force, "[FrameGen] frame gerado #{} dt={:.4f}s submit={} present=enfileirado(thread)", s_generatedCount, deltaTimeSeconds, (int)submitResult);
	}

	// NAO libera o command buffer agora - a GPU pode ainda estar
	// executando ele. Guarda ele em m_pendingCommandBuffer e libera no
	// INICIO da proxima chamada (passo 0 acima), depois de confirmar
	// via m_inFlightFence que a GPU realmente terminou.
	m_pendingCommandBuffer = cb;
	m_queryPending = timing && submitResult == VK_SUCCESS;

	// SUBOPTIMAL nao exige recriar o swapchain (no Adreno ele aparece em TODO present e recriar a
	// cada frame destroi o desempenho); so OUT_OF_DATE exige.
	chainInfo.swapchainImageIndex = (uint32)-1;
	return result >= 0;
}

void SwapchainFrameGenVk::DestroyAll()
{
	FrameGenPresenterWaitIdle();
	VkDevice device = m_renderer->GetLogicalDevice();
	vkDeviceWaitIdle(device); // garante que a GPU ja terminou tudo, inclusive o ultimo m_pendingCommandBuffer

	if (m_pendingCommandBuffer != VK_NULL_HANDLE)
	{
		vkFreeCommandBuffers(device, m_commandPool, 1, &m_pendingCommandBuffer);
		m_pendingCommandBuffer = VK_NULL_HANDLE;
	}
	if (m_inFlightFence) vkDestroyFence(device, m_inFlightFence, nullptr);
	for (uint32_t i = 0; i < kSemaphoreRing; i++)
		if (m_renderFinishedSemaphores[i]) vkDestroySemaphore(device, m_renderFinishedSemaphores[i], nullptr);

	m_frameInterpolation.reset();
	m_opticalFlow.reset();
	DestroyScaledImages();

	for (int i = 0; i < 2; i++)
	{
		if (m_backbufferCopyViews[i]) vkDestroyImageView(device, m_backbufferCopyViews[i], nullptr);
		if (m_backbufferCopyImages[i]) vkDestroyImage(device, m_backbufferCopyImages[i], nullptr);
		if (m_backbufferCopyMemory[i]) vkFreeMemory(device, m_backbufferCopyMemory[i], nullptr);
	}

	if (m_queryPool) vkDestroyQueryPool(device, m_queryPool, nullptr);
	if (m_commandPool) vkDestroyCommandPool(device, m_commandPool, nullptr);
}


// Contador de FPS: conta frames reais (CaptureBackbuffer) e gerados (present do interpolado)
// numa janela de ~1s, publica em FrameGenStats (overlay) e escreve 1 linha no log por janela.
void SwapchainFrameGenVk::TickStats(bool realFrame)
{
	const auto now = std::chrono::steady_clock::now();
	if (m_statWindowStart == std::chrono::steady_clock::time_point{})
		m_statWindowStart = now;
	if (realFrame)
		m_statRealCount++;
	else
		m_statGenCount++;
	const double elapsed = std::chrono::duration<double>(now - m_statWindowStart).count();
	if (elapsed < 1.0)
		return;
	const float real = (float)(m_statRealCount / elapsed);
	const float gen = (float)(m_statGenCount / elapsed);
	FrameGenStats::active.store(true, std::memory_order_relaxed);
	FrameGenStats::realFps.store(real, std::memory_order_relaxed);
	FrameGenStats::generatedFps.store(gen, std::memory_order_relaxed);
	cemuLog_log(LogType::Force, "[FrameGen] FPS real={:.1f} gerado={:.1f} total={:.1f}", real, gen, real + gen);
	m_statRealCount = 0;
	m_statGenCount = 0;
	m_statWindowStart = now;
}


// ---------------------------------------------------------------------------------------------
// Flow scale
// ---------------------------------------------------------------------------------------------
void SwapchainFrameGenVk::CreateOpticalFlow(int flowScalePercent)
{
	flowScalePercent = std::clamp(flowScalePercent, 25, 100);
	if (flowScalePercent >= 100)
	{
		m_flowWidth = m_width;
		m_flowHeight = m_height;
		flowScalePercent = 100;
	}
	else
	{
		// multiplo de 8 (tamanho do bloco do Optical Flow), minimo 64
		m_flowWidth = std::max<uint32_t>(64, ((m_width * (uint32_t)flowScalePercent) / 100) & ~7u);
		m_flowHeight = std::max<uint32_t>(64, ((m_height * (uint32_t)flowScalePercent) / 100) & ~7u);
	}
	m_appliedFlowScalePercent = flowScalePercent;
	m_appliedPyramid = FrameGenSettings::Get().pyramid;
	m_opticalFlow = std::make_unique<OpticalFlowVk>(m_renderer, m_flowWidth, m_flowHeight, m_appliedPyramid);
	if (m_appliedPyramid && !m_opticalFlow->IsPyramidActive())
		cemuLog_log(LogType::Force, "[FrameGen] piramide pedida mas indisponivel: usando Optical Flow simples");
	DestroyScaledImages();
	if (flowScalePercent < 100)
		CreateScaledImages();
}

void SwapchainFrameGenVk::CreateScaledImages()
{
	VkDevice device = m_renderer->GetLogicalDevice();
	for (int i = 0; i < 2; i++)
	{
		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.extent = { m_flowWidth, m_flowHeight, 1 };
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.format = m_backbufferFormat;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		if (vkCreateImage(device, &imageInfo, nullptr, &m_scaledImages[i]) != VK_SUCCESS)
			throw std::runtime_error("SwapchainFrameGenVk: falha ao criar imagem reduzida (flow scale)");

		VkMemoryRequirements memReq;
		vkGetImageMemoryRequirements(device, m_scaledImages[i], &memReq);
		uint32_t memoryTypeIndex = 0;
		if (!m_memoryManager->FindMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memoryTypeIndex))
			throw std::runtime_error("SwapchainFrameGenVk: memory type p/ imagem reduzida nao encontrado");
		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memReq.size;
		allocInfo.memoryTypeIndex = memoryTypeIndex;
		if (vkAllocateMemory(device, &allocInfo, nullptr, &m_scaledMemory[i]) != VK_SUCCESS)
			throw std::runtime_error("SwapchainFrameGenVk: vkAllocateMemory (imagem reduzida) falhou");
		vkBindImageMemory(device, m_scaledImages[i], m_scaledMemory[i], 0);

		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = m_scaledImages[i];
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = m_backbufferFormat;
		viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		if (vkCreateImageView(device, &viewInfo, nullptr, &m_scaledViews[i]) != VK_SUCCESS)
			throw std::runtime_error("SwapchainFrameGenVk: falha ao criar view da imagem reduzida");
		m_scaledInitialized[i] = false;
	}
}

void SwapchainFrameGenVk::DestroyScaledImages()
{
	VkDevice device = m_renderer->GetLogicalDevice();
	for (int i = 0; i < 2; i++)
	{
		if (m_scaledViews[i]) vkDestroyImageView(device, m_scaledViews[i], nullptr);
		if (m_scaledImages[i]) vkDestroyImage(device, m_scaledImages[i], nullptr);
		if (m_scaledMemory[i]) vkFreeMemory(device, m_scaledMemory[i], nullptr);
		m_scaledViews[i] = VK_NULL_HANDLE;
		m_scaledImages[i] = VK_NULL_HANDLE;
		m_scaledMemory[i] = VK_NULL_HANDLE;
		m_scaledInitialized[i] = false;
	}
}

// Troca o flow scale em runtime: recria o Optical Flow e as imagens reduzidas e recomeca o historico.
void SwapchainFrameGenVk::ApplyFlowScale(int flowScalePercent)
{
	VkDevice device = m_renderer->GetLogicalDevice();
	vkDeviceWaitIdle(device);
	if (m_pendingCommandBuffer != VK_NULL_HANDLE)
	{
		vkFreeCommandBuffers(device, m_commandPool, 1, &m_pendingCommandBuffer);
		m_pendingCommandBuffer = VK_NULL_HANDLE;
	}
	m_opticalFlow.reset();
	CreateOpticalFlow(flowScalePercent);
	m_hasPreviousFrame = false;
	m_currentCopyIndex = 1;
	cemuLog_log(LogType::Force, "[FrameGen] flow scale = {}% (optical flow {}x{}, piramide={})", m_appliedFlowScalePercent, m_flowWidth, m_flowHeight, m_opticalFlow->IsPyramidActive() ? m_opticalFlow->GetPyramidLevelCount() : 0);
}
