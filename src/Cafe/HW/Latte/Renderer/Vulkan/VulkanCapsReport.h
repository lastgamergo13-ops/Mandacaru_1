#pragma once
// Relatorio completo de capacidades do Vulkan no log: cada feature/limite/formato/extensao com true/false.
// Serve para ver o que o driver NAO suporta (false) e o que o emulador esta usando.
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"
#include "Cemu/Logging/CemuLogging.h"
#include <fmt/format.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>

namespace VkCapsReport
{
	inline const char* B(bool v) { return v ? "true" : "false"; }

	inline void Log(VkPhysicalDevice pd, const std::vector<const char*>& usedExtensions, const std::vector<std::pair<std::string, bool>>& emuFlags)
	{
		VkPhysicalDeviceProperties props{};
		vkGetPhysicalDeviceProperties(pd, &props);
		const uint32 v = props.apiVersion;
		cemuLog_log(LogType::Force, "[VkCaps] ===== relatorio Vulkan =====");
		cemuLog_log(LogType::Force, "[VkCaps] device='{}' vendorID=0x{:04x} deviceID=0x{:04x} type={} apiVersion={}.{}.{} driverVersion=0x{:08x}",
			props.deviceName, props.vendorID, props.deviceID, (int)props.deviceType, VK_VERSION_MAJOR(v), VK_VERSION_MINOR(v), VK_VERSION_PATCH(v), props.driverVersion);

		// ---- features basicas
		VkPhysicalDeviceFeatures2 f2{};
		f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		vkGetPhysicalDeviceFeatures2(pd, &f2);
		const VkPhysicalDeviceFeatures& f = f2.features;
		std::string s;
#define FT(n) s += fmt::format("{}={} ", #n, B(f.n))
		FT(robustBufferAccess); FT(fullDrawIndexUint32); FT(imageCubeArray); FT(independentBlend); FT(geometryShader); FT(tessellationShader);
		FT(sampleRateShading); FT(dualSrcBlend); FT(logicOp); FT(multiDrawIndirect); FT(drawIndirectFirstInstance); FT(depthClamp); FT(depthBiasClamp);
		FT(fillModeNonSolid); FT(depthBounds); FT(wideLines); FT(largePoints); FT(alphaToOne); FT(multiViewport); FT(samplerAnisotropy);
		FT(textureCompressionETC2); FT(textureCompressionASTC_LDR); FT(textureCompressionBC); FT(occlusionQueryPrecise); FT(pipelineStatisticsQuery);
		FT(vertexPipelineStoresAndAtomics); FT(fragmentStoresAndAtomics); FT(shaderTessellationAndGeometryPointSize); FT(shaderImageGatherExtended);
		FT(shaderStorageImageExtendedFormats); FT(shaderStorageImageMultisample); FT(shaderStorageImageReadWithoutFormat); FT(shaderStorageImageWriteWithoutFormat);
		FT(shaderUniformBufferArrayDynamicIndexing); FT(shaderSampledImageArrayDynamicIndexing); FT(shaderStorageBufferArrayDynamicIndexing);
		FT(shaderClipDistance); FT(shaderCullDistance); FT(shaderFloat64); FT(shaderInt64); FT(shaderInt16); FT(shaderResourceResidency); FT(shaderResourceMinLod);
		FT(sparseBinding); FT(variableMultisampleRate); FT(inheritedQueries);
#undef FT
		cemuLog_log(LogType::Force, "[VkCaps] features: {}", s);

		// ---- limites principais
		const VkPhysicalDeviceLimits& l = props.limits;
		cemuLog_log(LogType::Force, "[VkCaps] limits: maxImageDimension2D={} maxImageArrayLayers={} maxUniformBufferRange={} maxStorageBufferRange={} maxPushConstantsSize={} maxMemoryAllocationCount={} maxBoundDescriptorSets={}",
			l.maxImageDimension2D, l.maxImageArrayLayers, l.maxUniformBufferRange, l.maxStorageBufferRange, l.maxPushConstantsSize, l.maxMemoryAllocationCount, l.maxBoundDescriptorSets);
		cemuLog_log(LogType::Force, "[VkCaps] limits: maxPerStageDescriptorSamplers={} maxPerStageDescriptorUniformBuffers={} maxPerStageDescriptorStorageBuffers={} maxPerStageDescriptorSampledImages={} maxDescriptorSetUniformBuffersDynamic={} maxVertexInputAttributes={} maxVertexInputBindings={} maxColorAttachments={} maxSamplerAnisotropy={}",
			l.maxPerStageDescriptorSamplers, l.maxPerStageDescriptorUniformBuffers, l.maxPerStageDescriptorStorageBuffers, l.maxPerStageDescriptorSampledImages, l.maxDescriptorSetUniformBuffersDynamic, l.maxVertexInputAttributes, l.maxVertexInputBindings, l.maxColorAttachments, l.maxSamplerAnisotropy);
		cemuLog_log(LogType::Force, "[VkCaps] limits: minUniformBufferOffsetAlignment={} minStorageBufferOffsetAlignment={} nonCoherentAtomSize={} bufferImageGranularity={} maxSamplerAllocationCount={} maxFramebufferWidth={} maxViewports={}",
			l.minUniformBufferOffsetAlignment, l.minStorageBufferOffsetAlignment, l.nonCoherentAtomSize, l.bufferImageGranularity, l.maxSamplerAllocationCount, l.maxFramebufferWidth, l.maxViewports);

		// ---- formatos usados pelo Wii U
		struct Fm { VkFormat f; const char* n; };
		static const Fm fmts[] = {
			{VK_FORMAT_BC1_RGBA_UNORM_BLOCK,"BC1_RGBA_UNORM"},{VK_FORMAT_BC1_RGBA_SRGB_BLOCK,"BC1_RGBA_SRGB"},{VK_FORMAT_BC2_UNORM_BLOCK,"BC2_UNORM"},
			{VK_FORMAT_BC2_SRGB_BLOCK,"BC2_SRGB"},{VK_FORMAT_BC3_UNORM_BLOCK,"BC3_UNORM"},{VK_FORMAT_BC3_SRGB_BLOCK,"BC3_SRGB"},
			{VK_FORMAT_BC4_UNORM_BLOCK,"BC4_UNORM"},{VK_FORMAT_BC4_SNORM_BLOCK,"BC4_SNORM"},{VK_FORMAT_BC5_UNORM_BLOCK,"BC5_UNORM"},{VK_FORMAT_BC5_SNORM_BLOCK,"BC5_SNORM"},
			{VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK,"ETC2_RGBA8"},{VK_FORMAT_ASTC_4x4_UNORM_BLOCK,"ASTC_4x4"},
			{VK_FORMAT_D24_UNORM_S8_UINT,"D24_S8"},{VK_FORMAT_D32_SFLOAT_S8_UINT,"D32_S8"},{VK_FORMAT_D32_SFLOAT,"D32"},{VK_FORMAT_D16_UNORM,"D16"},{VK_FORMAT_X8_D24_UNORM_PACK32,"X8_D24"},
			{VK_FORMAT_A1R5G5B5_UNORM_PACK16,"A1R5G5B5"},{VK_FORMAT_R5G6B5_UNORM_PACK16,"R5G6B5"},{VK_FORMAT_R4G4B4A4_UNORM_PACK16,"R4G4B4A4"},{VK_FORMAT_B10G11R11_UFLOAT_PACK32,"B10G11R11"},
			{VK_FORMAT_A2B10G10R10_UNORM_PACK32,"A2B10G10R10"},{VK_FORMAT_R16G16B16A16_SFLOAT,"RGBA16F"},{VK_FORMAT_R32G32B32A32_SFLOAT,"RGBA32F"},{VK_FORMAT_R16_UNORM,"R16_UNORM"},{VK_FORMAT_R8G8B8A8_SRGB,"RGBA8_SRGB"},
		};
		for (const Fm& m : fmts)
		{
			VkFormatProperties fp{};
			vkGetPhysicalDeviceFormatProperties(pd, m.f, &fp);
			const auto o = fp.optimalTilingFeatures;
			cemuLog_log(LogType::Force, "[VkCaps] format {}: sampled={} linearFilter={} colorAtt={} blend={} depthStencilAtt={} blitSrc={} blitDst={} transferSrc={} transferDst={}", m.n,
				B(o & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT), B(o & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT), B(o & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT),
				B(o & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT), B(o & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT), B(o & VK_FORMAT_FEATURE_BLIT_SRC_BIT),
				B(o & VK_FORMAT_FEATURE_BLIT_DST_BIT), B(o & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT), B(o & VK_FORMAT_FEATURE_TRANSFER_DST_BIT));
		}

		// ---- extensoes do device (marca quais o emulador ativou)
		uint32_t cnt = 0;
		vkEnumerateDeviceExtensionProperties(pd, nullptr, &cnt, nullptr);
		std::vector<VkExtensionProperties> exts(cnt);
		vkEnumerateDeviceExtensionProperties(pd, nullptr, &cnt, exts.data());
		std::sort(exts.begin(), exts.end(), [](const VkExtensionProperties& a, const VkExtensionProperties& b) { return strcmp(a.extensionName, b.extensionName) < 0; });
		cemuLog_log(LogType::Force, "[VkCaps] {} extensoes de device suportadas ([ON]=ativada pelo emulador):", cnt);
		for (const auto& e : exts)
		{
			bool on = false;
			for (const char* u : usedExtensions) if (u && strcmp(u, e.extensionName) == 0) on = true;
			cemuLog_log(LogType::Force, "[VkCaps]   {} {} v{}", on ? "[ON] " : "[off]", e.extensionName, e.specVersion);
		}
		// extensoes pedidas que o driver nao tem
		for (const char* u : usedExtensions)
		{
			bool found = false;
			for (const auto& e : exts) if (u && strcmp(u, e.extensionName) == 0) found = true;
			if (!found) cemuLog_log(LogType::Force, "[VkCaps] AVISO: extensao pedida mas NAO listada pelo driver: {}", u ? u : "?");
		}

		// ---- decisoes internas do emulador
		for (const auto& p : emuFlags)
			cemuLog_log(LogType::Force, "[VkCaps] emulador usa {} = {}", p.first, B(p.second));
		cemuLog_log(LogType::Force, "[VkCaps] ===== fim do relatorio =====");
	}
}
