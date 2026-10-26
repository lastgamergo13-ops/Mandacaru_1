#include "Cafe/HW/Latte/Renderer/OpenGL/LatteTextureGL.h"
#ifndef GL_SRGB8_ALPHA8
#define GL_SRGB8_ALPHA8 0x8C43
#endif
#ifndef GL_R8_SNORM
#define GL_R8_SNORM 0x8F94
#endif
#ifndef GL_RG8_SNORM
#define GL_RG8_SNORM 0x8F95
#endif
#include "Cafe/HW/Latte/Renderer/OpenGL/LatteTextureViewGL.h"
#include "Cafe/HW/Latte/Renderer/OpenGL/OpenGLRenderer.h"
#include "Cafe/HW/Latte/Core/Latte.h"
#include "config/CemuConfig.h"

#include "config/LaunchSettings.h"

LatteTextureGL::LatteTextureGL(Latte::E_DIM dim, MPTR physAddress, MPTR physMipAddress, Latte::E_GX2SURFFMT format, uint32 width, uint32 height, uint32 depth, uint32 pitch, uint32 mipLevels, uint32 swizzle,
	Latte::E_HWTILEMODE tileMode, bool isDepth)
	: LatteTexture(dim, physAddress, physMipAddress, format, width, height, depth, pitch, mipLevels, swizzle, tileMode, isDepth)
{
	GenerateEmptyTextureFromGX2Dim(dim, this->glId_texture, this->glTexTarget, true);
	// set format info
	FormatInfoGL glFormatInfo;
	GetOpenGLFormatInfo(isDepth, overwriteInfo.hasFormatOverwrite ? (Latte::E_GX2SURFFMT)overwriteInfo.format : format, dim, &glFormatInfo);
	this->glInternalFormat = glFormatInfo.glInternalFormat;
	this->isAlternativeFormat = glFormatInfo.isUsingAlternativeFormat;
	// set debug name
	bool useGLDebugNames = false;
#ifdef CEMU_DEBUG_ASSERT
	useGLDebugNames = true;
#endif
	if (LaunchSettings::NSightModeEnabled())
		useGLDebugNames = true;
	if (useGLDebugNames)
	{
		char textureDebugLabel[512];
		sprintf(textureDebugLabel, "%08x_f%04x%s_p%04x_%dx%d", physAddress, (uint32)format, this->isDepth ? "_d" : "", pitch, width, height);
		glObjectLabel(GL_TEXTURE, this->glId_texture, -1, textureDebugLabel);
	}
}

LatteTextureGL::~LatteTextureGL()
{
	glDeleteTextures(1, &glId_texture);
	catchOpenGLError();
}

void LatteTextureGL::GenerateEmptyTextureFromGX2Dim(Latte::E_DIM dim, GLuint& texId, GLint& texTarget, bool createForTargetType)
{
	if (dim == Latte::E_DIM::DIM_2D)
		texTarget = GL_TEXTURE_2D;
	else if (dim == Latte::E_DIM::DIM_1D)
	{
		// GLES has no GL_TEXTURE_1D target at all (not even via extension) - the closest
		// equivalent, and the one this port uses, is a regular 2D texture with height 1.
		// See AllocateStorage() above for the matching storage-allocation side of this, and
		// note the GLSL sampler1D->sampler2D change this still requires in the shader
		// decompiler, which is not part of this change (see the project's OpenGL-on-Android
		// technical report, Etapa 4/6).
		texTarget = OpenGLRenderer::IsGLES() ? GL_TEXTURE_2D : GL_TEXTURE_1D;
	}
	else if (dim == Latte::E_DIM::DIM_3D)
		texTarget = GL_TEXTURE_3D;
	else if (dim == Latte::E_DIM::DIM_2D_ARRAY)
		texTarget = GL_TEXTURE_2D_ARRAY;
	else if (dim == Latte::E_DIM::DIM_CUBEMAP)
		texTarget = GL_TEXTURE_CUBE_MAP_ARRAY;
	else if (dim == Latte::E_DIM::DIM_2D_MSAA)
		texTarget = GL_TEXTURE_2D; // todo, GL_TEXTURE_2D_MULTISAMPLE ?
	else
	{
		cemu_assert_unimplemented();
	}
	if(createForTargetType)
		texId = glCreateTextureWrapper(texTarget); // initializes the texture to texTarget (equivalent to calling glGenTextures + glBindTexture)
	else
		glGenTextures(1, &texId);
}

LatteTextureView* LatteTextureGL::CreateView(Latte::E_DIM dim, Latte::E_GX2SURFFMT format, sint32 firstMip, sint32 mipCount, sint32 firstSlice, sint32 sliceCount)
{
	return new LatteTextureViewGL(this, dim, format, firstMip, mipCount, firstSlice, sliceCount);
}

void LatteTextureGL::GetOpenGLFormatInfo(bool isDepth, Latte::E_GX2SURFFMT format, Latte::E_DIM dim, FormatInfoGL* formatInfoOut)
{
	formatInfoOut->isUsingAlternativeFormat = false;

	if (isDepth)
	{
		if (format == Latte::E_GX2SURFFMT::D24_S8_UNORM)
		{
			formatInfoOut->setFormat(GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8);
			return;
		}
		else if (format == Latte::E_GX2SURFFMT::D24_S8_FLOAT)
		{
			formatInfoOut->setFormat(GL_DEPTH32F_STENCIL8, GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV);
			formatInfoOut->markAsAlternativeFormat();
			return;
		}
		else if (format == Latte::E_GX2SURFFMT::D32_S8_FLOAT)
		{
			formatInfoOut->setFormat(GL_DEPTH32F_STENCIL8, GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV);
			return;
		}
		else if (format == Latte::E_GX2SURFFMT::D32_FLOAT)
		{
			formatInfoOut->setFormat(GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT);
			return;
		}
		else if (format == Latte::E_GX2SURFFMT::D16_UNORM)
		{
			formatInfoOut->setFormat(GL_DEPTH_COMPONENT16, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT);
			return;
		}
		// unsupported depth format
		cemuLog_log(LogType::Force, "OpenGL: Unsupported texture depth format 0x{:04x}", (uint32)format);
		// use placeholder format
		formatInfoOut->setFormat(GL_DEPTH_COMPONENT16, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT);
		formatInfoOut->markAsAlternativeFormat();
		return;
	}

	bool glIsCompressed = false;
	bool isUsingAlternativeFormat = false; // set to true if there is no bit-perfect matching OpenGL format
	sint32 glInternalFormat;
	sint32 glSuppliedFormat;
	sint32 glSuppliedFormatType;
	// get format information
	if (format == Latte::E_GX2SURFFMT::R4_G4_UNORM)
	{
		formatInfoOut->setFormat(GL_RGBA4, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4);
		formatInfoOut->markAsAlternativeFormat();
		return;
	}
	else if (format == Latte::E_GX2SURFFMT::R4_G4_B4_A4_UNORM)
	{
		formatInfoOut->setFormat(GL_RGBA4, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4);
		return;
	}
	else if (format == Latte::E_GX2SURFFMT::R16_G16_B16_A16_FLOAT)
	{
		formatInfoOut->setFormat(GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT);
		return;
	}
	else if (format == Latte::E_GX2SURFFMT::R16_G16_FLOAT)
	{
		formatInfoOut->setFormat(GL_RG16F, GL_RG, GL_HALF_FLOAT);
		return;
	}
	else if (format == Latte::E_GX2SURFFMT::R16_SNORM)
	{
		formatInfoOut->setFormat(GL_R16_SNORM, GL_RED, GL_SHORT);
		return;
	}
	else if (format == Latte::E_GX2SURFFMT::R16_FLOAT)
	{
		formatInfoOut->setFormat(GL_R16F, GL_RED, GL_HALF_FLOAT);
		return;
	}
	else if (format == Latte::E_GX2SURFFMT::BC1_UNORM ||
		format == Latte::E_GX2SURFFMT::BC1_SRGB)
	{
		bool isSrgb = format == Latte::E_GX2SURFFMT::BC1_SRGB;
		if (OpenGLRenderer::GetInstance()->m_supportedFormatInfo.fmt_bc1)
			formatInfoOut->setCompressed(isSrgb ? GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT : GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, -1, -1);
		else if ((GetConfig().texture_compression_bc1 && OpenGLRenderer::CanUseEtc2Transcode(dim)) && OpenGLRenderer::GetInstance()->m_supportedFormatInfo.fmt_etc2)
			formatInfoOut->setCompressed(isSrgb ? GL_COMPRESSED_SRGB8_ETC2 : GL_COMPRESSED_RGB8_ETC2, -1, -1);
		else
		{
			formatInfoOut->setFormat(isSrgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
			formatInfoOut->markAsAlternativeFormat();
		}
		return;
	}
	else if (format == Latte::E_GX2SURFFMT::BC2_UNORM || format == Latte::E_GX2SURFFMT::BC2_SRGB)
	{
		bool isSrgb = format == Latte::E_GX2SURFFMT::BC2_SRGB;
		if (OpenGLRenderer::GetInstance()->m_supportedFormatInfo.fmt_bc2)
			formatInfoOut->setCompressed(isSrgb ? 0x8C4E /*GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT*/ : 0x83F2 /*GL_COMPRESSED_RGBA_S3TC_DXT3_EXT*/, -1, -1);
		else if ((GetConfig().texture_compression_bc2 && OpenGLRenderer::CanUseEtc2Transcode(dim)) && OpenGLRenderer::GetInstance()->m_supportedFormatInfo.fmt_etc2)
			formatInfoOut->setCompressed(isSrgb ? GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC : GL_COMPRESSED_RGBA8_ETC2_EAC, -1, -1);
		else
		{
			formatInfoOut->setFormat(isSrgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
			formatInfoOut->markAsAlternativeFormat();
		}
		return;
	}
	else if (format == Latte::E_GX2SURFFMT::BC3_UNORM || format == Latte::E_GX2SURFFMT::BC3_SRGB)
	{
		bool isSrgb = format == Latte::E_GX2SURFFMT::BC3_SRGB;
		if (OpenGLRenderer::GetInstance()->m_supportedFormatInfo.fmt_bc3)
			formatInfoOut->setCompressed(isSrgb ? GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, -1, -1);
		else if ((GetConfig().texture_compression_bc3 && OpenGLRenderer::CanUseEtc2Transcode(dim)) && OpenGLRenderer::GetInstance()->m_supportedFormatInfo.fmt_etc2)
			formatInfoOut->setCompressed(isSrgb ? GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC : GL_COMPRESSED_RGBA8_ETC2_EAC, -1, -1);
		else
		{
			formatInfoOut->setFormat(isSrgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
			formatInfoOut->markAsAlternativeFormat();
		}
		return;
	}
	else if (format == Latte::E_GX2SURFFMT::BC4_UNORM || format == Latte::E_GX2SURFFMT::BC4_SNORM)
	{
		bool isSnorm = format == Latte::E_GX2SURFFMT::BC4_SNORM;
		bool allowCompressed = true;
		if (dim != Latte::E_DIM::DIM_2D && dim != Latte::E_DIM::DIM_2D_ARRAY)
			allowCompressed = false; // RGTC1/EAC do not support non-2D textures here
		if (allowCompressed && OpenGLRenderer::GetInstance()->m_supportedFormatInfo.fmt_bc4)
		{
			formatInfoOut->setCompressed(isSnorm ? GL_COMPRESSED_SIGNED_RED_RGTC1 : GL_COMPRESSED_RED_RGTC1, -1, -1);
			return;
		}
		else if (allowCompressed && (GetConfig().texture_compression_bc4 && OpenGLRenderer::CanUseEtc2Transcode(dim)) && OpenGLRenderer::GetInstance()->m_supportedFormatInfo.fmt_etc2)
		{
			formatInfoOut->setCompressed(isSnorm ? GL_COMPRESSED_SIGNED_R11_EAC : GL_COMPRESSED_R11_EAC, -1, -1);
			return;
		}
		else if (allowCompressed)
		{
			if (isSnorm)
				formatInfoOut->setFormat(GL_R8_SNORM, GL_RED, GL_BYTE);
			else
				formatInfoOut->setFormat(GL_R8, GL_RED, GL_UNSIGNED_BYTE);
			formatInfoOut->markAsAlternativeFormat();
			return;
		}
		else
		{
			formatInfoOut->setFormat(GL_RG16F, GL_RG, GL_FLOAT);
			formatInfoOut->markAsAlternativeFormat();
			return;
		}
	}
	else if (format == Latte::E_GX2SURFFMT::BC5_UNORM || format == Latte::E_GX2SURFFMT::BC5_SNORM)
	{
		bool isSnorm = format == Latte::E_GX2SURFFMT::BC5_SNORM;
		if (OpenGLRenderer::GetInstance()->m_supportedFormatInfo.fmt_bc5)
			formatInfoOut->setCompressed(isSnorm ? GL_COMPRESSED_SIGNED_RG_RGTC2 : GL_COMPRESSED_RG_RGTC2, -1, -1);
		else if ((GetConfig().texture_compression_bc5 && OpenGLRenderer::CanUseEtc2Transcode(dim)) && OpenGLRenderer::GetInstance()->m_supportedFormatInfo.fmt_etc2)
			formatInfoOut->setCompressed(isSnorm ? GL_COMPRESSED_SIGNED_RG11_EAC : GL_COMPRESSED_RG11_EAC, -1, -1);
		else
		{
			if (isSnorm)
				formatInfoOut->setFormat(GL_RG8_SNORM, GL_RG, GL_BYTE);
			else
				formatInfoOut->setFormat(GL_RG8, GL_RG, GL_UNSIGNED_BYTE);
			formatInfoOut->markAsAlternativeFormat();
		}
		return;
	}
	else if (format == Latte::E_GX2SURFFMT::R32_FLOAT)
	{
		formatInfoOut->setFormat(GL_R32F, GL_RED, GL_FLOAT);
		return;
	}
	else if (format == Latte::E_GX2SURFFMT::R32_G32_FLOAT)
	{
		formatInfoOut->setFormat(GL_RG32F, GL_RG, GL_FLOAT);
		return;
	}
	else if (format == Latte::E_GX2SURFFMT::R32_G32_UINT)
	{
		formatInfoOut->setFormat(GL_RG32UI, GL_RG_INTEGER, GL_UNSIGNED_INT);
		return;
	}
	else if (format == Latte::E_GX2SURFFMT::R32_UINT)
	{
		formatInfoOut->setFormat(GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT);
		return;
	}
	else if (format == Latte::E_GX2SURFFMT::R16_UINT)
	{
		// used by VC DS (New Super Mario Bros)
		formatInfoOut->setFormat(GL_R16UI, GL_RED_INTEGER, GL_UNSIGNED_SHORT);
		return;
	}
	else if (format == Latte::E_GX2SURFFMT::R8_UINT)
	{
		// used by VC DS (New Super Mario Bros)
		formatInfoOut->setFormat(GL_R8UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE);
		return;
	}
	else if (format == Latte::E_GX2SURFFMT::R32_G32_B32_A32_FLOAT)
	{
		formatInfoOut->setFormat(GL_RGBA32F, GL_RGBA, GL_FLOAT);
		return;
	}
	else if (format == Latte::E_GX2SURFFMT::R8_G8_B8_A8_UNORM || format == Latte::E_GX2SURFFMT::R8_G8_B8_A8_SRGB)
	{
		if (format == Latte::E_GX2SURFFMT::R8_G8_B8_A8_SRGB)
			glInternalFormat = GL_SRGB8_ALPHA8;
		else
			glInternalFormat = GL_RGBA8;
		// supplied format
		glSuppliedFormat = GL_RGBA;
		glSuppliedFormatType = GL_UNSIGNED_BYTE;
		glIsCompressed = false;
	}
	else if (format == Latte::E_GX2SURFFMT::R8_G8_B8_A8_SNORM)
	{
		glInternalFormat = GL_RGBA8_SNORM;
		// supplied format
		glSuppliedFormat = GL_RGBA;
		glSuppliedFormatType = GL_BYTE;
		glIsCompressed = false;
	}
	else if (format == Latte::E_GX2SURFFMT::R8_UNORM)
	{
		glInternalFormat = GL_R8;
		// supplied format
		glSuppliedFormat = GL_RED;
		glSuppliedFormatType = GL_UNSIGNED_BYTE;
		glIsCompressed = false;
	}
	else if (format == Latte::E_GX2SURFFMT::R8_SNORM)
	{
		glInternalFormat = GL_R8_SNORM;
		// supplied format
		glSuppliedFormat = GL_RED;
		glSuppliedFormatType = GL_BYTE;
		glIsCompressed = false;
	}
	else if (format == Latte::E_GX2SURFFMT::R8_G8_UNORM)
	{
		glInternalFormat = GL_RG8;
		// supplied format
		glSuppliedFormat = GL_RG;
		glSuppliedFormatType = GL_UNSIGNED_BYTE;
		glIsCompressed = false;
	}
	else if (format == Latte::E_GX2SURFFMT::R8_G8_SNORM)
	{
		glInternalFormat = GL_RG8_SNORM;
		// supplied format
		glSuppliedFormat = GL_RG;
		glSuppliedFormatType = GL_BYTE;
		glIsCompressed = false;
	}
	else if (format == Latte::E_GX2SURFFMT::R16_UNORM)
	{
		glInternalFormat = GL_R16;
		// supplied format
		glSuppliedFormat = GL_RED;
		glSuppliedFormatType = GL_UNSIGNED_SHORT;
		glIsCompressed = false;
	}
	else if (format == Latte::E_GX2SURFFMT::R16_G16_B16_A16_UNORM)
	{
		glInternalFormat = GL_RGBA16;
		// supplied format
		glSuppliedFormat = GL_RGBA;
		glSuppliedFormatType = GL_UNSIGNED_SHORT;
		glIsCompressed = false;
	}
	else if (format == Latte::E_GX2SURFFMT::R16_G16_B16_A16_SNORM)
	{
		glInternalFormat = GL_RGBA16_SNORM;
		// supplied format
		glSuppliedFormat = GL_RGBA;
		glSuppliedFormatType = GL_SHORT;
		glIsCompressed = false;
	}
	else if (format == Latte::E_GX2SURFFMT::R16_G16_UNORM)
	{
		glInternalFormat = GL_RG16;
		// supplied format
		glSuppliedFormat = GL_RG;
		glSuppliedFormatType = GL_UNSIGNED_SHORT;
		glIsCompressed = false;
	}
	else if (format == Latte::E_GX2SURFFMT::R5_G6_B5_UNORM)
	{
		glInternalFormat = GL_RGB565;
		// supplied format
		glSuppliedFormat = GL_RGB;
		// GLES so tem GL_UNSIGNED_SHORT_5_6_5 (o _REV da GL_INVALID_ENUM no glTexSubImage2D: confirmado pelo [TexDiag] fmt=0x0008 no
		// PowerVR e Adreno). Nesse caso o decodificador troca R<->B (TextureDecoder_R5_G6_B5_swappedRB, ver OpenGLRenderer.cpp).
		glSuppliedFormatType = OpenGLRenderer::IsGLES() ? GL_UNSIGNED_SHORT_5_6_5 : GL_UNSIGNED_SHORT_5_6_5_REV;
		glIsCompressed = false;
	}
	else if (format == Latte::E_GX2SURFFMT::R5_G5_B5_A1_UNORM)
	{
		glInternalFormat = GL_RGB5_A1;
		// supplied format
		glSuppliedFormat = GL_RGBA;
		glSuppliedFormatType = GL_UNSIGNED_SHORT_5_5_5_1;
		glIsCompressed = false;
	}
	else if (format == Latte::E_GX2SURFFMT::A1_B5_G5_R5_UNORM)
	{
		glInternalFormat = GL_RGB5_A1;
		// supplied format
		glSuppliedFormat = GL_RGBA;
		glSuppliedFormatType = GL_UNSIGNED_SHORT_5_5_5_1;
		glIsCompressed = false;
	}
	else if (format == Latte::E_GX2SURFFMT::R10_G10_B10_A2_UNORM)
	{
		glInternalFormat = GL_RGB10_A2;
		// supplied format
		glSuppliedFormat = GL_RGBA;
		glSuppliedFormatType = GL_UNSIGNED_INT_2_10_10_10_REV;
		glIsCompressed = false;
	}
	else if (format == Latte::E_GX2SURFFMT::R10_G10_B10_A2_SRGB) // used by Super Mario Maker
	{
		glInternalFormat = GL_RGB10_A2; // todo - how to handle SRGB for this format?
		// supplied format
		glSuppliedFormat = GL_RGBA;
		glSuppliedFormatType = GL_UNSIGNED_INT_2_10_10_10_REV;
		glIsCompressed = false;
	}
	else if (format == Latte::E_GX2SURFFMT::A2_B10_G10_R10_UNORM)
	{
		glInternalFormat = GL_RGB10_A2;
		// supplied format
		glSuppliedFormat = GL_RGBA;
		glSuppliedFormatType = GL_UNSIGNED_INT_10_10_10_2;
		glIsCompressed = false;
	}
	else if (format == Latte::E_GX2SURFFMT::R10_G10_B10_A2_SNORM)
	{
		glInternalFormat = GL_RGBA16_SNORM; // OpenGL has no signed version of GL_RGB10_A2
		isUsingAlternativeFormat = true;
		// supplied format
		glSuppliedFormat = GL_RGBA;
		glSuppliedFormatType = GL_SHORT;
		glIsCompressed = false;
	}
	else if (format == Latte::E_GX2SURFFMT::R11_G11_B10_FLOAT)
	{
		glInternalFormat = GL_R11F_G11F_B10F;
		// supplied format
		glSuppliedFormat = GL_RGB;
		glSuppliedFormatType = GL_UNSIGNED_INT_10F_11F_11F_REV;
		glIsCompressed = false;
	}
	else if (format == Latte::E_GX2SURFFMT::R32_G32_B32_A32_UINT)
	{
		glInternalFormat = GL_RGBA32UI;
		// supplied format
		glSuppliedFormat = GL_RGBA_INTEGER;
		glSuppliedFormatType = GL_UNSIGNED_INT;
		glIsCompressed = false;
	}
	else if (format == Latte::E_GX2SURFFMT::R16_G16_B16_A16_UINT)
	{
		glInternalFormat = GL_RGBA16UI;
		// supplied format
		glSuppliedFormat = GL_RGBA_INTEGER;
		glSuppliedFormatType = GL_UNSIGNED_SHORT;
		glIsCompressed = false;
	}
	else if (format == Latte::E_GX2SURFFMT::R8_G8_B8_A8_UINT)
	{
		glInternalFormat = GL_RGBA8UI;
		// supplied format
		glSuppliedFormat = GL_RGBA_INTEGER;
		glSuppliedFormatType = GL_UNSIGNED_BYTE;
		glIsCompressed = false;
	}
	else if (format == Latte::E_GX2SURFFMT::R24_X8_UNORM)
	{
		// OpenGL has no color version of GL_DEPTH24_STENCIL8, therefore we use a 32-bit floating-point format instead
		glInternalFormat = GL_R32F;
		isUsingAlternativeFormat = true;
		// supplied format
		glSuppliedFormat = GL_RED;
		glSuppliedFormatType = GL_FLOAT;
		glIsCompressed = false;
	}
	else if (format == Latte::E_GX2SURFFMT::X24_G8_UINT)
	{
		// OpenGL has no X24_G8 format, we use RGBA8UI instead and manually swizzle the channels
		// this format is used in Resident Evil Revelations when scanning with the Genesis. It's also used in Cars 3: Driven to Win?

		glInternalFormat = GL_RGBA8UI;
		isUsingAlternativeFormat = true;
		// supplied format
		glSuppliedFormat = GL_RGBA;
		glSuppliedFormatType = GL_FLOAT;
		glIsCompressed = false;
	}
	else if (format == Latte::E_GX2SURFFMT::R32_X8_FLOAT)
	{
		// only available as depth format in OpenGL
		// used by Cars 3: Driven to Win
		// find a way to emulate this using a color format
		glInternalFormat = GL_DEPTH32F_STENCIL8;
		isUsingAlternativeFormat = false;
		// supplied format
		glSuppliedFormat = GL_DEPTH_STENCIL;
		glSuppliedFormatType = GL_FLOAT_32_UNSIGNED_INT_24_8_REV;
		glIsCompressed = false;
		cemu_assert_debug(false);
	}
	else
	{
		cemuLog_log(LogType::Force, "OpenGL: Unsupported texture format 0x{:04x}", (uint32)format);
		cemu_assert_unimplemented();
	}
	formatInfoOut->glInternalFormat = glInternalFormat;
	formatInfoOut->glSuppliedFormat = glSuppliedFormat;
	formatInfoOut->glSuppliedFormatType = glSuppliedFormatType;
	formatInfoOut->glIsCompressed = glIsCompressed;
	formatInfoOut->isUsingAlternativeFormat = isUsingAlternativeFormat;
}

void LatteTextureGL::AllocateOnHost()
{
	auto hostTexture = this;
	cemu_assert_debug(hostTexture->isDataDefined == false);
	sint32 effectiveBaseWidth = hostTexture->width;
	sint32 effectiveBaseHeight = hostTexture->height;
	sint32 effectiveBaseDepth = hostTexture->depth;
	if (hostTexture->overwriteInfo.hasResolutionOverwrite)
	{
		effectiveBaseWidth = hostTexture->overwriteInfo.width;
		effectiveBaseHeight = hostTexture->overwriteInfo.height;
		effectiveBaseDepth = hostTexture->overwriteInfo.depth;
	}
	// calculate mip count
	sint32 mipLevels = std::min(hostTexture->mipLevels, hostTexture->maxPossibleMipLevels);
	mipLevels = std::max(mipLevels, 1);
	AllocateStorage(hostTexture->glId_texture, hostTexture->dim, hostTexture->glInternalFormat, effectiveBaseWidth, effectiveBaseHeight, effectiveBaseDepth, mipLevels);
}

// Shared by AllocateOnHost() above (for a texture's own storage) and by
// LatteTextureViewGL::InitAliasView()'s GLES glCopyImageSubData fallback (for the fresh,
// still-empty texture object created to hold a copied-in view when no glTextureView
// variant exists on this device - see GLInclude.h's glTextureViewWrapper).
void LatteTextureGL::AllocateStorage(GLuint texId, Latte::E_DIM dim, GLuint glInternalFormat, sint32 width, sint32 height, sint32 depth, sint32 mipLevels)
{
	// create immutable storage
	if (dim == Latte::E_DIM::DIM_2D || dim == Latte::E_DIM::DIM_2D_MSAA)
	{
		cemu_assert_debug(depth == 1);
		glTextureStorage2DWrapper(GL_TEXTURE_2D, texId, mipLevels, glInternalFormat, width, height);
	}
	else if (dim == Latte::E_DIM::DIM_1D)
	{
		cemu_assert_debug(height == 1);
		cemu_assert_debug(depth == 1);
		if (OpenGLRenderer::IsGLES())
		{
			// GLES has no 1D textures at all (not even via extension) - GenerateEmptyTextureFromGX2Dim
			// already redirected texId's target to GL_TEXTURE_2D for this case, so allocate a
			// height-1 2D texture instead. See the comment there for the (separate, still
			// outstanding) GLSL sampler1D->sampler2D change this also requires on the shader side.
			glTextureStorage2DWrapper(GL_TEXTURE_2D, texId, mipLevels, glInternalFormat, width, 1);
		}
		else
			glTextureStorage1DWrapper(GL_TEXTURE_1D, texId, mipLevels, glInternalFormat, width);
	}
	else if (dim == Latte::E_DIM::DIM_2D_ARRAY || dim == Latte::E_DIM::DIM_2D_ARRAY_MSAA)
	{
		glTextureStorage3DWrapper(GL_TEXTURE_2D_ARRAY, texId, mipLevels, glInternalFormat, width, height, std::max(1, depth));
	}
	else if (dim == Latte::E_DIM::DIM_3D)
	{
		glTextureStorage3DWrapper(GL_TEXTURE_3D, texId, mipLevels, glInternalFormat, width, height, std::max(1, depth));
	}
	else if (dim == Latte::E_DIM::DIM_CUBEMAP)
	{
		glTextureStorage3DWrapper(GL_TEXTURE_CUBE_MAP_ARRAY, texId, mipLevels, glInternalFormat, width, height, depth);
	}
	else
	{
		cemu_assert_unimplemented();
	}
}