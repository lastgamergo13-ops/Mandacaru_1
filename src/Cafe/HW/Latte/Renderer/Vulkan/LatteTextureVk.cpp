#include "Cafe/HW/Latte/Renderer/Vulkan/LatteTextureVk.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/LatteTextureViewVk.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanRenderer.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"

LatteTextureVk::LatteTextureVk(class VulkanRenderer* vkRenderer, Latte::E_DIM dim, MPTR physAddress, MPTR physMipAddress, Latte::E_GX2SURFFMT format, uint32 width, uint32 height, uint32 depth, uint32 pitch, uint32 mipLevels, uint32 swizzle,
	Latte::E_HWTILEMODE tileMode, bool isDepth)
	: LatteTexture(dim, physAddress, physMipAddress, format, width, height, depth, pitch, mipLevels, swizzle, tileMode, isDepth), m_vkr(vkRenderer)
{
	vkObjTex = new VKRObjectTexture();

	VkImageCreateInfo imageInfo{};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	
	sint32 effectiveBaseWidth = width;
	sint32 effectiveBaseHeight = height;
	sint32 effectiveBaseDepth = depth;
	if (overwriteInfo.hasResolutionOverwrite)
	{
		effectiveBaseWidth = overwriteInfo.width;
		effectiveBaseHeight = overwriteInfo.height;
		effectiveBaseDepth = overwriteInfo.depth;
	}
	effectiveBaseDepth = std::max(1, effectiveBaseDepth);

	imageInfo.extent.width = effectiveBaseWidth;
	imageInfo.extent.height = effectiveBaseHeight;
	imageInfo.mipLevels = mipLevels;
	imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

	if (dim == Latte::E_DIM::DIM_3D)
	{
		imageInfo.extent.depth = effectiveBaseDepth;
		imageInfo.arrayLayers = 1;
		imageInfo.flags |= VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT;
	}
	else
	{
		imageInfo.extent.depth = 1;
		imageInfo.arrayLayers = effectiveBaseDepth;
		if (dim != Latte::E_DIM::DIM_1D && (effectiveBaseDepth % 6) == 0 && effectiveBaseWidth == effectiveBaseHeight)
			imageInfo.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
	}
	
	VulkanRenderer::FormatInfoVK texFormatInfo;
	vkRenderer->GetTextureFormatInfoVK(format, isDepth, dim, effectiveBaseWidth, effectiveBaseHeight, &texFormatInfo);
	cemu_assert_debug(hasStencil == ((texFormatInfo.vkImageAspect & VK_IMAGE_ASPECT_STENCIL_BIT) != 0));
	imageInfo.format = texFormatInfo.vkImageFormat;
	vkObjTex->m_imageAspect = texFormatInfo.vkImageAspect;
	
	if (isDepth == false && texFormatInfo.isCompressed)
	{
		imageInfo.flags |= VK_IMAGE_CREATE_BLOCK_TEXEL_VIEW_COMPATIBLE_BIT;
	}
	if (isDepth == false)
		imageInfo.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;

	if (isDepth)
	{
		imageInfo.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	}
	else
	{
		// Not every format that Cemu maps GX2 surface formats to is guaranteed by the Vulkan
		// spec to support being used as a color attachment (VK_FORMAT_R4G4_UNORM_PACK8 is a
		// known example) - and some formats that ARE spec-optional for this (like
		// VK_FORMAT_R8_UNORM) are outright unsupported as attachments on some real-world
		// Android GPU drivers (observed on some Adreno devices: the driver accepts the image
		// but then fails/hangs trying to actually back it with memory). Rather than hardcode
		// a per-format exception list, ask the driver directly and only request the
		// attachment usage bit when it's actually supported - this keeps regular sampled-only
		// textures with formats like R8_UNORM working even where they can't be render targets.
		if (Latte::IsCompressedFormat(format) == false)
		{
			VkFormatProperties fmtProp{};
			vkGetPhysicalDeviceFormatProperties(m_vkr->GetPhysicalDevice(), texFormatInfo.vkImageFormat, &fmtProp);
			bool supportsColorAttachment = (fmtProp.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) != 0;
			if (supportsColorAttachment)
				imageInfo.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
			else
				cemuLog_log(LogType::Force, "Texture format {} does not support being used as a color attachment on this GPU/driver - creating as sampled-only texture instead", (uint32)texFormatInfo.vkImageFormat);
		}
	}

	if (m_vkr->UseAttachmentFeedbackLoop() && (imageInfo.usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) != 0)
	{
		imageInfo.usage |= VK_IMAGE_USAGE_ATTACHMENT_FEEDBACK_LOOP_BIT_EXT;
		m_defaultLayout = VK_IMAGE_LAYOUT_ATTACHMENT_FEEDBACK_LOOP_OPTIMAL_EXT;
	}

	if (dim == Latte::E_DIM::DIM_2D)
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
	else if (dim == Latte::E_DIM::DIM_1D)
		imageInfo.imageType = VK_IMAGE_TYPE_1D;
	else if (dim == Latte::E_DIM::DIM_3D)
		imageInfo.imageType = VK_IMAGE_TYPE_3D;
	else if (dim == Latte::E_DIM::DIM_2D_ARRAY)
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
	else if (dim == Latte::E_DIM::DIM_CUBEMAP)
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
	else if (dim == Latte::E_DIM::DIM_2D_MSAA)
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
	else
	{
		cemu_assert_unimplemented();
	}

	if (vkCreateImage(m_vkr->GetLogicalDevice(), &imageInfo, nullptr, &vkObjTex->m_image) != VK_SUCCESS)
		m_vkr->UnrecoverableError("Failed to create texture image");
	
	if (m_vkr->IsDebugMarkersEnabled())
	{
		VkDebugUtilsObjectNameInfoEXT objName{};
		objName.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
		objName.objectType = VK_OBJECT_TYPE_IMAGE;
		objName.pNext = nullptr;
		objName.objectHandle = (uint64_t)vkObjTex->m_image;
		auto objNameStr = fmt::format("tex_{:08x}_fmt{:04x}_tm{:x}", physAddress, (uint32)format, (uint32)tileMode);
		objName.pObjectName = objNameStr.c_str();
		vkSetDebugUtilsObjectNameEXT(m_vkr->GetLogicalDevice(), &objName);
	}

	vkObjTex->m_flags = imageInfo.flags;
	vkObjTex->m_format = imageInfo.format;

	// init layout array
	m_layoutsMips = std::max(mipLevels, 1u); // todo - use effective mip count
	m_layoutsDepth = std::max(depth, 1u);
	if (Is3DTexture())
		m_layouts.resize(m_layoutsMips, VK_IMAGE_LAYOUT_UNDEFINED); // one per mip
	else
		m_layouts.resize(m_layoutsMips * m_layoutsDepth, VK_IMAGE_LAYOUT_UNDEFINED); // one per layer per mip
}

LatteTextureVk::~LatteTextureVk()
{
	cemu_assert_debug(views.empty());

	m_vkr->surfaceCopy_notifyTextureRelease(this);

	VulkanRenderer::GetInstance()->ReleaseDestructibleObject(vkObjTex);
	vkObjTex = nullptr;
}

LatteTextureView* LatteTextureVk::CreateView(Latte::E_DIM dim, Latte::E_GX2SURFFMT format, sint32 firstMip, sint32 mipCount, sint32 firstSlice, sint32 sliceCount)
{
	cemu_assert_debug(mipCount > 0);
	cemu_assert_debug(sliceCount > 0);
	cemu_assert_debug((firstMip + mipCount) <= this->mipLevels);
	cemu_assert_debug((firstSlice + sliceCount) <= this->depth);
	return new LatteTextureViewVk(m_vkr->GetLogicalDevice(), this, dim, format, firstMip, mipCount, firstSlice, sliceCount);
}

void LatteTextureVk::AllocateOnHost()
{
	auto allocationInfo = VulkanRenderer::GetInstance()->GetMemoryManager()->imageMemoryAllocate(GetImageObj()->m_image);
	vkObjTex->m_allocation = allocationInfo;
}
