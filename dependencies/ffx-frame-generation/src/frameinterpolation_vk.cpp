// frameinterpolation_vk.cpp  (V6 - ver frameinterpolation_vk.h)

#include "frameinterpolation_vk.h"
#include "opticalflow_vk.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanRenderer.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VKRMemoryManager.h"
#include "config/ActiveSettings.h"
#include "Cemu/Logging/CemuLogging.h"

#include <fstream>
#include <vector>
#include <stdexcept>
#include <string>
#include <cstring>

// Espelha o cbuffer cbInterp de mandacaru_frameinterpolation_simple.hlsl (48 bytes).
struct InterpConstants
{
	int32_t iSize[2];
	int32_t iFlowSize[2];
	float   fFlowSign;      // +1: vetor aponta do frame ATUAL para o ANTERIOR. Se o resultado
	                        // aparecer "ao contrario" (objetos andando pra tras), troque pra -1.
	float   fFlowScale;
	float   fT;
	float   fErrThreshold;
	int32_t iBlock;
	int32_t pad[3];
};
static_assert(sizeof(InterpConstants) == 48, "cbInterp deve ter 48 bytes");

namespace
{
	// V7 (extrapolacao + flow scale). Se nao existir, cai pro shader antigo (so interpolacao).
	constexpr const char* kShaderFileV7 = "mandacaru_frameinterpolation_v7.spv";
	constexpr const char* kShaderFileOld = "mandacaru_frameinterpolation_simple.spv";

	// Esquema de shift (dxc -fvk-b-shift 0 / -fvk-t-shift 100 / -fvk-u-shift 200)
	constexpr uint32_t kBindCb = 0;
	constexpr uint32_t kBindPrev = 100;
	constexpr uint32_t kBindCur = 101;
	constexpr uint32_t kBindFlow = 102;
	constexpr uint32_t kBindOut = 200;

	uint32_t DivRoundUp(uint32_t v, uint32_t d) { return (v + d - 1) / d; }

	fs::path GetShaderPath(const char* fileName)
	{
		return ActiveSettings::GetDataPath("resources/ffx-frame-generation/shaders_spv/{}", fileName);
	}
}

FrameInterpolationVk::FrameInterpolationVk(VulkanRenderer* renderer, uint32_t resolutionWidth, uint32_t resolutionHeight)
	: m_renderer(renderer), m_width(resolutionWidth), m_height(resolutionHeight)
{
	m_memoryManager = renderer->GetMemoryManager();
	CreateResources();
	CreatePipeline();
	CreateDescriptors();
}

FrameInterpolationVk::~FrameInterpolationVk() { DestroyAll(); }

void FrameInterpolationVk::CreateResources()
{
	VkDevice device = m_renderer->GetLogicalDevice();

	VkImageCreateInfo imageInfo{};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.extent = { m_width, m_height, 1 };
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	if (vkCreateImage(device, &imageInfo, nullptr, &m_interpolatedFrameImage) != VK_SUCCESS)
		throw std::runtime_error("FrameInterpolationVk: falha ao criar imagem de saida");

	VkMemoryRequirements memReq;
	vkGetImageMemoryRequirements(device, m_interpolatedFrameImage, &memReq);
	uint32_t memoryTypeIndex = 0;
	if (!m_memoryManager->FindMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memoryTypeIndex))
		throw std::runtime_error("FrameInterpolationVk: nao achei memory type compativel");
	VkMemoryAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memReq.size;
	allocInfo.memoryTypeIndex = memoryTypeIndex;
	if (vkAllocateMemory(device, &allocInfo, nullptr, &m_interpolatedFrameMemory) != VK_SUCCESS)
		throw std::runtime_error("FrameInterpolationVk: vkAllocateMemory falhou");
	vkBindImageMemory(device, m_interpolatedFrameImage, m_interpolatedFrameMemory, 0);

	VkImageViewCreateInfo viewInfo{};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = m_interpolatedFrameImage;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	if (vkCreateImageView(device, &viewInfo, nullptr, &m_interpolatedFrameView) != VK_SUCCESS)
		throw std::runtime_error("FrameInterpolationVk: falha ao criar image view");

	if (!m_memoryManager->CreateBuffer(sizeof(InterpConstants), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		m_constantBuffer, m_constantBufferMemory))
		throw std::runtime_error("FrameInterpolationVk: falha ao criar m_constantBuffer");
}

void FrameInterpolationVk::CreatePipeline()
{
	VkDevice device = m_renderer->GetLogicalDevice();

	fs::path path = GetShaderPath(kShaderFileV7);
	std::ifstream file(path, std::ios::ate | std::ios::binary);
	m_supportsExtrapolation = file.is_open();
	if (!file.is_open())
	{
		path = GetShaderPath(kShaderFileOld);
		file.open(path, std::ios::ate | std::ios::binary);
		cemuLog_log(LogType::Force, "[FrameGen] shader {} nao encontrado - usando o antigo (so interpolacao, sem flow scale correto)", kShaderFileV7);
	}
	if (!file.is_open())
		throw std::runtime_error(std::string("FrameInterpolationVk: nao consegui abrir o shader ") + path.string());
	size_t fileSize = (size_t)file.tellg();
	std::vector<char> code(fileSize);
	file.seekg(0);
	file.read(code.data(), fileSize);
	file.close();

	VkShaderModuleCreateInfo smInfo{};
	smInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	smInfo.codeSize = code.size();
	smInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
	if (vkCreateShaderModule(device, &smInfo, nullptr, &m_shaderModule) != VK_SUCCESS)
		throw std::runtime_error("FrameInterpolationVk: vkCreateShaderModule falhou");

	VkDescriptorSetLayoutBinding b[5]{};
	auto setB = [&](int i, uint32_t binding, VkDescriptorType t)
	{
		b[i].binding = binding; b[i].descriptorType = t; b[i].descriptorCount = 1; b[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	};
	setB(0, kBindCb, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
	setB(1, kBindPrev, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
	setB(2, kBindCur, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
	setB(3, kBindFlow, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
	setB(4, kBindOut, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = 5;
	layoutInfo.pBindings = b;
	if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS)
		throw std::runtime_error("FrameInterpolationVk: falha ao criar descriptor set layout");

	VkPipelineLayoutCreateInfo plInfo{};
	plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	plInfo.setLayoutCount = 1;
	plInfo.pSetLayouts = &m_descriptorSetLayout;
	if (vkCreatePipelineLayout(device, &plInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS)
		throw std::runtime_error("FrameInterpolationVk: falha ao criar pipeline layout");

	VkComputePipelineCreateInfo pInfo{};
	pInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	pInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	pInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	pInfo.stage.module = m_shaderModule;
	pInfo.stage.pName = "CS";
	pInfo.layout = m_pipelineLayout;
	VkResult r = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pInfo, nullptr, &m_pipeline);
	if (r != VK_SUCCESS)
		throw std::runtime_error(std::string("FrameInterpolationVk: falha ao criar pipeline (VkResult=") + std::to_string((int)r) + ")");
}

void FrameInterpolationVk::CreateDescriptors()
{
	VkDevice device = m_renderer->GetLogicalDevice();
	VkDescriptorPoolSize sizes[3] = {
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 },
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 3 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 },
	};
	VkDescriptorPoolCreateInfo pool{};
	pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool.maxSets = 1;
	pool.poolSizeCount = 3;
	pool.pPoolSizes = sizes;
	if (vkCreateDescriptorPool(device, &pool, nullptr, &m_descriptorPool) != VK_SUCCESS)
		throw std::runtime_error("FrameInterpolationVk: falha ao criar descriptor pool");

	VkDescriptorSetAllocateInfo ai{};
	ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	ai.descriptorPool = m_descriptorPool;
	ai.descriptorSetCount = 1;
	ai.pSetLayouts = &m_descriptorSetLayout;
	if (vkAllocateDescriptorSets(device, &ai, &m_descriptorSet) != VK_SUCCESS)
		throw std::runtime_error("FrameInterpolationVk: falha ao alocar descriptor set");
}

void FrameInterpolationVk::Dispatch(VkCommandBuffer commandBuffer,
	VkImageView currentColorView,
	VkImageView previousColorView,
	const OpticalFlowVk& opticalFlow,
	float deltaTimeSeconds,
	bool reset,
	float progress)
{
	(void)deltaTimeSeconds; (void)reset;
	VkDevice device = m_renderer->GetLogicalDevice();

	if (!m_resourcesTransitioned)
	{
		VkImageMemoryBarrier bar{};
		bar.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		bar.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		bar.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		bar.image = m_interpolatedFrameImage;
		bar.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		bar.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			0, 0, nullptr, 0, nullptr, 1, &bar);
		m_resourcesTransitioned = true;
	}

	// Constantes
	{
		InterpConstants c{};
		c.iSize[0] = (int32_t)m_width;
		c.iSize[1] = (int32_t)m_height;
		// O Optical Flow pode rodar em resolucao menor ("flow scale"): o tamanho da textura de
		// vetores vem dele, e os vetores (em pixels do OF) sao convertidos p/ pixels cheios.
		c.iFlowSize[0] = (int32_t)opticalFlow.GetFlowTextureWidth();
		c.iFlowSize[1] = (int32_t)opticalFlow.GetFlowTextureHeight();
		c.fFlowSign = 1.0f;
		c.fFlowScale = (float)m_width / (float)opticalFlow.GetInputWidth();
		// V7: fT > 1 = extrapola a partir do frame atual (ordem temporal correta).
		// Shader antigo: interpola entre anterior(0) e atual(1) em t = progress.
		c.fT = m_supportsExtrapolation ? (1.0f + progress) : progress;
		c.fErrThreshold = 0.12f;
		c.iBlock = (int32_t)OpticalFlowVk::kBlockSize;
		void* mapped = nullptr;
		vkMapMemory(device, m_constantBufferMemory, 0, sizeof(c), 0, &mapped);
		memcpy(mapped, &c, sizeof(c));
		vkUnmapMemory(device, m_constantBufferMemory);
	}

	// Descriptors
	VkDescriptorBufferInfo cb{ m_constantBuffer, 0, sizeof(InterpConstants) };
	VkDescriptorImageInfo prev{ VK_NULL_HANDLE, previousColorView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	VkDescriptorImageInfo cur{ VK_NULL_HANDLE, currentColorView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	VkDescriptorImageInfo flow{ VK_NULL_HANDLE, opticalFlow.GetOpticalFlowVectorView(), VK_IMAGE_LAYOUT_GENERAL };
	VkDescriptorImageInfo out{ VK_NULL_HANDLE, m_interpolatedFrameView, VK_IMAGE_LAYOUT_GENERAL };

	VkWriteDescriptorSet w[5]{};
	auto setW = [&](int i, uint32_t binding, VkDescriptorType t, const VkDescriptorBufferInfo* bi, const VkDescriptorImageInfo* ii)
	{
		w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		w[i].dstSet = m_descriptorSet;
		w[i].dstBinding = binding;
		w[i].descriptorCount = 1;
		w[i].descriptorType = t;
		w[i].pBufferInfo = bi;
		w[i].pImageInfo = ii;
	};
	setW(0, kBindCb, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &cb, nullptr);
	setW(1, kBindPrev, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, nullptr, &prev);
	setW(2, kBindCur, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, nullptr, &cur);
	setW(3, kBindFlow, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, nullptr, &flow);
	setW(4, kBindOut, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &out);
	vkUpdateDescriptorSets(device, 5, w, 0, nullptr);

	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
	vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);
	vkCmdDispatch(commandBuffer, DivRoundUp(m_width, 8), DivRoundUp(m_height, 8), 1);
}

void FrameInterpolationVk::DestroyAll()
{
	VkDevice device = m_renderer->GetLogicalDevice();
	vkDeviceWaitIdle(device);
	if (m_pipeline) vkDestroyPipeline(device, m_pipeline, nullptr);
	if (m_pipelineLayout) vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
	if (m_descriptorSetLayout) vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);
	if (m_descriptorPool) vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
	if (m_shaderModule) vkDestroyShaderModule(device, m_shaderModule, nullptr);
	if (m_interpolatedFrameView) vkDestroyImageView(device, m_interpolatedFrameView, nullptr);
	if (m_interpolatedFrameImage) vkDestroyImage(device, m_interpolatedFrameImage, nullptr);
	if (m_interpolatedFrameMemory) vkFreeMemory(device, m_interpolatedFrameMemory, nullptr);
	if (m_constantBuffer) vkDestroyBuffer(device, m_constantBuffer, nullptr);
	if (m_constantBufferMemory) vkFreeMemory(device, m_constantBufferMemory, nullptr);
}
