// opticalflow_vk.cpp
//
// Ver o comentario no topo de opticalflow_vk.h antes de mexer aqui -
// esta e uma PRIMEIRA VERSAO simplificada (sem piramide de mip),
// feita pra validar o pipeline ponta a ponta.

#include "opticalflow_vk.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanRenderer.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VKRMemoryManager.h"

#include <fstream>
#include <vector>
#include <stdexcept>
#include <cstring>

// Estrutura do cbuffer cbOF (FFX_OPTICALFLOW_BIND_CB_COMMON), copiada
// campo a campo de ffx_opticalflow_callbacks_hlsl.h - se a AMD mudar
// esse layout numa atualizacao do SDK, atualize aqui tambem.
struct OpticalFlowConstants
{
	int32_t  iInputLumaResolution[2];
	uint32_t uOpticalFlowPyramidLevel;
	uint32_t uOpticalFlowPyramidLevelCount;
	uint32_t iFrameIndex;
	uint32_t backbufferTransferFunction;
	float    minMaxLuminance[2];
};

namespace
{
	// Nomes dos .spv na mesma ordem do enum OpticalFlowVk::Pass -
	// tem que bater 1:1 com a pasta shaders_spv/
	constexpr const char* kPassShaderFiles[(uint32_t)OpticalFlowVk::Pass::Count] = {
		"ffx_opticalflow_prepare_luma_pass.spv",
		"ffx_opticalflow_compute_luminance_pyramid_pass.spv",
		"ffx_opticalflow_generate_scd_histogram_pass.spv",
		"ffx_opticalflow_compute_scd_divergence_pass.spv",
		"ffx_opticalflow_compute_optical_flow_advanced_pass_v5.spv",
		"ffx_opticalflow_filter_optical_flow_pass_v5.spv",
		"ffx_opticalflow_scale_optical_flow_advanced_pass_v5.spv",
	};

	// TODO: confirme este valor abrindo ffx_opticalflow_resources.h ou
	// o define real de FFX_OPTICALFLOW_THREAD_GROUP_WIDTH/HEIGHT no SDK -
	// 8x8 e o padrao mais comum nos passes de imagem do FFX SDK, mas
	// NAO foi confirmado linha a linha para este shader especifico.
	constexpr uint32_t kThreadGroupWidth = 8;
	constexpr uint32_t kThreadGroupHeight = 8;

	uint32_t DivRoundUp(uint32_t value, uint32_t divisor)
	{
		return (value + divisor - 1) / divisor;
	}

	// Caminho relativo onde os .spv ficam dentro do pacote da dependencia.
	// Ajuste aqui se voce mover a pasta shaders_spv/ de lugar.
	std::string GetShaderPath(const char* fileName)
	{
		return std::string("dependencies/ffx-frame-generation/shaders_spv/") + fileName;
	}
}

OpticalFlowVk::OpticalFlowVk(VulkanRenderer* renderer, uint32_t resolutionWidth, uint32_t resolutionHeight)
	: m_renderer(renderer), m_width(resolutionWidth), m_height(resolutionHeight)
{
	m_memoryManager = renderer->GetMemoryManager();

	CreateShaderModules();
	CreateIntermediateResources(resolutionWidth, resolutionHeight);
	CreatePipelines();
	CreateDescriptorSets();
	CreateSampler();
}

OpticalFlowVk::~OpticalFlowVk()
{
	DestroyAll();
}

VkShaderModule OpticalFlowVk::LoadShaderModule(const char* spvFileName) const
{
	std::string path = GetShaderPath(spvFileName);
	std::ifstream file(path, std::ios::ate | std::ios::binary);
	if (!file.is_open())
		throw std::runtime_error(std::string("OpticalFlowVk: nao consegui abrir o shader ") + path);

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
	VkResult result = vkCreateShaderModule(m_renderer->GetLogicalDevice(), &createInfo, nullptr, &module);
	if (result != VK_SUCCESS)
		throw std::runtime_error(std::string("OpticalFlowVk: vkCreateShaderModule falhou para ") + spvFileName);

	return module;
}

void OpticalFlowVk::CreateShaderModules()
{
	for (uint32_t i = 0; i < (uint32_t)Pass::Count; i++)
		m_shaderModules[i] = LoadShaderModule(kPassShaderFiles[i]);
}

void OpticalFlowVk::CreateIntermediateResources(uint32_t width, uint32_t height)
{
	VkDevice device = m_renderer->GetLogicalDevice();

	// --- imagem de luma (R32F, escrita pelo prepare_luma, lida pelos passes seguintes) ---
	{
		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.extent = { width, height, 1 };
		imageInfo.mipLevels = 1; // TODO: piramide completa usa 6 niveis - ver header
		imageInfo.arrayLayers = 1;
		imageInfo.format = VK_FORMAT_R32_SFLOAT;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

		if (vkCreateImage(device, &imageInfo, nullptr, &m_lumaImage) != VK_SUCCESS)
			throw std::runtime_error("OpticalFlowVk: falha ao criar m_lumaImage");

		VkMemoryRequirements memReq;
		vkGetImageMemoryRequirements(device, m_lumaImage, &memReq);

		uint32_t memoryTypeIndex = 0;
		if (!m_memoryManager->FindMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memoryTypeIndex))
			throw std::runtime_error("OpticalFlowVk: nao achei memory type compativel para m_lumaImage");

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memReq.size;
		allocInfo.memoryTypeIndex = memoryTypeIndex;
		if (vkAllocateMemory(device, &allocInfo, nullptr, &m_lumaMemory) != VK_SUCCESS)
			throw std::runtime_error("OpticalFlowVk: vkAllocateMemory falhou para m_lumaImage");
		vkBindImageMemory(device, m_lumaImage, m_lumaMemory, 0);

		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = m_lumaImage;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = VK_FORMAT_R32_SFLOAT;
		viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		if (vkCreateImageView(device, &viewInfo, nullptr, &m_lumaView) != VK_SUCCESS)
			throw std::runtime_error("OpticalFlowVk: falha ao criar m_lumaView");
	}

	// --- imagem de saida: vetores de movimento (RG16F) ---
	{
		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.extent = { width, height, 1 };
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.format = VK_FORMAT_R16G16_SFLOAT;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

		if (vkCreateImage(device, &imageInfo, nullptr, &m_opticalFlowVectorImage) != VK_SUCCESS)
			throw std::runtime_error("OpticalFlowVk: falha ao criar m_opticalFlowVectorImage");

		VkMemoryRequirements memReq;
		vkGetImageMemoryRequirements(device, m_opticalFlowVectorImage, &memReq);

		uint32_t memoryTypeIndex = 0;
		if (!m_memoryManager->FindMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memoryTypeIndex))
			throw std::runtime_error("OpticalFlowVk: nao achei memory type compativel para m_opticalFlowVectorImage");

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memReq.size;
		allocInfo.memoryTypeIndex = memoryTypeIndex;
		if (vkAllocateMemory(device, &allocInfo, nullptr, &m_opticalFlowVectorMemory) != VK_SUCCESS)
			throw std::runtime_error("OpticalFlowVk: vkAllocateMemory falhou para m_opticalFlowVectorImage");
		vkBindImageMemory(device, m_opticalFlowVectorImage, m_opticalFlowVectorMemory, 0);

		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = m_opticalFlowVectorImage;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = VK_FORMAT_R16G16_SFLOAT;
		viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		if (vkCreateImageView(device, &viewInfo, nullptr, &m_opticalFlowVectorView) != VK_SUCCESS)
			throw std::runtime_error("OpticalFlowVk: falha ao criar m_opticalFlowVectorView");
	}

	// --- buffer do histograma de scene-change-detection ---
	// TODO: a AMD usa DOIS buffers (atual + anterior) para comparar
	// histograma entre frames - esta versao simplificada usa so 1.
	{
		const VkDeviceSize histogramSize = 256 * sizeof(uint32_t); // 256 buckets, ajuste se necessario
		bool ok = m_memoryManager->CreateBuffer(
			histogramSize,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			m_scdHistogramBuffer,
			m_scdHistogramMemory);
		if (!ok)
			throw std::runtime_error("OpticalFlowVk: falha ao criar m_scdHistogramBuffer");
	}

	// --- buffer de constantes (cbOF) ---
	{
		bool ok = m_memoryManager->CreateBuffer(
			sizeof(OpticalFlowConstants),
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			m_constantBuffer,
			m_constantBufferMemory);
		if (!ok)
			throw std::runtime_error("OpticalFlowVk: falha ao criar m_constantBuffer");
	}
}

void OpticalFlowVk::CreatePipelines()
{
	VkDevice device = m_renderer->GetLogicalDevice();

	// --- descriptor set layout ---
	// Simplificado: 1 UBO (constantes) + 1 imagem de entrada (cor) +
	// 1 imagem de luma + 1 imagem de saida (vetores) + 1 buffer (histograma).
	// Cada pass usa um subconjunto disso, mas usar o mesmo layout pros
	// 7 passes simplifica a primeira versao (custa um pouco de
	// eficiencia de binding, nao de correcao).
	VkDescriptorSetLayoutBinding bindings[5]{};
	bindings[0] = { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
	bindings[1] = { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }; // cor de entrada
	bindings[2] = { 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };          // luma (leitura/escrita)
	bindings[3] = { 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };          // vetores de saida
	bindings[4] = { 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };         // histograma SCD

	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = 5;
	layoutInfo.pBindings = bindings;
	if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS)
		throw std::runtime_error("OpticalFlowVk: falha ao criar descriptor set layout");

	VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 1;
	pipelineLayoutInfo.pSetLayouts = &m_descriptorSetLayout;
	if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS)
		throw std::runtime_error("OpticalFlowVk: falha ao criar pipeline layout");

	for (uint32_t i = 0; i < (uint32_t)Pass::Count; i++)
	{
		VkPipelineShaderStageCreateInfo stageInfo{};
		stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		stageInfo.module = m_shaderModules[i];
		stageInfo.pName = "CS"; // ponto de entrada confirmado no codigo-fonte HLSL

		VkComputePipelineCreateInfo pipelineInfo{};
		pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		pipelineInfo.stage = stageInfo;
		pipelineInfo.layout = m_pipelineLayout;

		if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipelines[i]) != VK_SUCCESS)
			throw std::runtime_error(std::string("OpticalFlowVk: falha ao criar pipeline para pass ") + std::to_string(i));
	}
}

void OpticalFlowVk::CreateDescriptorSets()
{
	VkDevice device = m_renderer->GetLogicalDevice();

	VkDescriptorPoolSize poolSizes[3]{};
	poolSizes[0] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, (uint32_t)Pass::Count };
	poolSizes[1] = { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, (uint32_t)Pass::Count * 2 };
	poolSizes[2] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, (uint32_t)Pass::Count };

	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.poolSizeCount = 3;
	poolInfo.pPoolSizes = poolSizes;
	poolInfo.maxSets = (uint32_t)Pass::Count;
	if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS)
		throw std::runtime_error("OpticalFlowVk: falha ao criar descriptor pool");

	std::vector<VkDescriptorSetLayout> layouts((uint32_t)Pass::Count, m_descriptorSetLayout);
	VkDescriptorSetAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = m_descriptorPool;
	allocInfo.descriptorSetCount = (uint32_t)Pass::Count;
	allocInfo.pSetLayouts = layouts.data();
	if (vkAllocateDescriptorSets(device, &allocInfo, m_descriptorSets) != VK_SUCCESS)
		throw std::runtime_error("OpticalFlowVk: falha ao alocar descriptor sets");

	// NOTA: o binding de qual VkImageView vai em cada descriptor set
	// (cor atual vs anterior, luma, vetores) depende de qual frame
	// esta sendo processado - por isso e feito dentro de Dispatch(),
	// nao aqui na inicializacao. Ver Dispatch() abaixo.
}

void OpticalFlowVk::CreateSampler()
{
	// Sampler simples, sem filtro anisotropico - a imagem de entrada e
	// lida pixel a pixel pelo prepare_luma, nao precisa de nada
	// sofisticado aqui.
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
		throw std::runtime_error("OpticalFlowVk: falha ao criar m_colorSampler");
}

void OpticalFlowVk::UpdateDescriptorSets(VkImageView currentColorView)
{
	// Escreve os 5 bindings (ver CreatePipelines() para o layout) em
	// CADA um dos 7 descriptor sets - um por pass. Nesta versao
	// simplificada todo pass recebe o mesmo conjunto de recursos; o
	// shader de cada pass so le os bindings que ele de fato declarou
	// no HLSL (ver aviso no .h sobre isso nao ser 100% fiel ao layout
	// real de binding da AMD, que varia por pass).
	VkDevice device = m_renderer->GetLogicalDevice();

	for (uint32_t i = 0; i < (uint32_t)Pass::Count; i++)
	{
		VkDescriptorBufferInfo constantBufferInfo{};
		constantBufferInfo.buffer = m_constantBuffer;
		constantBufferInfo.offset = 0;
		constantBufferInfo.range = sizeof(OpticalFlowConstants);

		VkDescriptorImageInfo colorImageInfo{};
		colorImageInfo.sampler = m_colorSampler;
		colorImageInfo.imageView = currentColorView;
		colorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkDescriptorImageInfo lumaImageInfo{};
		lumaImageInfo.imageView = m_lumaView;
		lumaImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL; // storage image precisa de GENERAL

		VkDescriptorImageInfo vectorImageInfo{};
		vectorImageInfo.imageView = m_opticalFlowVectorView;
		vectorImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

		VkDescriptorBufferInfo histogramBufferInfo{};
		histogramBufferInfo.buffer = m_scdHistogramBuffer;
		histogramBufferInfo.offset = 0;
		histogramBufferInfo.range = VK_WHOLE_SIZE;

		VkWriteDescriptorSet writes[5]{};

		writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[0].dstSet = m_descriptorSets[i];
		writes[0].dstBinding = 0;
		writes[0].descriptorCount = 1;
		writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		writes[0].pBufferInfo = &constantBufferInfo;

		writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[1].dstSet = m_descriptorSets[i];
		writes[1].dstBinding = 1;
		writes[1].descriptorCount = 1;
		writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[1].pImageInfo = &colorImageInfo;

		writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[2].dstSet = m_descriptorSets[i];
		writes[2].dstBinding = 2;
		writes[2].descriptorCount = 1;
		writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		writes[2].pImageInfo = &lumaImageInfo;

		writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[3].dstSet = m_descriptorSets[i];
		writes[3].dstBinding = 3;
		writes[3].descriptorCount = 1;
		writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		writes[3].pImageInfo = &vectorImageInfo;

		writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[4].dstSet = m_descriptorSets[i];
		writes[4].dstBinding = 4;
		writes[4].descriptorCount = 1;
		writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		writes[4].pBufferInfo = &histogramBufferInfo;

		vkUpdateDescriptorSets(device, 5, writes, 0, nullptr);
	}

	// NOTA IMPORTANTE: m_lumaView e m_opticalFlowVectorView estao
	// como VK_IMAGE_LAYOUT_GENERAL acima porque e o layout exigido
	// para uso como storage image. Isso pressupoe que essas imagens
	// ja foram transicionadas pra esse layout (com
	// vkCmdPipelineBarrier, mudando de VK_IMAGE_LAYOUT_UNDEFINED para
	// VK_IMAGE_LAYOUT_GENERAL) ANTES da primeira chamada de Dispatch().
	// Isso ainda nao esta feito automaticamente neste arquivo - se o
	// Vulkan Validation Layer acusar erro de layout, e por causa
	// disso. Ver TODO equivalente a ser resolvido na proxima etapa,
	// idealmente dentro de CreateIntermediateResources() logo apos
	// criar cada imagem.
}

void OpticalFlowVk::ComputeToComputeBarrier(VkCommandBuffer cb)
{
	VkMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

	vkCmdPipelineBarrier(cb,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void OpticalFlowVk::Dispatch(VkCommandBuffer commandBuffer, VkImageView currentColorView, VkImageView previousColorView, bool reset)
{
	// Na primeira chamada, transiciona as imagens de storage
	// (m_lumaImage, m_opticalFlowVectorImage) de UNDEFINED para
	// GENERAL - layout exigido pra uso como storage image nos
	// shaders. Sem isso, o Vulkan Validation Layer acusa erro assim
	// que o primeiro vkCmdDispatch tentar ler/escrever nelas.
	if (!m_resourcesTransitioned)
	{
		VkImageMemoryBarrier barriers[2]{};

		barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barriers[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barriers[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
		barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barriers[0].image = m_lumaImage;
		barriers[0].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		barriers[0].srcAccessMask = 0;
		barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

		barriers[1] = barriers[0];
		barriers[1].image = m_opticalFlowVectorImage;

		vkCmdPipelineBarrier(commandBuffer,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			0, 0, nullptr, 0, nullptr, 2, barriers);

		m_resourcesTransitioned = true;
	}

	// 1) Atualiza o buffer de constantes para este frame
	{
		OpticalFlowConstants constants{};
		constants.iInputLumaResolution[0] = (int32_t)m_width;
		constants.iInputLumaResolution[1] = (int32_t)m_height;
		constants.uOpticalFlowPyramidLevel = 0;
		constants.uOpticalFlowPyramidLevelCount = 1; // TODO: 6 quando a piramide completa existir
		constants.iFrameIndex = 0; // TODO: incrementar por frame real, vindo do Cemu
		constants.backbufferTransferFunction = 0; // 0 = linear/sRGB simples - ajustar conforme o backbuffer do Cemu
		constants.minMaxLuminance[0] = 0.0f;
		constants.minMaxLuminance[1] = 1.0f;

		void* mapped = nullptr;
		vkMapMemory(m_renderer->GetLogicalDevice(), m_constantBufferMemory, 0, sizeof(constants), 0, &mapped);
		memcpy(mapped, &constants, sizeof(constants));
		vkUnmapMemory(m_renderer->GetLogicalDevice(), m_constantBufferMemory);
	}

	// TODO: escrever aqui a atualizacao do binding de previousColorView
	// tambem - nesta versao so currentColorView esta sendo passado pro
	// shader (binding 1). O prepare_luma real da AMD usa cor atual E
	// anterior para decidir o "reset" de cena; por enquanto o parametro
	// previousColorView fica sem uso (ver "(void)reset" mais abaixo,
	// mesma limitacao).
	UpdateDescriptorSets(currentColorView);
	(void)previousColorView;

	uint32_t groupsX = DivRoundUp(m_width, kThreadGroupWidth);
	uint32_t groupsY = DivRoundUp(m_height, kThreadGroupHeight);

	// 2) Executa os 7 passes, na ordem exata do enum FfxOpticalflowPass original
	for (uint32_t i = 0; i < (uint32_t)Pass::Count; i++)
	{
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelines[i]);
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout,
			0, 1, &m_descriptorSets[i], 0, nullptr);

		vkCmdDispatch(commandBuffer, groupsX, groupsY, 1);

		// barreira entre cada pass - o pass seguinte le o que este escreveu
		if (i + 1 < (uint32_t)Pass::Count)
			ComputeToComputeBarrier(commandBuffer);
	}

	(void)reset; // TODO: quando reset==true, pular a comparacao com o frame
	             // anterior (equivalente ao "camera cut" do FSR3 original)
}

void OpticalFlowVk::DestroyAll()
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

	if (m_lumaView) vkDestroyImageView(device, m_lumaView, nullptr);
	if (m_lumaImage) vkDestroyImage(device, m_lumaImage, nullptr);
	if (m_lumaMemory) vkFreeMemory(device, m_lumaMemory, nullptr);

	if (m_opticalFlowVectorView) vkDestroyImageView(device, m_opticalFlowVectorView, nullptr);
	if (m_opticalFlowVectorImage) vkDestroyImage(device, m_opticalFlowVectorImage, nullptr);
	if (m_opticalFlowVectorMemory) vkFreeMemory(device, m_opticalFlowVectorMemory, nullptr);

	if (m_scdHistogramBuffer) vkDestroyBuffer(device, m_scdHistogramBuffer, nullptr);
	if (m_scdHistogramMemory) vkFreeMemory(device, m_scdHistogramMemory, nullptr);

	if (m_constantBuffer) vkDestroyBuffer(device, m_constantBuffer, nullptr);
	if (m_constantBufferMemory) vkFreeMemory(device, m_constantBufferMemory, nullptr);

	if (m_colorSampler) vkDestroySampler(device, m_colorSampler, nullptr);
}
