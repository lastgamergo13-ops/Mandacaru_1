// opticalflow_vk.cpp
//
// Ver o comentario no topo de opticalflow_vk.h - VERSAO 2, reescrita
// apos o crash real em hardware (Adreno 650) causado pelo layout
// generico da V1. Cada pass agora tem seu proprio layout e formula de
// dispatch, extraidos diretamente dos #define e das formulas reais de
// ffx_opticalflow.cpp (SDK de referencia da AMD).

#include "opticalflow_vk.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanRenderer.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VKRMemoryManager.h"
#include "config/ActiveSettings.h"

#include <fstream>
#include <vector>
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <initializer_list>

struct OpticalFlowConstants
{
	int32_t  iInputLumaResolution[2];
	uint32_t uOpticalFlowPyramidLevel;
	uint32_t uOpticalFlowPyramidLevelCount;
	uint32_t iFrameIndex;
	uint32_t backbufferTransferFunction;
	float    minMaxLuminance[2];
};

// cbOF_SPD - so usado pelo pass ComputeLuminancePyramid (ver
// ffx_opticalflow_callbacks_hlsl.h, bloco FFX_OPTICALFLOW_BIND_CB_SPD)
struct OpticalFlowSpdConstants
{
	uint32_t mips;
	uint32_t numWorkGroups;
	uint32_t workGroupOffset[2];
	uint32_t numWorkGroupOpticalFlowInputPyramid;
	uint32_t pad0;
	uint32_t pad1;
};

namespace
{
	constexpr const char* kPassShaderFiles[(uint32_t)OpticalFlowVk::Pass::Count] = {
		"ffx_opticalflow_prepare_luma_pass.spv",
		"ffx_opticalflow_compute_luminance_pyramid_pass.spv",
		"ffx_opticalflow_generate_scd_histogram_pass.spv",
		"ffx_opticalflow_compute_scd_divergence_pass.spv",
		"ffx_opticalflow_compute_optical_flow_advanced_pass_v5.spv",
		"ffx_opticalflow_filter_optical_flow_pass_v5.spv",
		"ffx_opticalflow_scale_optical_flow_advanced_pass_v5.spv",
	};

	// Constantes reais da AMD (ffx_opticalflow.cpp) - usadas pro
	// dimensionamento dos buffers de histograma SCD e formulas de dispatch.
	constexpr uint32_t kHistogramBins = 256;
	constexpr uint32_t kHistogramsPerDim = 3;
	constexpr uint32_t kHistogramShifts = 3;

	uint32_t DivRoundUp(uint32_t value, uint32_t divisor)
	{
		return (value + divisor - 1) / divisor;
	}

	fs::path GetShaderPath(const char* fileName)
	{
		return ActiveSettings::GetDataPath("resources/ffx-frame-generation/shaders_spv/{}", fileName);
	}
}

OpticalFlowVk::OpticalFlowVk(VulkanRenderer* renderer, uint32_t resolutionWidth, uint32_t resolutionHeight)
	: m_renderer(renderer), m_width(resolutionWidth), m_height(resolutionHeight)
{
	m_memoryManager = renderer->GetMemoryManager();

	CreateShaderModules();
	CreateIntermediateResources(resolutionWidth, resolutionHeight);
	CreatePerPassLayoutsAndPipelines();
	CreateDescriptorPoolAndSets();
	CreateSampler();
}

OpticalFlowVk::~OpticalFlowVk()
{
	DestroyAll();
}

VkShaderModule OpticalFlowVk::LoadShaderModule(const char* spvFileName) const
{
	fs::path path = GetShaderPath(spvFileName);
	std::ifstream file(path, std::ios::ate | std::ios::binary);
	if (!file.is_open())
		throw std::runtime_error(std::string("OpticalFlowVk: nao consegui abrir o shader ") + path.string());

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
		throw std::runtime_error(std::string("OpticalFlowVk: vkCreateShaderModule falhou para ") + spvFileName);

	return module;
}

void OpticalFlowVk::CreateShaderModules()
{
	for (uint32_t i = 0; i < (uint32_t)Pass::Count; i++)
		m_shaderModules[i] = LoadShaderModule(kPassShaderFiles[i]);
}

namespace
{
	void CreateImage2D(VulkanRenderer* renderer, VKRMemoryManager* memoryManager,
		uint32_t width, uint32_t height, VkFormat format,
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
		imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

		if (vkCreateImage(device, &imageInfo, nullptr, &outImage) != VK_SUCCESS)
			throw std::runtime_error("OpticalFlowVk: falha ao criar imagem intermediaria");

		VkMemoryRequirements memReq;
		vkGetImageMemoryRequirements(device, outImage, &memReq);
		uint32_t memoryTypeIndex = 0;
		if (!memoryManager->FindMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memoryTypeIndex))
			throw std::runtime_error("OpticalFlowVk: nao achei memory type compativel");

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memReq.size;
		allocInfo.memoryTypeIndex = memoryTypeIndex;
		if (vkAllocateMemory(device, &allocInfo, nullptr, &outMemory) != VK_SUCCESS)
			throw std::runtime_error("OpticalFlowVk: vkAllocateMemory falhou");
		vkBindImageMemory(device, outImage, outMemory, 0);

		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = outImage;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = format;
		viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		if (vkCreateImageView(device, &viewInfo, nullptr, &outView) != VK_SUCCESS)
			throw std::runtime_error("OpticalFlowVk: falha ao criar image view");
	}
}

void OpticalFlowVk::CreateIntermediateResources(uint32_t width, uint32_t height)
{
	// luma (pass 0 escreve, varios passes leem)
	CreateImage2D(m_renderer, m_memoryManager, width, height, VK_FORMAT_R32_SFLOAT,
		m_lumaImage, m_lumaMemory, m_lumaView);

	// Niveis da piramide (pass 1). Nivel 1 em resolucao real (metade),
	// niveis 2-6 so existem pra satisfazer o layout do shader - sao
	// imagens 4x4 "dummy", sem dado util (simplificacao de 1 nivel).
	for (int i = 0; i < kPyramidLevels; i++)
	{
		uint32_t levelW = (i == 0) ? std::max(1u, width / 2) : 4u;
		uint32_t levelH = (i == 0) ? std::max(1u, height / 2) : 4u;
		CreateImage2D(m_renderer, m_memoryManager, levelW, levelH, VK_FORMAT_R32_SFLOAT,
			m_pyramidImages[i], m_pyramidMemory[i], m_pyramidViews[i]);
	}

	// Buffers de histograma SCD - tamanho real: 256 * 3 * 3 = 2304 elementos
	const VkDeviceSize histogramSize = kHistogramElementCount * sizeof(uint32_t);
	auto makeBuffer = [&](VkBuffer& buf, VkDeviceMemory& mem, VkDeviceSize size)
	{
		if (!m_memoryManager->CreateBuffer(size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, buf, mem))
			throw std::runtime_error("OpticalFlowVk: falha ao criar buffer SCD");
	};
	makeBuffer(m_scdHistogramBuffer, m_scdHistogramMemory, histogramSize);
	makeBuffer(m_scdPreviousHistogramBuffer, m_scdPreviousHistogramMemory, histogramSize);
	makeBuffer(m_scdTempBuffer, m_scdTempMemory, histogramSize);
	makeBuffer(m_scdOutputBuffer, m_scdOutputMemory, histogramSize);

	// Pipeline de 3 estagios do optical flow em si (raw -> filtered -> final)
	// ver explicacao no .h sobre por que 3 imagens em vez de 1 (evitar
	// ler e escrever o mesmo recurso no mesmo dispatch).
	CreateImage2D(m_renderer, m_memoryManager, width, height, VK_FORMAT_R16G16_SFLOAT,
		m_opticalFlowVectorImage, m_opticalFlowVectorMemory, m_opticalFlowVectorView);

	// Constant buffers
	if (!m_memoryManager->CreateBuffer(sizeof(OpticalFlowConstants), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_constantBuffer, m_constantBufferMemory))
		throw std::runtime_error("OpticalFlowVk: falha ao criar m_constantBuffer");

	if (!m_memoryManager->CreateBuffer(sizeof(OpticalFlowSpdConstants), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_spdConstantBuffer, m_spdConstantBufferMemory))
		throw std::runtime_error("OpticalFlowVk: falha ao criar m_spdConstantBuffer");
}

namespace
{
	// Helper: cria um descriptor set layout a partir de uma lista
	// compacta de (binding, tipo) - todos no estagio de compute.
	VkDescriptorSetLayout MakeLayout(VkDevice device, std::initializer_list<VkDescriptorType> types)
	{
		std::vector<VkDescriptorSetLayoutBinding> bindings;
		uint32_t i = 0;
		for (VkDescriptorType t : types)
		{
			VkDescriptorSetLayoutBinding b{};
			b.binding = i++;
			b.descriptorType = t;
			b.descriptorCount = 1;
			b.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
			bindings.push_back(b);
		}
		VkDescriptorSetLayoutCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		info.bindingCount = (uint32_t)bindings.size();
		info.pBindings = bindings.data();

		VkDescriptorSetLayout layout;
		if (vkCreateDescriptorSetLayout(device, &info, nullptr, &layout) != VK_SUCCESS)
			throw std::runtime_error("OpticalFlowVk: falha ao criar descriptor set layout");
		return layout;
	}
}

void OpticalFlowVk::CreatePerPassLayoutsAndPipelines()
{
	VkDevice device = m_renderer->GetLogicalDevice();
	using DT = VkDescriptorType;
	constexpr DT CIS = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	constexpr DT SI = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	constexpr DT SB = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	constexpr DT UB = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

	// Layouts fieis a tabela extraida dos #define de cada .hlsl real:
	m_descriptorSetLayouts[(uint32_t)Pass::PrepareLuma] =
		MakeLayout(device, { CIS, SI, UB }); // SRV cor, UAV luma, CB comum

	m_descriptorSetLayouts[(uint32_t)Pass::ComputeLuminancePyramid] =
		MakeLayout(device, { SI, SI, SI, SI, SI, SI, SI, UB, UB }); // UAV input + 6 niveis + CB comum + CB SPD

	m_descriptorSetLayouts[(uint32_t)Pass::GenerateScdHistogram] =
		MakeLayout(device, { CIS, SB, UB }); // SRV luma, UAV histograma, CB comum

	m_descriptorSetLayouts[(uint32_t)Pass::ComputeScdDivergence] =
		MakeLayout(device, { SB, SB, SB, SB, UB }); // 4 UAVs buffer + CB comum

	m_descriptorSetLayouts[(uint32_t)Pass::ComputeOpticalFlowAdvancedV5] =
		MakeLayout(device, { CIS, CIS, SI, SB, UB }); // SRV luma atual+anterior, UAV optical flow, UAV scd output, CB comum

	m_descriptorSetLayouts[(uint32_t)Pass::FilterOpticalFlowV5] =
		MakeLayout(device, { CIS, SI, UB }); // SRV optical_flow_previous, UAV optical_flow, CB comum

	m_descriptorSetLayouts[(uint32_t)Pass::ScaleOpticalFlowAdvancedV5] =
		MakeLayout(device, { CIS, CIS, CIS, SI, SB, UB }); // 3 SRVs, UAV next_level, UAV scd output, CB comum

	for (uint32_t i = 0; i < (uint32_t)Pass::Count; i++)
	{
		VkPipelineLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutInfo.setLayoutCount = 1;
		layoutInfo.pSetLayouts = &m_descriptorSetLayouts[i];
		if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_pipelineLayouts[i]) != VK_SUCCESS)
			throw std::runtime_error("OpticalFlowVk: falha ao criar pipeline layout");

		VkPipelineShaderStageCreateInfo stageInfo{};
		stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		stageInfo.module = m_shaderModules[i];
		stageInfo.pName = "CS";

		VkComputePipelineCreateInfo pipelineInfo{};
		pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		pipelineInfo.stage = stageInfo;
		pipelineInfo.layout = m_pipelineLayouts[i];
		if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipelines[i]) != VK_SUCCESS)
			throw std::runtime_error(std::string("OpticalFlowVk: falha ao criar pipeline para pass ") + std::to_string(i));
	}
}

void OpticalFlowVk::CreateDescriptorPoolAndSets()
{
	VkDevice device = m_renderer->GetLogicalDevice();

	// Soma generosa de todos os tipos usados em todos os passes.
	VkDescriptorPoolSize poolSizes[4] = {
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 16 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 16 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 16 },
	};
	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.poolSizeCount = 4;
	poolInfo.pPoolSizes = poolSizes;
	poolInfo.maxSets = (uint32_t)Pass::Count;
	if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS)
		throw std::runtime_error("OpticalFlowVk: falha ao criar descriptor pool");

	for (uint32_t i = 0; i < (uint32_t)Pass::Count; i++)
	{
		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = m_descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &m_descriptorSetLayouts[i];
		if (vkAllocateDescriptorSets(device, &allocInfo, &m_descriptorSets[i]) != VK_SUCCESS)
			throw std::runtime_error("OpticalFlowVk: falha ao alocar descriptor set");
	}
}

void OpticalFlowVk::CreateSampler()
{
	VkSamplerCreateInfo samplerInfo{};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	if (vkCreateSampler(m_renderer->GetLogicalDevice(), &samplerInfo, nullptr, &m_colorSampler) != VK_SUCCESS)
		throw std::runtime_error("OpticalFlowVk: falha ao criar m_colorSampler");
}

void OpticalFlowVk::UpdateDescriptorSets(VkImageView currentColorView, VkImageView previousColorView)
{
	VkDevice device = m_renderer->GetLogicalDevice();

	VkDescriptorBufferInfo cbInfo{ m_constantBuffer, 0, sizeof(OpticalFlowConstants) };
	VkDescriptorBufferInfo spdCbInfo{ m_spdConstantBuffer, 0, sizeof(OpticalFlowSpdConstants) };
	VkDescriptorImageInfo currentColorInfo{ m_colorSampler, currentColorView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	VkDescriptorImageInfo previousColorInfo{ m_colorSampler, previousColorView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	VkDescriptorImageInfo lumaSampledInfo{ m_colorSampler, m_lumaView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	VkDescriptorImageInfo lumaStorageInfo{ VK_NULL_HANDLE, m_lumaView, VK_IMAGE_LAYOUT_GENERAL };
	VkDescriptorImageInfo pyramidInfo[kPyramidLevels];
	for (int i = 0; i < kPyramidLevels; i++)
		pyramidInfo[i] = { VK_NULL_HANDLE, m_pyramidViews[i], VK_IMAGE_LAYOUT_GENERAL };
	VkDescriptorImageInfo flowStorageInfo{ VK_NULL_HANDLE, m_opticalFlowVectorView, VK_IMAGE_LAYOUT_GENERAL };
	VkDescriptorImageInfo flowSampledInfo{ m_colorSampler, m_opticalFlowVectorView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	VkDescriptorBufferInfo histInfo{ m_scdHistogramBuffer, 0, VK_WHOLE_SIZE };
	VkDescriptorBufferInfo prevHistInfo{ m_scdPreviousHistogramBuffer, 0, VK_WHOLE_SIZE };
	VkDescriptorBufferInfo tempInfo{ m_scdTempBuffer, 0, VK_WHOLE_SIZE };
	VkDescriptorBufferInfo outHistInfo{ m_scdOutputBuffer, 0, VK_WHOLE_SIZE };

	auto write = [&](VkDescriptorSet set, uint32_t binding, VkDescriptorType type,
		const VkDescriptorBufferInfo* buf, const VkDescriptorImageInfo* img)
	{
		VkWriteDescriptorSet w{};
		w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		w.dstSet = set;
		w.dstBinding = binding;
		w.descriptorCount = 1;
		w.descriptorType = type;
		w.pBufferInfo = buf;
		w.pImageInfo = img;
		vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
	};

	VkDescriptorSet s;

	// Pass 0: PrepareLuma - SRV cor, UAV luma, CB comum
	s = m_descriptorSets[(uint32_t)Pass::PrepareLuma];
	write(s, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &currentColorInfo);
	write(s, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &lumaStorageInfo);
	write(s, 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &cbInfo, nullptr);

	// Pass 1: ComputeLuminancePyramid - UAV input + 6 niveis + 2 CBs
	s = m_descriptorSets[(uint32_t)Pass::ComputeLuminancePyramid];
	write(s, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &lumaStorageInfo);
	for (int i = 0; i < kPyramidLevels; i++)
		write(s, 1 + i, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &pyramidInfo[i]);
	write(s, 7, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &cbInfo, nullptr);
	write(s, 8, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &spdCbInfo, nullptr);

	// Pass 2: GenerateScdHistogram - SRV luma, UAV histograma, CB comum
	s = m_descriptorSets[(uint32_t)Pass::GenerateScdHistogram];
	write(s, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &lumaSampledInfo);
	write(s, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &histInfo, nullptr);
	write(s, 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &cbInfo, nullptr);

	// Pass 3: ComputeScdDivergence - 4 buffers + CB comum
	s = m_descriptorSets[(uint32_t)Pass::ComputeScdDivergence];
	write(s, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &histInfo, nullptr);
	write(s, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &prevHistInfo, nullptr);
	write(s, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &tempInfo, nullptr);
	write(s, 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &outHistInfo, nullptr);
	write(s, 4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &cbInfo, nullptr);

	// Pass 4: ComputeOpticalFlowAdvancedV5 - SRV luma atual+anterior, UAV flow, UAV scd out, CB
	s = m_descriptorSets[(uint32_t)Pass::ComputeOpticalFlowAdvancedV5];
	write(s, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &lumaSampledInfo);
	write(s, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &previousColorInfo); // ver TODO abaixo
	write(s, 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &flowStorageInfo);
	write(s, 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &outHistInfo, nullptr);
	write(s, 4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &cbInfo, nullptr);

	// Pass 5: FilterOpticalFlowV5 - SRV flow anterior, UAV flow, CB
	// (simplificado: le e escreve a mesma imagem final - ver aviso)
	s = m_descriptorSets[(uint32_t)Pass::FilterOpticalFlowV5];
	write(s, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &flowSampledInfo);
	write(s, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &flowStorageInfo);
	write(s, 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &cbInfo, nullptr);

	// Pass 6: ScaleOpticalFlowAdvancedV5 - 3 SRVs, UAV next_level, UAV scd out, CB
	s = m_descriptorSets[(uint32_t)Pass::ScaleOpticalFlowAdvancedV5];
	write(s, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &lumaSampledInfo);
	write(s, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &previousColorInfo);
	write(s, 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &flowSampledInfo);
	write(s, 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &flowStorageInfo);
	write(s, 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &outHistInfo, nullptr);
	write(s, 5, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &cbInfo, nullptr);

	// TODO IMPORTANTE: os passes 4, 5 e 6 acima leem E escrevem,
	// direta ou indiretamente, a MESMA imagem m_opticalFlowVectorView
	// dentro do mesmo Dispatch() (um pass le o que o anterior
	// escreveu, via visões sampled/storage da MESMA imagem). Isso
	// funciona porque ha uma barreira de memoria entre cada
	// vkCmdDispatch (ver ComputeToComputeBarrier em Dispatch()), que
	// serializa a execucao - mas e uma simplificacao em cima da
	// arquitetura real da AMD, que usa imagens SEPARADAS pra cada
	// estagio (raw/filtered/final) numa piramide de verdade. Streams
	// paralelos de GPU mais agressivos (ou drivers que reordenam
	// agressivamente) poderiam teoricamente expor um bug aqui - se
	// notar corrupção visual nos vetores gerados, esse e o primeiro
	// lugar a revisar (separar em 3 imagens reais: raw, filtered, final).
}

void OpticalFlowVk::ComputeToComputeBarrier(VkCommandBuffer cb)
{
	VkMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
	vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void OpticalFlowVk::Dispatch(VkCommandBuffer commandBuffer, VkImageView currentColorView, VkImageView previousColorView, bool reset)
{
	if (!m_resourcesTransitioned)
	{
		std::vector<VkImage> images = { m_lumaImage, m_opticalFlowVectorImage };
		for (int i = 0; i < kPyramidLevels; i++) images.push_back(m_pyramidImages[i]);

		std::vector<VkImageMemoryBarrier> barriers(images.size());
		for (size_t i = 0; i < images.size(); i++)
		{
			barriers[i] = {};
			barriers[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barriers[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			barriers[i].newLayout = VK_IMAGE_LAYOUT_GENERAL;
			barriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barriers[i].image = images[i];
			barriers[i].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			barriers[i].srcAccessMask = 0;
			barriers[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
		}
		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			0, 0, nullptr, 0, nullptr, (uint32_t)barriers.size(), barriers.data());
		m_resourcesTransitioned = true;
	}

	{
		OpticalFlowConstants c{};
		c.iInputLumaResolution[0] = (int32_t)m_width;
		c.iInputLumaResolution[1] = (int32_t)m_height;
		c.uOpticalFlowPyramidLevel = 0;
		c.uOpticalFlowPyramidLevelCount = 1;
		c.iFrameIndex = 0;
		c.backbufferTransferFunction = 0;
		c.minMaxLuminance[0] = 0.0f;
		c.minMaxLuminance[1] = 1.0f;
		void* mapped = nullptr;
		vkMapMemory(m_renderer->GetLogicalDevice(), m_constantBufferMemory, 0, sizeof(c), 0, &mapped);
		memcpy(mapped, &c, sizeof(c));
		vkUnmapMemory(m_renderer->GetLogicalDevice(), m_constantBufferMemory);
	}
	{
		OpticalFlowSpdConstants c{};
		c.mips = 1;
		c.numWorkGroups = DivRoundUp(m_width, 64) * DivRoundUp(m_height, 64);
		c.workGroupOffset[0] = 0;
		c.workGroupOffset[1] = 0;
		c.numWorkGroupOpticalFlowInputPyramid = c.numWorkGroups;
		c.pad0 = 0;
		c.pad1 = 0;
		void* mapped = nullptr;
		vkMapMemory(m_renderer->GetLogicalDevice(), m_spdConstantBufferMemory, 0, sizeof(c), 0, &mapped);
		memcpy(mapped, &c, sizeof(c));
		vkUnmapMemory(m_renderer->GetLogicalDevice(), m_spdConstantBufferMemory);
	}

	UpdateDescriptorSets(currentColorView, previousColorView);

	auto dispatchPass = [&](Pass pass, uint32_t gx, uint32_t gy, uint32_t gz = 1)
	{
		uint32_t idx = (uint32_t)pass;
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelines[idx]);
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayouts[idx],
			0, 1, &m_descriptorSets[idx], 0, nullptr);
		vkCmdDispatch(commandBuffer, gx, gy, gz);
		ComputeToComputeBarrier(commandBuffer);
	};

	// Formulas de dispatch - fieis as formulas reais da AMD quando a
	// constante correspondente e conhecida (SCD histogram/divergence);
	// simplificadas pra operacao de 1 nivel so nos passes que no SDK
	// real fazem um laco multi-nivel (luminance pyramid, optical flow
	// advanced, filter, scale) - ver aviso no topo do .h.

	// Pass 0 - PrepareLuma: threadPixels 2x2, TG 16x16
	dispatchPass(Pass::PrepareLuma,
		DivRoundUp(DivRoundUp(m_width, 2), 16),
		DivRoundUp(DivRoundUp(m_height, 2), 16));

	// Pass 1 - ComputeLuminancePyramid: convencao SPD (tile 64x64 por grupo), TG 256x1x1
	dispatchPass(Pass::ComputeLuminancePyramid,
		DivRoundUp(m_width, 64), DivRoundUp(m_height, 64));

	// Pass 2 - GenerateScdHistogram: formula real da AMD
	{
		const uint32_t strataWidth = (m_width / 4) / kHistogramsPerDim;
		dispatchPass(Pass::GenerateScdHistogram,
			DivRoundUp(strataWidth, 32), 16, kHistogramsPerDim * kHistogramsPerDim);
	}

	// Pass 3 - ComputeScdDivergence: formula real da AMD (constantes fixas)
	dispatchPass(Pass::ComputeScdDivergence, kHistogramsPerDim * kHistogramsPerDim, kHistogramShifts);

	// Pass 4 - ComputeOpticalFlowAdvancedV5: threadPixels=4, TG 64x1x1
	dispatchPass(Pass::ComputeOpticalFlowAdvancedV5,
		DivRoundUp(DivRoundUp(m_width, 4), 64), m_height);

	// Pass 5 - FilterOpticalFlowV5: TG 16x4x1, 1 nivel (resolucao cheia)
	dispatchPass(Pass::FilterOpticalFlowV5,
		DivRoundUp(m_width, 16), DivRoundUp(m_height, 4));

	// Pass 6 - ScaleOpticalFlowAdvancedV5: TG 4x4x4, 1 nivel
	dispatchPass(Pass::ScaleOpticalFlowAdvancedV5,
		DivRoundUp(m_width, 4), DivRoundUp(m_height, 4));

	// Copia o histograma deste frame pro buffer de "anterior", pro
	// ComputeScdDivergence do PROXIMO frame comparar - sem isso, a
	// deteccao de corte de cena nunca teria nada pra comparar.
	VkBufferCopy copyRegion{ 0, 0, kHistogramElementCount * sizeof(uint32_t) };
	vkCmdCopyBuffer(commandBuffer, m_scdHistogramBuffer, m_scdPreviousHistogramBuffer, 1, &copyRegion);

	(void)reset; // TODO: usar reset pra zerar o historico SCD no corte de cena
}

void OpticalFlowVk::DestroyAll()
{
	VkDevice device = m_renderer->GetLogicalDevice();
	vkDeviceWaitIdle(device);

	for (uint32_t i = 0; i < (uint32_t)Pass::Count; i++)
	{
		if (m_pipelines[i]) vkDestroyPipeline(device, m_pipelines[i], nullptr);
		if (m_pipelineLayouts[i]) vkDestroyPipelineLayout(device, m_pipelineLayouts[i], nullptr);
		if (m_descriptorSetLayouts[i]) vkDestroyDescriptorSetLayout(device, m_descriptorSetLayouts[i], nullptr);
		if (m_shaderModules[i]) vkDestroyShaderModule(device, m_shaderModules[i], nullptr);
	}
	if (m_descriptorPool) vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
	if (m_colorSampler) vkDestroySampler(device, m_colorSampler, nullptr);

	auto destroyImage = [&](VkImage& img, VkDeviceMemory& mem, VkImageView& view)
	{
		if (view) vkDestroyImageView(device, view, nullptr);
		if (img) vkDestroyImage(device, img, nullptr);
		if (mem) vkFreeMemory(device, mem, nullptr);
	};
	destroyImage(m_lumaImage, m_lumaMemory, m_lumaView);
	for (int i = 0; i < kPyramidLevels; i++)
		destroyImage(m_pyramidImages[i], m_pyramidMemory[i], m_pyramidViews[i]);
	destroyImage(m_opticalFlowVectorImage, m_opticalFlowVectorMemory, m_opticalFlowVectorView);

	auto destroyBuffer = [&](VkBuffer& buf, VkDeviceMemory& mem)
	{
		if (buf) vkDestroyBuffer(device, buf, nullptr);
		if (mem) vkFreeMemory(device, mem, nullptr);
	};
	destroyBuffer(m_scdHistogramBuffer, m_scdHistogramMemory);
	destroyBuffer(m_scdPreviousHistogramBuffer, m_scdPreviousHistogramMemory);
	destroyBuffer(m_scdTempBuffer, m_scdTempMemory);
	destroyBuffer(m_scdOutputBuffer, m_scdOutputMemory);
	destroyBuffer(m_constantBuffer, m_constantBufferMemory);
	destroyBuffer(m_spdConstantBuffer, m_spdConstantBufferMemory);
}
