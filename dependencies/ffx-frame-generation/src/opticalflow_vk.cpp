// opticalflow_vk.cpp
//
// VERSAO 3 - ver o comentario extenso no topo do .h antes de mexer
// aqui. Todos os bindings abaixo foram confirmados inspecionando o
// BINARIO .spv compilado (nao inferidos do .hlsl) com um script Python
// que le as OpDecorate Binding/DescriptorSet do SPIR-V diretamente.
//
// *** IMPORTANTE - ACAO NECESSARIA ANTES DE TESTAR ***
// Os 22 .spv precisam ser RECOMPILADOS com flags de shift, senao esta
// versao do codigo nao vai bater com os bindings reais (vai voltar a
// dar erro de pipeline incompativel, so que de um jeito diferente).
// Comando atualizado (adicione as 3 flags -fvk-*-shift no comando que
// voce ja usa, tanto pros 7 do Optical Flow quanto pros do Frame
// Interpolation depois):
//
//   dxc.exe -spirv "-fspv-target-env=vulkan1.1" -T cs_6_0 -E CS ^
//     -D FFX_HLSL -D FFX_GPU -I $incGpu ^
//     -fvk-b-shift 0 0 -fvk-t-shift 100 0 -fvk-u-shift 200 0 ^
//     arquivo.hlsl -Fo saida.spv
//
// O esquema escolhido: registradores b# (constant buffer) ficam em
// 0+N, t# (SRV/textura) ficam em 100+N, u# (UAV) ficam em 200+N -
// assim nunca colidem entre si dentro do mesmo pass.

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

	// Esquema de shift usado na recompilacao - ver aviso no topo do arquivo.
	constexpr uint32_t kOpticalFlowBlockSize = OpticalFlowVk::kBlockSize;
	constexpr uint32_t kShiftB = 0;
	constexpr uint32_t kShiftT = 100;
	constexpr uint32_t kShiftU = 200;

	constexpr uint32_t kHistogramBins = 256;
	constexpr uint32_t kHistogramsPerDim = 3;
	constexpr uint32_t kHistogramShifts = 3;

	uint32_t DivRoundUp(uint32_t value, uint32_t divisor) { return (value + divisor - 1) / divisor; }

	fs::path GetShaderPath(const char* fileName)
	{
		return ActiveSettings::GetDataPath("resources/ffx-frame-generation/shaders_spv/{}", fileName);
	}

	// Par (binding, tipo) - usado pra montar os layouts com bindings
	// NAO contiguos (0, 100, 200, ...), diferente da V2 que assumia
	// bindings sequenciais 0,1,2,3...
	struct BindingDesc { uint32_t binding; VkDescriptorType type; };

	VkDescriptorSetLayout MakeLayout(VkDevice device, std::initializer_list<BindingDesc> descs)
	{
		std::vector<VkDescriptorSetLayoutBinding> bindings;
		for (const auto& d : descs)
		{
			VkDescriptorSetLayoutBinding b{};
			b.binding = d.binding;
			b.descriptorType = d.type;
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

	void CreateImage2D(VulkanRenderer* renderer, VKRMemoryManager* memoryManager,
		uint32_t width, uint32_t height, VkFormat format,
		VkImage& outImage, VkDeviceMemory& outMemory, VkImageView& outView,
		VkImageUsageFlags extraUsage = 0)
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
		imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | extraUsage;
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

OpticalFlowVk::OpticalFlowVk(VulkanRenderer* renderer, uint32_t resolutionWidth, uint32_t resolutionHeight)
	: m_renderer(renderer), m_width(resolutionWidth), m_height(resolutionHeight)
{
	m_memoryManager = renderer->GetMemoryManager();
	CreateShaderModules();
	CreateIntermediateResources(resolutionWidth, resolutionHeight);
	CreatePerPassLayoutsAndPipelines();
	CreateDescriptorPoolAndSets();
}

OpticalFlowVk::~OpticalFlowVk() { DestroyAll(); }

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
	{
		// Pass::ComputeLuminancePyramid REMOVIDO da ativa: depois de
		// extensa investigacao (quad ops nao eliminaveis pelo SDK da
		// AMD, depois um shader customizado nosso que ainda assim deu
		// VK_ERROR_UNKNOWN especifico do driver Adreno), confirmamos
		// que NENHUM outro pass consome a saida dele na nossa
		// arquitetura simplificada de 1 nivel - entao o jeito mais
		// limpo e nao rodar esse pass, em vez de seguir brigando com
		// o driver por um resultado que ninguem usa.
		if (Pass(i) == Pass::ComputeLuminancePyramid)
			continue;
		m_shaderModules[i] = LoadShaderModule(kPassShaderFiles[i]);
	}
}

void OpticalFlowVk::CreateIntermediateResources(uint32_t width, uint32_t height)
{
	CreateImage2D(m_renderer, m_memoryManager, width, height, VK_FORMAT_R32_UINT, m_lumaImage, m_lumaMemory, m_lumaView);
	CreateImage2D(m_renderer, m_memoryManager, width, height, VK_FORMAT_R32_UINT, m_lumaPreviousImage, m_lumaPreviousMemory, m_lumaPreviousView);

	// Pass::ComputeLuminancePyramid removido (ver aviso em
	// CreateShaderModules) - as imagens de pirâmide nao sao mais
	// criadas, ninguem as consome.

	// (V5) SCD no HLSL real sao RWTexture2D, NAO buffers (ver
	// ffx_opticalflow_callbacks_hlsl.h): histogram/temp/output = R32_UINT,
	// previous_histogram = R32_SFLOAT. Acessados como (indice, 0).
	CreateImage2D(m_renderer, m_memoryManager, kHistogramElementCount, 1, VK_FORMAT_R32_UINT, m_scdHistogramImage, m_scdHistogramMemory, m_scdHistogramView, VK_IMAGE_USAGE_TRANSFER_DST_BIT);
	CreateImage2D(m_renderer, m_memoryManager, kHistogramElementCount, 1, VK_FORMAT_R32_SFLOAT, m_scdPreviousHistogramImage, m_scdPreviousHistogramMemory, m_scdPreviousHistogramView, VK_IMAGE_USAGE_TRANSFER_DST_BIT);
	CreateImage2D(m_renderer, m_memoryManager, kHistogramElementCount, 1, VK_FORMAT_R32_UINT, m_scdTempImage, m_scdTempMemory, m_scdTempView, VK_IMAGE_USAGE_TRANSFER_DST_BIT);
	CreateImage2D(m_renderer, m_memoryManager, kHistogramElementCount, 1, VK_FORMAT_R32_UINT, m_scdOutputImage, m_scdOutputMemory, m_scdOutputView, VK_IMAGE_USAGE_TRANSFER_DST_BIT);

	// Tamanho da textura de flow = ceil(w/8) x ceil(h/8) (GetOpticalFlowTextureSize do SDK)
	const uint32_t flowW = DivRoundUp(width, kOpticalFlowBlockSize), flowH = DivRoundUp(height, kOpticalFlowBlockSize);
	// 3 estagios do calculo do flow em si: raw (pass4) -> filtered (pass5) -> final (pass6)
	CreateImage2D(m_renderer, m_memoryManager, flowW, flowH, VK_FORMAT_R32G32_SINT, m_opticalFlowRawImage, m_opticalFlowRawMemory, m_opticalFlowRawView);
	CreateImage2D(m_renderer, m_memoryManager, flowW, flowH, VK_FORMAT_R32G32_SINT, m_opticalFlowFilteredImage, m_opticalFlowFilteredMemory, m_opticalFlowFilteredView);
	CreateImage2D(m_renderer, m_memoryManager, flowW, flowH, VK_FORMAT_R32G32_SINT, m_opticalFlowVectorImage, m_opticalFlowVectorMemory, m_opticalFlowVectorView);

	if (!m_memoryManager->CreateBuffer(sizeof(OpticalFlowConstants), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_constantBuffer, m_constantBufferMemory))
		throw std::runtime_error("OpticalFlowVk: falha ao criar m_constantBuffer");
}

void OpticalFlowVk::CreatePerPassLayoutsAndPipelines()
{
	VkDevice device = m_renderer->GetLogicalDevice();
	constexpr auto UB = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	constexpr auto SI = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;   // SEM sampler - Load() puro
	constexpr auto ST = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	constexpr auto SB = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; // SCD e RWTexture2D, nao buffer (V5)

	// Layouts EXATOS confirmados via inspecao binaria. Os numeros de
	// binding usam o esquema de shift (b+0, t+100, u+200) - ver aviso
	// no topo do arquivo sobre a flag de recompilacao necessaria.

	m_descriptorSetLayouts[(uint32_t)Pass::PrepareLuma] = MakeLayout(device, {
		{ 0 + kShiftB, UB },    // cbOF
		{ 0 + kShiftT, SI },    // r_input_color
		{ 0 + kShiftU, ST },    // rw_optical_flow_input
	});

	// Pass::ComputeLuminancePyramid removido - sem layout, sem pipeline.

	m_descriptorSetLayouts[(uint32_t)Pass::GenerateScdHistogram] = MakeLayout(device, {
		{ 0 + kShiftB, UB },    // cbOF
		{ 0 + kShiftT, SI },    // r_optical_flow_input
		{ 0 + kShiftU, SB },    // rw_optical_flow_scd_histogram
	});

	m_descriptorSetLayouts[(uint32_t)Pass::ComputeScdDivergence] = MakeLayout(device, {
		{ 0 + kShiftU, SB }, { 1 + kShiftU, SB }, { 2 + kShiftU, SB }, { 3 + kShiftU, SB },
		// SEM constant buffer - confirmado
	});

	m_descriptorSetLayouts[(uint32_t)Pass::ComputeOpticalFlowAdvancedV5] = MakeLayout(device, {
		{ 0 + kShiftB, UB },    // cbOF
		{ 0 + kShiftT, SI },    // r_optical_flow_input (luma atual)
		{ 1 + kShiftT, SI },    // r_optical_flow_previous_input (luma anterior)
		{ 0 + kShiftU, ST },    // rw_optical_flow
		{ 1 + kShiftU, SB },    // rw_optical_flow_scd_output
	});

	m_descriptorSetLayouts[(uint32_t)Pass::FilterOpticalFlowV5] = MakeLayout(device, {
		{ 0 + kShiftT, SI },    // r_optical_flow_previous (le o raw)
		{ 0 + kShiftU, ST },    // rw_optical_flow (escreve o filtered)
		// SEM constant buffer - confirmado
	});

	m_descriptorSetLayouts[(uint32_t)Pass::ScaleOpticalFlowAdvancedV5] = MakeLayout(device, {
		{ 0 + kShiftB, UB },    // cbOF
		{ 0 + kShiftT, SI },    // r_optical_flow_input (luma atual)
		{ 1 + kShiftT, SI },    // r_optical_flow_previous_input (luma anterior)
		{ 2 + kShiftT, SI },    // r_optical_flow (le o filtered)
		{ 0 + kShiftU, ST },    // rw_optical_flow_next_level (escreve o FINAL)
		{ 1 + kShiftU, SB },    // rw_optical_flow_scd_output
	});

	for (uint32_t i = 0; i < (uint32_t)Pass::Count; i++)
	{
		if (Pass(i) == Pass::ComputeLuminancePyramid)
			continue; // removido - ver aviso em CreateShaderModules

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
		VkResult pipelineResult = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipelines[i]);
		if (pipelineResult != VK_SUCCESS)
			throw std::runtime_error(std::string("OpticalFlowVk: falha ao criar pipeline para pass ")
				+ std::to_string(i) + " (VkResult=" + std::to_string((int)pipelineResult) + ")");
	}
}

void OpticalFlowVk::CreateDescriptorPoolAndSets()
{
	VkDevice device = m_renderer->GetLogicalDevice();
	VkDescriptorPoolSize poolSizes[3] = {
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 16 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 32 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 16 },
	};
	// UNIFORM_BUFFER somado a parte pra nao exagerar na contagem (so 4 passes usam)
	VkDescriptorPoolSize poolSizesFull[4] = { poolSizes[0], poolSizes[1], poolSizes[2], { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 8 } };

	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.poolSizeCount = 4;
	poolInfo.pPoolSizes = poolSizesFull;
	poolInfo.maxSets = (uint32_t)Pass::Count + 1; // +1 pro set extra do PrepareLuma (luma anterior)
	if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS)
		throw std::runtime_error("OpticalFlowVk: falha ao criar descriptor pool");

	for (uint32_t i = 0; i < (uint32_t)Pass::Count; i++)
	{
		if (Pass(i) == Pass::ComputeLuminancePyramid)
			continue; // removido - ver aviso em CreateShaderModules

		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = m_descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &m_descriptorSetLayouts[i];
		if (vkAllocateDescriptorSets(device, &allocInfo, &m_descriptorSets[i]) != VK_SUCCESS)
			throw std::runtime_error("OpticalFlowVk: falha ao alocar descriptor set");
	}

	// Descriptor set extra pra segunda chamada do PrepareLuma (luma do
	// frame anterior) - mesmo layout do Pass::PrepareLuma.
	{
		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = m_descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &m_descriptorSetLayouts[(uint32_t)Pass::PrepareLuma];
		if (vkAllocateDescriptorSets(device, &allocInfo, &m_descriptorSetPrepareLumaPrevious) != VK_SUCCESS)
			throw std::runtime_error("OpticalFlowVk: falha ao alocar descriptor set (PrepareLuma previous)");
	}
}

void OpticalFlowVk::UpdateDescriptorSets(VkImageView currentColorView, VkImageView previousColorView)
{
	VkDevice device = m_renderer->GetLogicalDevice();

	VkDescriptorBufferInfo cbInfo{ m_constantBuffer, 0, sizeof(OpticalFlowConstants) };
	VkDescriptorImageInfo currentColorInfo{ VK_NULL_HANDLE, currentColorView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	VkDescriptorImageInfo previousColorInfo{ VK_NULL_HANDLE, previousColorView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	VkDescriptorImageInfo lumaStorageInfo{ VK_NULL_HANDLE, m_lumaView, VK_IMAGE_LAYOUT_GENERAL };
	VkDescriptorImageInfo lumaPreviousStorageInfo{ VK_NULL_HANDLE, m_lumaPreviousView, VK_IMAGE_LAYOUT_GENERAL };
	VkDescriptorImageInfo lumaSampledInfo{ VK_NULL_HANDLE, m_lumaView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	VkDescriptorImageInfo lumaPreviousSampledInfo{ VK_NULL_HANDLE, m_lumaPreviousView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	VkDescriptorImageInfo rawStorageInfo{ VK_NULL_HANDLE, m_opticalFlowRawView, VK_IMAGE_LAYOUT_GENERAL };
	VkDescriptorImageInfo rawSampledInfo{ VK_NULL_HANDLE, m_opticalFlowRawView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	VkDescriptorImageInfo filteredStorageInfo{ VK_NULL_HANDLE, m_opticalFlowFilteredView, VK_IMAGE_LAYOUT_GENERAL };
	VkDescriptorImageInfo filteredSampledInfo{ VK_NULL_HANDLE, m_opticalFlowFilteredView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	VkDescriptorImageInfo finalStorageInfo{ VK_NULL_HANDLE, m_opticalFlowVectorView, VK_IMAGE_LAYOUT_GENERAL };
	VkDescriptorImageInfo histInfo{ VK_NULL_HANDLE, m_scdHistogramView, VK_IMAGE_LAYOUT_GENERAL };
	VkDescriptorImageInfo prevHistInfo{ VK_NULL_HANDLE, m_scdPreviousHistogramView, VK_IMAGE_LAYOUT_GENERAL };
	VkDescriptorImageInfo tempInfo{ VK_NULL_HANDLE, m_scdTempView, VK_IMAGE_LAYOUT_GENERAL };
	VkDescriptorImageInfo outHistInfo{ VK_NULL_HANDLE, m_scdOutputView, VK_IMAGE_LAYOUT_GENERAL };

	auto write = [&](VkDescriptorSet set, uint32_t binding, VkDescriptorType type,
		const VkDescriptorBufferInfo* buf, const VkDescriptorImageInfo* img)
	{
		VkWriteDescriptorSet w{};
		w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		w.dstSet = set; w.dstBinding = binding; w.descriptorCount = 1; w.descriptorType = type;
		w.pBufferInfo = buf; w.pImageInfo = img;
		vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
	};

	VkDescriptorSet s;

	// Pass 0 (PrepareLuma) e chamado 2x no Dispatch() com descriptor
	// sets DIFERENTES (um pra cor atual->luma atual, outro pra cor
	// anterior->luma anterior) - usamos os 2 ultimos slots do array
	// de descriptor sets emprestados do enum (ver nota no Dispatch()).
	s = m_descriptorSets[(uint32_t)Pass::PrepareLuma];
	write(s, 0 + kShiftB, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &cbInfo, nullptr);
	write(s, 0 + kShiftT, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, nullptr, &currentColorInfo);
	write(s, 0 + kShiftU, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &lumaStorageInfo);

	s = m_descriptorSetPrepareLumaPrevious;
	write(s, 0 + kShiftB, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &cbInfo, nullptr);
	write(s, 0 + kShiftT, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, nullptr, &previousColorInfo);
	write(s, 0 + kShiftU, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &lumaPreviousStorageInfo);

	// Pass::ComputeLuminancePyramid removido - sem descriptor set pra escrever.

	s = m_descriptorSets[(uint32_t)Pass::GenerateScdHistogram];
	write(s, 0 + kShiftB, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &cbInfo, nullptr);
	write(s, 0 + kShiftT, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, nullptr, &lumaSampledInfo);
	write(s, 0 + kShiftU, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &histInfo);

	s = m_descriptorSets[(uint32_t)Pass::ComputeScdDivergence];
	write(s, 0 + kShiftU, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &histInfo);
	write(s, 1 + kShiftU, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &prevHistInfo);
	write(s, 2 + kShiftU, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &tempInfo);
	write(s, 3 + kShiftU, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &outHistInfo);

	s = m_descriptorSets[(uint32_t)Pass::ComputeOpticalFlowAdvancedV5];
	write(s, 0 + kShiftB, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &cbInfo, nullptr);
	write(s, 0 + kShiftT, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, nullptr, &lumaSampledInfo);
	write(s, 1 + kShiftT, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, nullptr, &lumaPreviousSampledInfo);
	write(s, 0 + kShiftU, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &rawStorageInfo);
	write(s, 1 + kShiftU, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &outHistInfo);

	s = m_descriptorSets[(uint32_t)Pass::FilterOpticalFlowV5];
	write(s, 0 + kShiftT, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, nullptr, &rawSampledInfo);
	write(s, 0 + kShiftU, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &finalStorageInfo);

	s = m_descriptorSets[(uint32_t)Pass::ScaleOpticalFlowAdvancedV5];
	write(s, 0 + kShiftB, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &cbInfo, nullptr);
	write(s, 0 + kShiftT, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, nullptr, &lumaSampledInfo);
	write(s, 1 + kShiftT, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, nullptr, &lumaPreviousSampledInfo);
	write(s, 2 + kShiftT, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, nullptr, &filteredSampledInfo);
	write(s, 0 + kShiftU, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &finalStorageInfo);
	write(s, 1 + kShiftU, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &outHistInfo);
}

void OpticalFlowVk::ComputeToComputeBarrier(VkCommandBuffer cb)
{
	VkMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
	vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void OpticalFlowVk::Dispatch(VkCommandBuffer commandBuffer, VkImageView currentColorView, VkImageView previousColorView, bool reset)
{
	if (!m_resourcesTransitioned)
	{
		std::vector<VkImage> images = { m_lumaImage, m_lumaPreviousImage, m_opticalFlowRawImage, m_opticalFlowFilteredImage, m_opticalFlowVectorImage,
			m_scdHistogramImage, m_scdPreviousHistogramImage, m_scdTempImage, m_scdOutputImage };
		// Pass::ComputeLuminancePyramid removido - imagens de piramide nao existem mais, nada a transicionar aqui.

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

	UpdateDescriptorSets(currentColorView, previousColorView);

	// Zera os acumuladores do SCD (InterlockedAdd) a cada frame; o
	// "previous histogram" so no primeiro frame (SDK faz o mesmo no reset).
	{
		VkClearColorValue zero{};
		VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		vkCmdClearColorImage(commandBuffer, m_scdHistogramImage, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &range);
		vkCmdClearColorImage(commandBuffer, m_scdTempImage, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &range);
		vkCmdClearColorImage(commandBuffer, m_scdOutputImage, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &range);
		if (m_frameCounter == 0 || reset)
			vkCmdClearColorImage(commandBuffer, m_scdPreviousHistogramImage, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &range);
		m_frameCounter++;

		VkMemoryBarrier mb{};
		mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
	}

	auto dispatchWithSet = [&](Pass pass, VkDescriptorSet set, uint32_t gx, uint32_t gy, uint32_t gz = 1)
	{
		uint32_t idx = (uint32_t)pass;
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelines[idx]);
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayouts[idx], 0, 1, &set, 0, nullptr);
		vkCmdDispatch(commandBuffer, gx, gy, gz);
		ComputeToComputeBarrier(commandBuffer);
	};
	auto dispatchPass = [&](Pass pass, uint32_t gx, uint32_t gy, uint32_t gz = 1)
	{
		dispatchWithSet(pass, m_descriptorSets[(uint32_t)pass], gx, gy, gz);
	};

	const uint32_t tgPrepareLuma_x = DivRoundUp(DivRoundUp(m_width, 2), 16);
	const uint32_t tgPrepareLuma_y = DivRoundUp(DivRoundUp(m_height, 2), 16);

	// Pass 0 RODADO 2X: luma atual (descriptor set normal) e luma
	// anterior (descriptor set separado m_descriptorSetPrepareLumaPrevious)
	dispatchPass(Pass::PrepareLuma, tgPrepareLuma_x, tgPrepareLuma_y);
	dispatchWithSet(Pass::PrepareLuma, m_descriptorSetPrepareLumaPrevious, tgPrepareLuma_x, tgPrepareLuma_y);

	// Pass::ComputeLuminancePyramid removido - nao despachado (ver aviso em CreateShaderModules).

	{
		const uint32_t strataWidth = (m_width / 4) / kHistogramsPerDim;
		dispatchPass(Pass::GenerateScdHistogram, DivRoundUp(strataWidth, 32), 16, kHistogramsPerDim * kHistogramsPerDim);
	}

	dispatchPass(Pass::ComputeScdDivergence, kHistogramsPerDim * kHistogramsPerDim, kHistogramShifts);

	// Formulas de ffx_opticalflow.cpp (nivel 0): compute = ((ceil(w/4)*16+63)/64, ceil(h/16));
	// filter = (ceil(flowW/16), ceil(flowH/4)) escrevendo direto o vetor FINAL.
	// O pass Scale so roda ENTRE niveis da piramide - nao no nivel 0.
	{
		const uint32_t flowW = DivRoundUp(m_width, kOpticalFlowBlockSize);
		const uint32_t flowH = DivRoundUp(m_height, kOpticalFlowBlockSize);
		dispatchPass(Pass::ComputeOpticalFlowAdvancedV5, (DivRoundUp(m_width, 4) * 16 + 63) / 64, DivRoundUp(m_height, 16));
		dispatchPass(Pass::FilterOpticalFlowV5, DivRoundUp(flowW, 16), DivRoundUp(flowH, 4));
	}

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

	auto destroyImage = [&](VkImage& img, VkDeviceMemory& mem, VkImageView& view)
	{
		if (view) vkDestroyImageView(device, view, nullptr);
		if (img) vkDestroyImage(device, img, nullptr);
		if (mem) vkFreeMemory(device, mem, nullptr);
	};
	destroyImage(m_lumaImage, m_lumaMemory, m_lumaView);
	destroyImage(m_lumaPreviousImage, m_lumaPreviousMemory, m_lumaPreviousView);
	// Pass::ComputeLuminancePyramid removido - imagens de piramide nunca sao criadas, nada a destruir aqui.
	destroyImage(m_opticalFlowRawImage, m_opticalFlowRawMemory, m_opticalFlowRawView);
	destroyImage(m_opticalFlowFilteredImage, m_opticalFlowFilteredMemory, m_opticalFlowFilteredView);
	destroyImage(m_opticalFlowVectorImage, m_opticalFlowVectorMemory, m_opticalFlowVectorView);

	auto destroyBuffer = [&](VkBuffer& buf, VkDeviceMemory& mem)
	{
		if (buf) vkDestroyBuffer(device, buf, nullptr);
		if (mem) vkFreeMemory(device, mem, nullptr);
	};
	destroyImage(m_scdHistogramImage, m_scdHistogramMemory, m_scdHistogramView);
	destroyImage(m_scdPreviousHistogramImage, m_scdPreviousHistogramMemory, m_scdPreviousHistogramView);
	destroyImage(m_scdTempImage, m_scdTempMemory, m_scdTempView);
	destroyImage(m_scdOutputImage, m_scdOutputMemory, m_scdOutputView);
	destroyBuffer(m_constantBuffer, m_constantBufferMemory);
}
