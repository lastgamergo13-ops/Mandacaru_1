#pragma once
#include "Cafe/HW/Latte/Core/LatteTextureLoader.h"

// V11.28 - Decoders BC1..BC5 -> formatos de 8 bits (RGBA8 / R8 / RG8), usados no OpenGL/GLES quando o driver
// NAO tem S3TC/RGTC (ex.: Adreno 650). Substituem os decoders "_uncompress" antigos que geravam float32
// (16 bytes/texel, 4x mais RAM e GPU, e com formatos GL que nao batiam com o buffer no BC5).
// BC1/2/3 usam GL_RGBA8 (UNORM) ou GL_SRGB8_ALPHA8 (SRGB, a conversao e feita pelo hardware como no caminho nativo).

namespace LatteTexRGBA8
{
	inline uint8 toU8(float v)
	{
		if (v <= 0.0f) return 0;
		if (v >= 1.0f) return 255;
		return (uint8)(v * 255.0f + 0.5f);
	}
	inline int8 toS8(float v)
	{
		if (v <= -1.0f) return -127;
		if (v >= 1.0f) return 127;
		return (int8)(v >= 0.0f ? (v * 127.0f + 0.5f) : (v * 127.0f - 0.5f));
	}

	template<int NCOMP, int BLOCKBYTES, bool SNORM, typename TDecodeFn>
	inline void decodeLoop(LatteTextureLoaderCtx* ctx, uint8* out, TDecodeFn decodeBlock)
	{
		for (sint32 y = 0; y < ctx->height; y += ctx->stepY)
		{
			for (sint32 x = 0; x < ctx->width; x += ctx->stepX)
			{
				uint8* blockData = LatteTextureLoader_GetInput(ctx, x, y);
				sint32 bw = (std::min)(4, ctx->width - x);
				sint32 bh = (std::min)(4, ctx->height - y);
				float blk[4 * 4 * 4];
				decodeBlock(blockData, blk);
				for (sint32 py = 0; py < bh; py++)
				{
					for (sint32 px = 0; px < bw; px++)
					{
						uint8* dst = out + ((size_t)(x + px) + (size_t)(y + py) * ctx->width) * NCOMP;
						const float* src = blk + (px + py * 4) * NCOMP;
						for (int c = 0; c < NCOMP; c++)
						{
							if (SNORM)
								dst[c] = (uint8)toS8(src[c]);
							else
								dst[c] = toU8(src[c]);
						}
					}
				}
			}
		}
	}

	template<int NCOMP>
	inline void pixelToRGBA(const float* blk, uint8 bx, uint8 by, uint8* outPixel, bool snorm)
	{
		const float* s = blk + (bx + by * 4) * NCOMP;
		float v[4] = {0, 0, 0, 1};
		for (int c = 0; c < NCOMP; c++)
			v[c] = snorm ? (0.5f + s[c] * 0.5f) : s[c];
		for (int c = 0; c < 4; c++)
			outPixel[c] = toU8(v[c]);
	}
}

class TextureDecoder_BC1_RGBA8 : public TextureDecoder, public SingletonClass<TextureDecoder_BC1_RGBA8>
{
public:
	sint32 getBytesPerTexel(LatteTextureLoaderCtx*) override { return 4; }
	void decode(LatteTextureLoaderCtx* ctx, uint8* out) override
	{
		LatteTexRGBA8::decodeLoop<4, 8, false>(ctx, out, [](uint8* b, float* o) { decodeBC1Block(b, o); });
	}
	void decodePixelToRGBA(uint8* blockData, uint8* outputPixel, uint8 bx, uint8 by) override
	{
		BC1_GetPixel(blockData, bx, by, outputPixel);
	}
};

class TextureDecoder_BC2_RGBA8 : public TextureDecoder, public SingletonClass<TextureDecoder_BC2_RGBA8>
{
public:
	sint32 getBytesPerTexel(LatteTextureLoaderCtx*) override { return 4; }
	void decode(LatteTextureLoaderCtx* ctx, uint8* out) override
	{
		LatteTexRGBA8::decodeLoop<4, 16, false>(ctx, out, [](uint8* b, float* o) { decodeBC2Block_UNORM(b, o); });
	}
	void decodePixelToRGBA(uint8* blockData, uint8* outputPixel, uint8 bx, uint8 by) override
	{
		float blk[64];
		decodeBC2Block_UNORM(blockData, blk);
		LatteTexRGBA8::pixelToRGBA<4>(blk, bx, by, outputPixel, false);
	}
};

class TextureDecoder_BC3_RGBA8 : public TextureDecoder, public SingletonClass<TextureDecoder_BC3_RGBA8>
{
public:
	sint32 getBytesPerTexel(LatteTextureLoaderCtx*) override { return 4; }
	void decode(LatteTextureLoaderCtx* ctx, uint8* out) override
	{
		LatteTexRGBA8::decodeLoop<4, 16, false>(ctx, out, [](uint8* b, float* o) { decodeBC3Block_UNORM(b, o); });
	}
	void decodePixelToRGBA(uint8* blockData, uint8* outputPixel, uint8 bx, uint8 by) override
	{
		float blk[64];
		decodeBC3Block_UNORM(blockData, blk);
		LatteTexRGBA8::pixelToRGBA<4>(blk, bx, by, outputPixel, false);
	}
};

class TextureDecoder_BC4_R8 : public TextureDecoder, public SingletonClass<TextureDecoder_BC4_R8>
{
public:
	sint32 getBytesPerTexel(LatteTextureLoaderCtx*) override { return 1; }
	void decode(LatteTextureLoaderCtx* ctx, uint8* out) override
	{
		LatteTexRGBA8::decodeLoop<1, 8, false>(ctx, out, [](uint8* b, float* o) { decodeBC4Block_UNORM(b, o); });
	}
	void decodePixelToRGBA(uint8* blockData, uint8* outputPixel, uint8 bx, uint8 by) override
	{
		float blk[16];
		decodeBC4Block_UNORM(blockData, blk);
		LatteTexRGBA8::pixelToRGBA<1>(blk, bx, by, outputPixel, false);
	}
};

class TextureDecoder_BC4_R8_SNORM : public TextureDecoder, public SingletonClass<TextureDecoder_BC4_R8_SNORM>
{
public:
	sint32 getBytesPerTexel(LatteTextureLoaderCtx*) override { return 1; }
	void decode(LatteTextureLoaderCtx* ctx, uint8* out) override
	{
		LatteTexRGBA8::decodeLoop<1, 8, true>(ctx, out, [](uint8* b, float* o) { decodeBC4Block_SNORM(b, o); });
	}
	void decodePixelToRGBA(uint8* blockData, uint8* outputPixel, uint8 bx, uint8 by) override
	{
		float blk[16];
		decodeBC4Block_SNORM(blockData, blk);
		LatteTexRGBA8::pixelToRGBA<1>(blk, bx, by, outputPixel, true);
	}
};

class TextureDecoder_BC5_RG8 : public TextureDecoder, public SingletonClass<TextureDecoder_BC5_RG8>
{
public:
	sint32 getBytesPerTexel(LatteTextureLoaderCtx*) override { return 2; }
	void decode(LatteTextureLoaderCtx* ctx, uint8* out) override
	{
		LatteTexRGBA8::decodeLoop<2, 16, false>(ctx, out, [](uint8* b, float* o) { decodeBC5Block_UNORM(b, o); });
	}
	void decodePixelToRGBA(uint8* blockData, uint8* outputPixel, uint8 bx, uint8 by) override
	{
		float blk[32];
		decodeBC5Block_UNORM(blockData, blk);
		LatteTexRGBA8::pixelToRGBA<2>(blk, bx, by, outputPixel, false);
	}
};

class TextureDecoder_BC5_RG8_SNORM : public TextureDecoder, public SingletonClass<TextureDecoder_BC5_RG8_SNORM>
{
public:
	sint32 getBytesPerTexel(LatteTextureLoaderCtx*) override { return 2; }
	void decode(LatteTextureLoaderCtx* ctx, uint8* out) override
	{
		LatteTexRGBA8::decodeLoop<2, 16, true>(ctx, out, [](uint8* b, float* o) { decodeBC5Block_SNORM(b, o); });
	}
	void decodePixelToRGBA(uint8* blockData, uint8* outputPixel, uint8 bx, uint8 by) override
	{
		float blk[32];
		decodeBC5Block_SNORM(blockData, blk);
		LatteTexRGBA8::pixelToRGBA<2>(blk, bx, by, outputPixel, true);
	}
};
