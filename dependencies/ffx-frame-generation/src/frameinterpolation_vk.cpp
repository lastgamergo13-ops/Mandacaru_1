// frameinterpolation_vk.cpp
//
// Ver o comentario no topo de frameinterpolation_vk.h antes de mexer
// aqui - esta e uma PRIMEIRA VERSAO, com os mesmos avisos de escopo
// do opticalflow_vk.cpp.

#include "frameinterpolation_vk.h"
#include "opticalflow_vk.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanRenderer.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VKRMemoryManager.h"

#include <fstream>
#include <vector>
#include <stdexcept>
#include <cstring>

// Estrutura do cbuffer cbFI (FFX_FRAMEINTERPOLATION_BIND_CB_FRAMEINTERPOLATION),
// copiada campo a campo de ffx_frameinterpolation_callbacks_hlsl.h -
// se a AMD mudar esse layout numa atualizacao do SDK, atualize aqui tambem.
struct FrameInterpolationConstants
{
	int32_t  renderSize[2];
	int32_t  displaySize[2];

	float    displaySizeRcp[2];
	float    cameraNear;
	float    cameraFar;

	int32_t  upscalerTargetSize[2];
	int32_t  mode;
	int32_t  reset;

	float    deviceToViewDepth[4];

	float    deltaTime;
	int32_t  hudLessAttachedFactor;
	int32_t  distortionFieldSize[2];

	float    opticalFlowScale[2];
	int32_t  opticalFlowBlockSize;
	uint32_t dispatchFlags;

	int32_t  maxRenderSize[2];
	int32_t  opticalFlowHalfResMode;
	int32_t  numInstances;

	int32_t  interpolationRectBase[2];
	int32_t  interpolationRectSize[2];

	float    debugBarColor[3];
	uint32_t backBufferTransferFunction;

	float    minMaxLuminance[2];
	float    tanHalfFov;
	int32_t  pad1;

	float    jitter[2];
	float    motionVectorScale[2];
};

namespace
{
	constexpr const char* kPassShaderFiles[(uint32_t)FrameInterpolationVk::Pass::Count] = {
		"ffx_frameinterpolation_setup_pass.spv",
		"ffx_frameinterpolation_reconstruct_previous_depth_pass.spv",
		"ffx_frameinterpolation_optical_flow_vector_field_pass.spv",
		"ffx_frameinterpolation_disocclusion_mask_pass.spv",
		"ffx_frameinterpolation_pass.spv",
		"ffx_frameinterpolation_compute_inpainting_pyramid_pass.spv",
		"ffx_frameinterpolation_inpainting_pass.spv",
	};

	// TODO: mesmo aviso do opticalflow_vk.cpp - confirme o tamanho real
	// do thread group abrindo o header do SDK antes de usar em producao.
	constexpr uint32_t kThreadGroupWidth = 8;
	constexpr uint32_t kThreadGroupHeight = 8;

	uint32_t DivRoundUp(uint32_t value, uint32_t divisor)
	{
		return (value + divisor - 1) / divisor;
	}

	std::string GetShaderPath(const char* fileName)
	{
		return std::string("dependencies/ffx-frame-generation/shaders_spv/") + fileName;
	}

	// Helper generico de criacao de imagem 2D - repete o padrao usado
	// varias vezes no opticalflow_vk.cpp; se voce ja fatorou isso num
	// lugar comum durante a integracao, use essa versao compartilhada
	// no lugar desta copia local.
	void CreateImage2D(VulkanRenderer* renderer, VKRMemoryManager* memoryManager,
		uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage,
		VkImage& outImage, VkDeviceMemory& outMemory, VkImageView& outView)
	{
		VkDevice device = renderer->GetLogicalDevice();

		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.extent = { width, height, 1 };
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.format = format;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageInfo.usage = usage;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

		if (vkCreateImage(device, &imageInfo, nullptr, &outImage) != VK_SUCCESS)
			throw std::runtime_error("FrameInterpolationVk: falha ao criar imagem intermediaria");

		VkMemoryRequirements memReq;
		vkGetImageMemoryRequirements(device, outImage, &memReq);

		uint32_t memoryTypeIndex = 0;
		if (!memoryManager->FindMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memoryTypeIndex))
			throw std::runtime_error("FrameInterpolationVk: nao achei memory type compativel");

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memReq.size;
		allocInfo.memoryTypeIndex = memoryTypeIndex;
		if (vkAllocateMemory(device, &allocInfo, nullptr, &outMemory) != VK_SUCCESS)
			throw std::runtime_error("FrameInterpolationVk: vkAllocateMemory falhou");
		vkBindImageMemory(device, outImage, outMemory, 0);

		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = outImage;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = format;
		viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		if (vkCreateImageView(device, &viewInfo, nullptr, &outView) != VK_SUCCESS)
			throw std::runtime_error("FrameInterpolationVk: falha ao criar image view");
	}
}

FrameInterpolationVk::FrameInterpolationVk(VulkanRenderer* renderer, uint32_t resolutionWidth, uint32_t resolutionHeight)
	: m_renderer(renderer), m_width(resolutionWidth), m_height(resolutionHeight)
{
	m_memoryManager = renderer->GetMemoryManager();

	CreateShaderModules();
	CreateIntermediateResources(resolutionWidth, resolutionHeight);
	CreatePipelines();
	CreateDescriptorSets();
	CreateSampler();
}

FrameInterpolationVk::~FrameInterpolationVk()
{
	DestroyAll();
}

VkShaderModule FrameInterpolationVk::LoadShaderModule(const char* spvFileName) const
{
	std::string path = GetShaderPath(spvFileName);
	std::ifstream file(path, std::ios::ate | std::ios::binary);
	if (!file.is_open())
		throw std::runtime_error(std::string("FrameInterpolationVk: nao consegui abrir o shader ") + path);

	size_t fileSize = (size_t)file.tellg();
	std::vector<char> buffer(fileSize);
	file.seekg(0);
	file.read(buffer.data(), fileSize);
	file.close();

	VkShaderModuleCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.codeSize = buffer.size();
	createInfo.pCode = reinterpret_cast<const uint32_t*>(buffer.data());

	VkShaderModule module;
	if (vkCreateShaderModule(m_renderer->GetLogicalDevice(), &createInfo, nullptr, &module) != VK_SUCCESS)
		throw std::runtime_error(std::string("FrameInterpolationVk: vkCreateShaderModule falhou para ") + spvFileName);

	return module;
}

void FrameInterpolationVk::CreateShaderModules()
{
	for (uint32_t i = 0; i < (uint32_t)Pass::Count; i++)
		m_shaderModules[i] = LoadShaderModule(kPassShaderFiles[i]);
}

void FrameInterpolationVk::CreateIntermediateResources(uint32_t width, uint32_t height)
{
	CreateImage2D(m_renderer, m_memoryManager, width, height, VK_FORMAT_R32_SFLOAT,
		VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		m_reconstructedPrevDepthImage, m_reconstructedPrevDepthMemory, m_reconstructedPrevDepthView);

	// TODO: ver aviso no .h - esta imagem nao e escrita por nenhum
	// dispatch nesta versao, ja que RECONSTRUCT_AND_DILATE nao esta
	// sendo chamado. Fica alocada e zerada (VK_IMAGE_LAYOUT_UNDEFINED
	// -> GENERAL, sem conteudo util) ate isso ser resolvido.
	CreateImage2D(m_renderer, m_memoryManager, width, height, VK_FORMAT_R16G16_SFLOAT,
		VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		m_dilatedVelocityImage, m_dilatedVelocityMemory, m_dilatedVelocityView);

	CreateImage2D(m_renderer, m_memoryManager, width, height, VK_FORMAT_R32_SFLOAT,
		VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		m_disocclusionMaskImage, m_disocclusionMaskMemory, m_disocclusionMaskView);

	// TODO: piramide completa (varios niveis de mip) - 1 nivel aqui,
	// mesma simplificacao do luminance pyramid no OpticalFlowVk.
	CreateImage2D(m_renderer, m_memoryManager, width, height, VK_FORMAT_R32G32B32A32_SFLOAT,
		VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		m_inpaintingPyramidImage, m_inpaintingPyramidMemory, m_inpaintingPyramidView);

	CreateImage2D(m_renderer, m_memoryManager, width, height, VK_FORMAT_R32_SFLOAT,
		VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		m_inpaintingMaskImage, m_inpaintingMaskMemory, m_inpaintingMaskView);

	CreateImage2D(m_renderer, m_memoryManager, width, height, VK_FORMAT_R16G16B16A16_SFLOAT,
		VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
		m_interpolatedFrameImage, m_interpolatedFrameMemory, m_interpolatedFrameView);
	// VK_IMAGE_USAGE_TRANSFER_SRC_BIT acima porque o swapchain_vk (proxima
	// etapa) vai precisar copiar/blitar isso pra dentro do swapchain.

	bool ok = m_memoryManager->CreateBuffer(
		sizeof(FrameInterpolationConstants),
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		m_constantBuffer,
		m_constantBufferMemory);
	if (!ok)
		throw std::runtime_error("FrameInterpolationVk: falha ao criar m_constantBuffer");
}

void FrameInterpolationVk::CreatePipelines()
{
	VkDevice device = m_renderer->GetLogicalDevice();

	// Layout simplificado (mesma abordagem do OpticalFlowVk - um so
	// layout unificado pra todos os passes, ver aviso sobre fidelidade
	// aos bindings reais por pass):
	//   0: UBO cbFI
	//   1: cor atual (combined image sampler)
	//   2: cor anterior / previousInterpolationSource (combined image sampler)
	//   3: vetores de optical flow, vindos do OpticalFlowVk (combined image sampler)
	//   4: profundidade reconstruida do frame anterior (storage image)
	//   5: velocidade dilatada (storage image)
	//   6: mascara de disoclusao (storage image)
	//   7: piramide de inpainting (storage image)
	//   8: mascara de inpainting (storage image)
	//   9: saida - frame interpolado (storage image)
	VkDescriptorSetLayoutBinding bindings[10]{};
	bindings[0] = { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
	bindings[1] = { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
	bindings[2] = { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
	bindings[3] = { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
	bindings[4] = { 4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
	bindings[5] = { 5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
	bindings[6] = { 6, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
	bindings[7] = { 7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
	bindings[8] = { 8, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
	bindings[9] = { 9, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };

	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = 10;
	layoutInfo.pBindings = bindings;
	if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS)
		throw std::runtime_error("FrameInterpolationVk: falha ao criar descriptor set layout");

	VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 1;
	pipelineLayoutInfo.pSetLayouts = &m_descriptorSetLayout;
	if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS)
		throw std::runtime_error("FrameInterpolationVk: falha ao criar pipeline layout");

	for (uint32_t i = 0; i < (uint32_t)Pass::Count; i++)
	{
		VkPipelineShaderStageCreateInfo stageInfo{};
		stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		stageInfo.module = m_shaderModules[i];
		stageInfo.pName = "CS";

		VkComputePipelineCreateInfo pipelineInfo{};
		pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		pipelineInfo.stage = stageInfo;
		pipelineInfo.layout = m_pipelineLayout;

		if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipelines[i]) != VK_SUCCESS)
			throw std::runtime_error(std::string("FrameInterpolationVk: falha ao criar pipeline para pass ") + std::to_string(i));
	}
}

void FrameInterpolationVk::CreateDescriptorSets()
{
	VkDevice device = m_renderer->GetLogicalDevice();

	VkDescriptorPoolSize poolSizes[3]{};
	poolSizes[0] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, (uint32_t)Pass::Count };
	poolSizes[1] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, (uint32_t)Pass::Count * 3 };
	poolSizes[2] = { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, (uint32_t)Pass::Count * 6 };

	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.poolSizeCount = 3;
	poolInfo.pPoolSizes = poolSizes;
	poolInfo.maxSets = (uint32_t)Pass::Count;
	if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS)
		throw std::runtime_error("FrameInterpolationVk: falha ao criar descriptor pool");

	std::vector<VkDescriptorSetLayout> layouts((uint32_t)Pass::Count, m_descriptorSetLayout);
	VkDescriptorSetAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = m_descriptorPool;
	allocInfo.descriptorSetCount = (uint32_t)Pass::Count;
	allocInfo.pSetLayouts = layouts.data();
	if (vkAllocateDescriptorSets(device, &allocInfo, m_descriptorSets) != VK_SUCCESS)
		throw std::runtime_error("FrameInterpolationVk: falha ao alocar descriptor sets");
}

void FrameInterpolationVk::CreateSampler()
{
	VkSamplerCreateInfo samplerInfo{};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.anisotropyEnable = VK_FALSE;
	samplerInfo.maxAnisotropy = 1.0f;
	samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
	samplerInfo.unnormalizedCoordinates = VK_FALSE;
	samplerInfo.compareEnable = VK_FALSE;
	samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

	if (vkCreateSampler(m_renderer->GetLogicalDevice(), &samplerInfo, nullptr, &m_colorSampler) != VK_SUCCESS)
		throw std::runtime_error("FrameInterpolationVk: falha ao criar m_colorSampler");
}

void FrameInterpolationVk::UpdateDescriptorSets(VkImageView currentColorView, VkImageView previousColorView, VkImageView opticalFlowVectorView)
{
	VkDevice device = m_renderer->GetLogicalDevice();

	for (uint32_t i = 0; i < (uint32_t)Pass::Count; i++)
	{
		VkDescriptorBufferInfo cbInfo{ m_constantBuffer, 0, sizeof(FrameInterpolationConstants) };

		VkDescriptorImageInfo currentColorInfo{ m_colorSampler, currentColorView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		VkDescriptorImageInfo previousColorInfo{ m_colorSampler, previousColorView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		VkDescriptorImageInfo opticalFlowInfo{ m_colorSampler, opticalFlowVectorView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

		VkDescriptorImageInfo prevDepthInfo{ VK_NULL_HANDLE, m_reconstructedPrevDepthView, VK_IMAGE_LAYOUT_GENERAL };
		VkDescriptorImageInfo dilatedVelInfo{ VK_NULL_HANDLE, m_dilatedVelocityView, VK_IMAGE_LAYOUT_GENERAL };
		VkDescriptorImageInfo disocclusionInfo{ VK_NULL_HANDLE, m_disocclusionMaskView, VK_IMAGE_LAYOUT_GENERAL };
		VkDescriptorImageInfo inpaintPyramidInfo{ VK_NULL_HANDLE, m_inpaintingPyramidView, VK_IMAGE_LAYOUT_GENERAL };
		VkDescriptorImageInfo inpaintMaskInfo{ VK_NULL_HANDLE, m_inpaintingMaskView, VK_IMAGE_LAYOUT_GENERAL };
		VkDescriptorImageInfo outputInfo{ VK_NULL_HANDLE, m_interpolatedFrameView, VK_IMAGE_LAYOUT_GENERAL };

		VkWriteDescriptorSet writes[10]{};
		auto setWrite = [&](uint32_t idx, uint32_t binding, VkDescriptorType type, const VkDescriptorBufferInfo* bufInfo, const VkDescriptorImageInfo* imgInfo)
		{
			writes[idx].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[idx].dstSet = m_descriptorSets[i];
			writes[idx].dstBinding = binding;
			writes[idx].descriptorCount = 1;
			writes[idx].descriptorType = type;
			writes[idx].pBufferInfo = bufInfo;
			writes[idx].pImageInfo = imgInfo;
		};

		setWrite(0, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &cbInfo, nullptr);
		setWrite(1, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &currentColorInfo);
		setWrite(2, 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &previousColorInfo);
		setWrite(3, 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &opticalFlowInfo);
		setWrite(4, 4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &prevDepthInfo);
		setWrite(5, 5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &dilatedVelInfo);
		setWrite(6, 6, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &disocclusionInfo);
		setWrite(7, 7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &inpaintPyramidInfo);
		setWrite(8, 8, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &inpaintMaskInfo);
		setWrite(9, 9, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &outputInfo);

		vkUpdateDescriptorSets(device, 10, writes, 0, nullptr);
	}
}

void FrameInterpolationVk::ComputeToComputeBarrier(VkCommandBuffer cb)
{
	VkMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

	vkCmdPipelineBarrier(cb,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void FrameInterpolationVk::Dispatch(VkCommandBuffer commandBuffer,
	VkImageView currentColorView,
	VkImageView previousColorView,
	const OpticalFlowVk& opticalFlow,
	float deltaTimeSeconds,
	bool reset)
{
	if (!m_resourcesTransitioned)
	{
		VkImage imagesToTransition[6] = {
			m_reconstructedPrevDepthImage, m_dilatedVelocityImage, m_disocclusionMaskImage,
			m_inpaintingPyramidImage, m_inpaintingMaskImage, m_interpolatedFrameImage
		};

		VkImageMemoryBarrier barriers[6]{};
		for (int i = 0; i < 6; i++)
		{
			barriers[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barriers[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			barriers[i].newLayout = VK_IMAGE_LAYOUT_GENERAL;
			barriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barriers[i].image = imagesToTransition[i];
			barriers[i].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			barriers[i].srcAccessMask = 0;
			barriers[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
		}

		vkCmdPipelineBarrier(commandBuffer,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			0, 0, nullptr, 0, nullptr, 6, barriers);

		m_resourcesTransitioned = true;
	}

	// 1) Atualiza o buffer de constantes
	{
		FrameInterpolationConstants constants{};
		constants.renderSize[0] = (int32_t)m_width;
		constants.renderSize[1] = (int32_t)m_height;
		constants.displaySize[0] = (int32_t)m_width;
		constants.displaySize[1] = (int32_t)m_height;
		constants.displaySizeRcp[0] = 1.0f / (float)m_width;
		constants.displaySizeRcp[1] = 1.0f / (float)m_height;
		constants.cameraNear = 0.1f;  // TODO: o Cemu nao tem depth real do jogo -
		constants.cameraFar = 1000.0f; // ver aviso no relatorio principal sobre
		                                // reconstrucao de depth sem depth real
		constants.upscalerTargetSize[0] = (int32_t)m_width;
		constants.upscalerTargetSize[1] = (int32_t)m_height;
		constants.mode = 0;
		constants.reset = reset ? 1 : 0;
		constants.deviceToViewDepth[0] = 1.0f;
		constants.deviceToViewDepth[1] = 1.0f;
		constants.deviceToViewDepth[2] = 0.0f;
		constants.deviceToViewDepth[3] = 0.0f;
		constants.deltaTime = deltaTimeSeconds;
		constants.hudLessAttachedFactor = 0;
		constants.distortionFieldSize[0] = 0;
		constants.distortionFieldSize[1] = 0;
		constants.opticalFlowScale[0] = 1.0f / (float)m_width;
		constants.opticalFlowScale[1] = 1.0f / (float)m_height;
		constants.opticalFlowBlockSize = 8; // TODO: confirme contra o valor real usado no OpticalFlowVk
		constants.dispatchFlags = 0;
		constants.maxRenderSize[0] = (int32_t)m_width;
		constants.maxRenderSize[1] = (int32_t)m_height;
		constants.opticalFlowHalfResMode = 0;
		constants.numInstances = 1;
		constants.interpolationRectBase[0] = 0;
		constants.interpolationRectBase[1] = 0;
		constants.interpolationRectSize[0] = (int32_t)m_width;
		constants.interpolationRectSize[1] = (int32_t)m_height;
		constants.debugBarColor[0] = 0.0f;
		constants.debugBarColor[1] = 0.0f;
		constants.debugBarColor[2] = 0.0f;
		constants.backBufferTransferFunction = 0;
		constants.minMaxLuminance[0] = 0.0f;
		constants.minMaxLuminance[1] = 1.0f;
		constants.tanHalfFov = 0.5773f; // ~60 graus de FOV - ajuste conforme o jogo, se relevante
		constants.pad1 = 0;
		constants.jitter[0] = 0.0f;
		constants.jitter[1] = 0.0f;
		constants.motionVectorScale[0] = 1.0f;
		constants.motionVectorScale[1] = 1.0f;

		void* mapped = nullptr;
		vkMapMemory(m_renderer->GetLogicalDevice(), m_constantBufferMemory, 0, sizeof(constants), 0, &mapped);
		memcpy(mapped, &constants, sizeof(constants));
		vkUnmapMemory(m_renderer->GetLogicalDevice(), m_constantBufferMemory);
	}

	// 2) Atualiza os descriptor sets com as views deste frame
	UpdateDescriptorSets(currentColorView, previousColorView, opticalFlow.GetOpticalFlowVectorView());

	// 3) Executa os 7 passes, na ordem CONFIRMADA no .cpp de referencia
	//    (Setup -> ReconstructPreviousDepth -> OpticalFlowVectorField ->
	//    DisocclusionMask -> Interpolation -> InpaintingPyramid -> Inpainting)
	uint32_t groupsX = DivRoundUp(m_width, kThreadGroupWidth);
	uint32_t groupsY = DivRoundUp(m_height, kThreadGroupHeight);

	for (uint32_t i = 0; i < (uint32_t)Pass::Count; i++)
	{
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelines[i]);
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout,
			0, 1, &m_descriptorSets[i], 0, nullptr);

		vkCmdDispatch(commandBuffer, groupsX, groupsY, 1);

		if (i + 1 < (uint32_t)Pass::Count)
			ComputeToComputeBarrier(commandBuffer);
	}

	m_frameIndex++;
}

void FrameInterpolationVk::DestroyAll()
{
	VkDevice device = m_renderer->GetLogicalDevice();
	vkDeviceWaitIdle(device);

	for (uint32_t i = 0; i < (uint32_t)Pass::Count; i++)
	{
		if (m_pipelines[i]) vkDestroyPipeline(device, m_pipelines[i], nullptr);
		if (m_shaderModules[i]) vkDestroyShaderModule(device, m_shaderModules[i], nullptr);
	}
	if (m_pipelineLayout) vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
	if (m_descriptorSetLayout) vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);
	if (m_descriptorPool) vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
	if (m_colorSampler) vkDestroySampler(device, m_colorSampler, nullptr);

	auto destroyImage = [&](VkImage& img, VkDeviceMemory& mem, VkImageView& view)
	{
		if (view) vkDestroyImageView(device, view, nullptr);
		if (img) vkDestroyImage(device, img, nullptr);
		if (mem) vkFreeMemory(device, mem, nullptr);
	};

	destroyImage(m_reconstructedPrevDepthImage, m_reconstructedPrevDepthMemory, m_reconstructedPrevDepthView);
	destroyImage(m_dilatedVelocityImage, m_dilatedVelocityMemory, m_dilatedVelocityView);
	destroyImage(m_disocclusionMaskImage, m_disocclusionMaskMemory, m_disocclusionMaskView);
	destroyImage(m_inpaintingPyramidImage, m_inpaintingPyramidMemory, m_inpaintingPyramidView);
	destroyImage(m_inpaintingMaskImage, m_inpaintingMaskMemory, m_inpaintingMaskView);
	destroyImage(m_interpolatedFrameImage, m_interpolatedFrameMemory, m_interpolatedFrameView);

	if (m_constantBuffer) vkDestroyBuffer(device, m_constantBuffer, nullptr);
	if (m_constantBufferMemory) vkFreeMemory(device, m_constantBufferMemory, nullptr);
}
