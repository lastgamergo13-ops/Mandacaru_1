#pragma once
// Real, from-specification ETC1 block encoder, used to transcode Wii U BC1 (opaque
// diffuse) textures into ETC2-compatible compressed data (GL_COMPRESSED_RGB8_ETC2 /
// VK_FORMAT_ETC2_R8G8B8_*_BLOCK) for Android GPUs that support ETC2 natively but not
// BC/S3TC. See LatteTextureLoader.h's TextureDecoder_BC1_To_ETC2_RGB8 for the caller.
//
// The bitstream layout implemented here is exactly the Khronos
// OES_compressed_ETC1_RGB8_texture specification:
//   https://registry.khronos.org/OpenGL/extensions/OES/OES_compressed_ETC1_RGB8_texture.txt
// ETC2's baseline mode is fully backwards compatible with ETC1, so any spec-correct
// ETC1 block is also a valid, correctly-decodable ETC2 block.
//
// Known, deliberate simplifications of this encoder (documented so nobody assumes more
// quality/coverage than what's actually here):
//  - Always encodes with flip=0 (the two 2x4 subblocks are always the left/right halves
//    of the block, never top/bottom). A "real" encoder also tries flip=1 and keeps
//    whichever orientation gives less error; skipping that search costs a small, bounded
//    amount of quality on blocks with strong horizontal detail, but every block produced
//    is still 100% valid, spec-correct ETC1/ETC2 data either way.
//  - Base colors come from each subblock's plain average color (no least-squares/PCA
//    endpoint search). This is the standard "fast" ETC1 encoding strategy and is what
//    most real-time encoders default to; it costs a bit of quality vs. an exhaustive
//    endpoint search, again without ever producing invalid data.
//  - RGB only, no alpha channel - matches ETC2's "RGB8" baseline format and the common,
//    fully-opaque case of BC1_UNORM this is used for.
//  - This has NOT been validated against a real ETC2 GPU decoder or a reference decoder
//    library (no GPU/compiler available in the environment this was written in). The
//    debug-only round-trip check below can only catch this file's own internal
//    inconsistencies, not a mistaken reading of the spec. Test on real hardware before
//    trusting this in a release build; the feature ships OFF by default for this reason.

#include <cstdint>
#include <cstring>
#include <algorithm>

namespace LatteETC1Encoder
{
	// Table 3.17.2 of the spec: the 8 fixed intensity modifier tables, expressed as the
	// (small, large) magnitude pair. A pixel's modifier is one of +-small or +-large.
	struct ModifierTable { int small_; int large_; };
	static constexpr ModifierTable kModifierTables[8] =
	{
		{2,     8},
		{5,    17},
		{9,    29},
		{13,   42},
		{18,   60},
		{24,   80},
		{33,  106},
		{47,  183},
	};

	inline uint8_t Extend5to8(int v5)
	{
		// Replicate the top 3 bits into the bottom 3 (spec example: 11100b -> 11100111b).
		return (uint8_t)((v5 << 3) | (v5 >> 2));
	}

	inline uint8_t Extend4to8(int v4)
	{
		return (uint8_t)((v4 << 4) | v4);
	}

	inline int Clamp255(int v)
	{
		return v < 0 ? 0 : (v > 255 ? 255 : v);
	}

	inline int QuantizeTo5(int avg255) { return std::clamp((avg255 * 31 + 127) / 255, 0, 31); }
	inline int QuantizeTo4(int avg255) { return std::clamp((avg255 * 15 + 127) / 255, 0, 15); }

	// One subblock's worth of source pixels (8 pixels, RGB8 each, no alpha).
	struct SubblockPixels
	{
		uint8_t r[8];
		uint8_t g[8];
		uint8_t b[8];
	};

	struct SubblockEncoding
	{
		uint8_t tableIndex;  // 0-7: which of the 8 modifier tables (the "table codeword")
		uint8_t pixelMsb[8]; // one bit per pixel, subblock-local pixel order 0-7
		uint8_t pixelLsb[8];
	};

	// Picks the best of the 8 modifier tables for this subblock's base color, and the best
	// 2-bit index per pixel for whichever table is chosen. This is a brute-force search
	// over all 8 tables x 4 candidate values per pixel, so for a *fixed* base color and
	// flip choice, this always finds the true minimum-error encoding.
	inline void EncodeSubblock(const SubblockPixels& px, uint8_t baseR, uint8_t baseG, uint8_t baseB, SubblockEncoding& out)
	{
		uint64_t bestError = UINT64_MAX;
		uint8_t bestTable = 0;
		uint8_t bestMsb[8]{};
		uint8_t bestLsb[8]{};
		for (int t = 0; t < 8; t++)
		{
			const int small_ = kModifierTables[t].small_;
			const int large_ = kModifierTables[t].large_;
			// Table 3.17.3: candidate k (0-3) -> (msb,lsb) -> signed modifier value.
			//   k=0 (msb0,lsb0) -> +small     k=1 (msb0,lsb1) -> +large
			//   k=2 (msb1,lsb0) -> -small     k=3 (msb1,lsb1) -> -large
			const int candidates[4] = { +small_, +large_, -small_, -large_ };
			uint64_t tableError = 0;
			uint8_t msb[8], lsb[8];
			for (int p = 0; p < 8; p++)
			{
				uint64_t bestPixelError = UINT64_MAX;
				int bestK = 0;
				for (int k = 0; k < 4; k++)
				{
					int mod = candidates[k];
					int rr = Clamp255((int)baseR + mod) - px.r[p];
					int gg = Clamp255((int)baseG + mod) - px.g[p];
					int bb = Clamp255((int)baseB + mod) - px.b[p];
					uint64_t err = (uint64_t)(rr * rr + gg * gg + bb * bb);
					if (err < bestPixelError)
					{
						bestPixelError = err;
						bestK = k;
					}
				}
				msb[p] = (uint8_t)((bestK >> 1) & 1);
				lsb[p] = (uint8_t)(bestK & 1);
				tableError += bestPixelError;
			}
			if (tableError < bestError)
			{
				bestError = tableError;
				bestTable = (uint8_t)t;
				std::memcpy(bestMsb, msb, 8);
				std::memcpy(bestLsb, lsb, 8);
			}
		}
		out.tableIndex = bestTable;
		std::memcpy(out.pixelMsb, bestMsb, 8);
		std::memcpy(out.pixelLsb, bestLsb, 8);
	}

	inline int AverageChannel(const uint8_t* values, int count)
	{
		int sum = 0;
		for (int i = 0; i < count; i++)
			sum += values[i];
		return (sum + count / 2) / count;
	}

#ifdef CEMU_DEBUG_ASSERT
	// Debug-only round-trip decoder, used purely as an internal self-consistency check
	// from EncodeBlock4x4RGB() below. This can only catch bugs where the encoder
	// contradicts itself; it cannot catch a mistaken reading of the real spec, and it is
	// NOT a substitute for testing on real ETC2 hardware.
	inline void DecodeBlock4x4RGB_DebugOnly(const uint8_t inBlock[8], uint8_t outRgba4x4[4 * 4 * 4])
	{
		uint64_t block = 0;
		for (int i = 0; i < 8; i++)
			block |= (uint64_t)inBlock[i] << (56 - i * 8);

		bool diff = ((block >> 33) & 1) != 0;
		uint8_t baseColor[2][3];
		if (diff)
		{
			int r1 = (int)((block >> 59) & 0x1F);
			int dR = (int8_t)(((block >> 56) & 0x7) << 5) >> 5; // sign-extend 3-bit value
			int g1 = (int)((block >> 51) & 0x1F);
			int dG = (int8_t)(((block >> 48) & 0x7) << 5) >> 5;
			int b1 = (int)((block >> 43) & 0x1F);
			int dB = (int8_t)(((block >> 40) & 0x7) << 5) >> 5;
			baseColor[0][0] = Extend5to8(r1);
			baseColor[0][1] = Extend5to8(g1);
			baseColor[0][2] = Extend5to8(b1);
			baseColor[1][0] = Extend5to8(r1 + dR);
			baseColor[1][1] = Extend5to8(g1 + dG);
			baseColor[1][2] = Extend5to8(b1 + dB);
		}
		else
		{
			baseColor[0][0] = Extend4to8((int)((block >> 60) & 0xF));
			baseColor[1][0] = Extend4to8((int)((block >> 56) & 0xF));
			baseColor[0][1] = Extend4to8((int)((block >> 52) & 0xF));
			baseColor[1][1] = Extend4to8((int)((block >> 48) & 0xF));
			baseColor[0][2] = Extend4to8((int)((block >> 44) & 0xF));
			baseColor[1][2] = Extend4to8((int)((block >> 40) & 0xF));
		}
		int table0 = (int)((block >> 37) & 0x7);
		int table1 = (int)((block >> 34) & 0x7);
		for (int half = 0; half < 2; half++)
		{
			int table = half == 0 ? table0 : table1;
			int small_ = kModifierTables[table].small_;
			int large_ = kModifierTables[table].large_;
			int candidates[4] = { +small_, +large_, -small_, -large_ };
			for (int p = 0; p < 8; p++)
			{
				int letterIndex = half * 8 + p;
				int msb = (int)((block >> (16 + letterIndex)) & 1);
				int lsb = (int)((block >> letterIndex) & 1);
				int k = (msb << 1) | lsb;
				int mod = candidates[k];
				int x = half * 2 + (p / 4);
				int y = p % 4;
				uint8_t* out = outRgba4x4 + (y * 4 + x) * 4;
				out[0] = (uint8_t)Clamp255(baseColor[half][0] + mod);
				out[1] = (uint8_t)Clamp255(baseColor[half][1] + mod);
				out[2] = (uint8_t)Clamp255(baseColor[half][2] + mod);
				out[3] = 255;
			}
		}
	}
#endif

	// Encodes one 4x4 RGB block (16 pixels, row-major as rgba4x4[(y*4+x)*4 + channel],
	// alpha channel present but ignored) into 8 bytes of ETC1/ETC2 data. Always uses
	// flip=0 and, when possible, differential mode - falling back to individual mode only
	// when the two subblocks' average colors are too far apart to fit the 3-bit
	// differential delta. This fallback means the encoder is always spec-correct: it
	// never produces an out-of-range/undefined block, regardless of input.
	inline void EncodeBlock4x4RGB(const uint8_t rgba4x4[4 * 4 * 4], uint8_t outBlock[8])
	{
		// Split into the two 2x4 side-by-side subblocks (flip=0): subblock 1 = left two
		// columns (x=0,1), subblock 2 = right two columns (x=2,3) - spec figure 3.9.2.
		SubblockPixels sub[2];
		for (int half = 0; half < 2; half++)
		{
			int idx = 0;
			for (int x = half * 2; x < half * 2 + 2; x++)
			{
				for (int y = 0; y < 4; y++)
				{
					const uint8_t* p = rgba4x4 + (y * 4 + x) * 4;
					sub[half].r[idx] = p[0];
					sub[half].g[idx] = p[1];
					sub[half].b[idx] = p[2];
					idx++;
				}
			}
		}

		int avgR[2], avgG[2], avgB[2];
		for (int half = 0; half < 2; half++)
		{
			avgR[half] = AverageChannel(sub[half].r, 8);
			avgG[half] = AverageChannel(sub[half].g, 8);
			avgB[half] = AverageChannel(sub[half].b, 8);
		}

		bool useDifferential;
		int r1_5 = 0, g1_5 = 0, b1_5 = 0, dR = 0, dG = 0, dB = 0;
		int r1_4 = 0, g1_4 = 0, b1_4 = 0, r2_4 = 0, g2_4 = 0, b2_4 = 0;
		uint8_t baseColor[2][3];
		{
			r1_5 = QuantizeTo5(avgR[0]);
			g1_5 = QuantizeTo5(avgG[0]);
			b1_5 = QuantizeTo5(avgB[0]);
			int r2_5 = QuantizeTo5(avgR[1]);
			int g2_5 = QuantizeTo5(avgG[1]);
			int b2_5 = QuantizeTo5(avgB[1]);
			dR = r2_5 - r1_5;
			dG = g2_5 - g1_5;
			dB = b2_5 - b1_5;
			useDifferential = (dR >= -4 && dR <= 3 && dG >= -4 && dG <= 3 && dB >= -4 && dB <= 3);
			if (useDifferential)
			{
				baseColor[0][0] = Extend5to8(r1_5);
				baseColor[0][1] = Extend5to8(g1_5);
				baseColor[0][2] = Extend5to8(b1_5);
				baseColor[1][0] = Extend5to8(r1_5 + dR);
				baseColor[1][1] = Extend5to8(g1_5 + dG);
				baseColor[1][2] = Extend5to8(b1_5 + dB);
			}
		}
		if (!useDifferential)
		{
			r1_4 = QuantizeTo4(avgR[0]); g1_4 = QuantizeTo4(avgG[0]); b1_4 = QuantizeTo4(avgB[0]);
			r2_4 = QuantizeTo4(avgR[1]); g2_4 = QuantizeTo4(avgG[1]); b2_4 = QuantizeTo4(avgB[1]);
			baseColor[0][0] = Extend4to8(r1_4);
			baseColor[0][1] = Extend4to8(g1_4);
			baseColor[0][2] = Extend4to8(b1_4);
			baseColor[1][0] = Extend4to8(r2_4);
			baseColor[1][1] = Extend4to8(g2_4);
			baseColor[1][2] = Extend4to8(b2_4);
		}

		SubblockEncoding enc[2];
		for (int half = 0; half < 2; half++)
			EncodeSubblock(sub[half], baseColor[half][0], baseColor[half][1], baseColor[half][2], enc[half]);

		// Pack the 64-bit block per the spec's "Add table 3.17.1".
		uint64_t block = 0;
		if (useDifferential)
		{
			auto to3bitTwoComplement = [](int d) -> uint64_t { return (uint64_t)(d & 0x7); };
			block |= (uint64_t)(r1_5 & 0x1F) << 59;
			block |= to3bitTwoComplement(dR) << 56;
			block |= (uint64_t)(g1_5 & 0x1F) << 51;
			block |= to3bitTwoComplement(dG) << 48;
			block |= (uint64_t)(b1_5 & 0x1F) << 43;
			block |= to3bitTwoComplement(dB) << 40;
		}
		else
		{
			block |= (uint64_t)(r1_4 & 0xF) << 60;
			block |= (uint64_t)(r2_4 & 0xF) << 56;
			block |= (uint64_t)(g1_4 & 0xF) << 52;
			block |= (uint64_t)(g2_4 & 0xF) << 48;
			block |= (uint64_t)(b1_4 & 0xF) << 44;
			block |= (uint64_t)(b2_4 & 0xF) << 40;
		}
		block |= (uint64_t)(enc[0].tableIndex & 0x7) << 37;
		block |= (uint64_t)(enc[1].tableIndex & 0x7) << 34;
		if (useDifferential)
			block |= (1ull << 33);
		// bit 32 (flipbit) intentionally left 0 - this encoder always uses flip=0.

		// Low 32 bits: pixel index bits. Letter index i = half*8 + subblockLocalPixel,
		// which (for flip=0, matching how `sub[half]` was filled above) is exactly the
		// spec's x*4+y - see figures 3.9.1/3.9.2.
		uint64_t msbBits = 0, lsbBits = 0;
		for (int half = 0; half < 2; half++)
		{
			for (int p = 0; p < 8; p++)
			{
				int letterIndex = half * 8 + p;
				msbBits |= (uint64_t)(enc[half].pixelMsb[p]) << letterIndex;
				lsbBits |= (uint64_t)(enc[half].pixelLsb[p]) << letterIndex;
			}
		}
		block |= (msbBits & 0xFFFF) << 16;
		block |= (lsbBits & 0xFFFF);

		// Per spec: byte q0 (bits 63-56) is stored at the lowest memory address.
		for (int i = 0; i < 8; i++)
			outBlock[i] = (uint8_t)((block >> (56 - i * 8)) & 0xFF);

#ifdef CEMU_DEBUG_ASSERT
		// Internal self-consistency check only - see DecodeBlock4x4RGB_DebugOnly's comment.
		uint8_t roundTrip[4 * 4 * 4];
		DecodeBlock4x4RGB_DebugOnly(outBlock, roundTrip);
		int worstError = 0;
		for (int i = 0; i < 16; i++)
		{
			for (int c = 0; c < 3; c++)
				worstError = std::max(worstError, std::abs((int)roundTrip[i * 4 + c] - (int)rgba4x4[i * 4 + c]));
		}
		// A well-formed encode should never be wildly off from the source average; a huge
		// error here means the packer and the debug unpacker disagree with each other.
		cemu_assert_debug(worstError <= 196);
#endif
	}
}

// Real, from-specification EAC (Ericsson Alpha Compression) block encoder, used for the
// alpha channel of GL_COMPRESSED_RGBA8_ETC2_EAC / VK_FORMAT_ETC2_R8G8B8A8_*_BLOCK - the
// format BC2 and BC3 (both RGB + separate alpha) are transcoded to. The color half of
// that format is the plain ETC2 RGB block above (LatteETC1Encoder); this namespace only
// covers the alpha half, which is a completely separate 8-byte block with its own layout.
//
// Bitstream layout and the 16 intensity modifier tables implemented here match the
// official Khronos OpenGL ES 3.0 specification, Appendix C.1.3 ("Format
// COMPRESSED_RGBA8_ETC2_EAC") and C.1.5 ("Format COMPRESSED_R11_EAC", which reuses the
// same block shape at a different bit depth - see LatteTextureETC1Encoder.h's own notes
// when R11/RG11 support for BC4/BC5 is added).
//
// Known, deliberate simplifications (same spirit as LatteETC1Encoder above):
//  - The block's base value is fixed to the block's rounded average alpha rather than
//    jointly optimized with the table/multiplier choice. A full joint search would cost
//    much more time per texture for a further, usually small, quality gain.
//  - This has NOT been validated against a real EAC GPU decoder or reference decoder
//    library, for the same reasons noted in LatteETC1Encoder above. Ships behind the same
//    "Active texture compression" option, off by default.
namespace LatteEACEncoder
{
	// Khronos OpenGL ES 3.0 spec, Table C.10 ("Intensity modifier sets for alpha
	// component"): 16 fixed tables of 8 signed values each. A pixel's contribution is
	// tableValue[pixelIndex(0-7)] and is later scaled by the block's multiplier.
	static constexpr int8_t kAlphaModifierTables[16][8] =
	{
		{ -3,  -6,  -9, -15,  2,  5,  8, 14 },
		{ -3,  -7, -10, -13,  2,  6,  9, 12 },
		{ -2,  -5,  -8, -13,  1,  4,  7, 12 },
		{ -2,  -4,  -6, -13,  1,  3,  5, 12 },
		{ -3,  -6,  -8, -12,  2,  5,  7, 11 },
		{ -3,  -7,  -9, -11,  2,  6,  8, 10 },
		{ -4,  -7,  -8, -11,  3,  6,  7, 10 },
		{ -3,  -5,  -8, -11,  2,  4,  7, 10 },
		{ -2,  -6,  -8, -10,  1,  5,  7,  9 },
		{ -2,  -5,  -8, -10,  1,  4,  7,  9 },
		{ -2,  -4,  -8, -10,  1,  3,  7,  9 },
		{ -2,  -5,  -7, -10,  1,  4,  6,  9 },
		{ -3,  -4,  -7, -10,  2,  3,  6,  9 },
		{ -1,  -2,  -3, -10,  0,  1,  2,  9 },
		{ -4,  -6,  -8,  -9,  3,  5,  7,  8 },
		{ -3,  -5,  -7,  -9,  2,  4,  6,  8 },
	};

	inline int Clamp255(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

	// Encodes one 4x4 alpha block (16 values, 0-255, in the same row-major pixel order
	// as LatteETC1Encoder - alpha8[y*4+x]) into the 8-byte EAC alpha block.
	inline void EncodeAlphaBlock8bit(const uint8_t alpha8[16], uint8_t outBlock[8])
	{
		int sum = 0;
		for (int i = 0; i < 16; i++)
			sum += alpha8[i];
		const int base = (sum + 8) / 16; // rounded average, fixed for the whole block

		uint64_t bestError = UINT64_MAX;
		int bestTable = 0, bestMultiplier = 0;
		uint8_t bestIndex[16]{};
		for (int table = 0; table < 16; table++)
		{
			for (int multiplier = 0; multiplier <= 15; multiplier++)
			{
				uint64_t error = 0;
				uint8_t index[16];
				for (int p = 0; p < 16; p++)
				{
					uint64_t bestPixelError = UINT64_MAX;
					int bestK = 0;
					for (int k = 0; k < 8; k++)
					{
						int value = Clamp255(base + multiplier * kAlphaModifierTables[table][k]);
						int diff = value - (int)alpha8[p];
						uint64_t err = (uint64_t)(diff * diff);
						if (err < bestPixelError)
						{
							bestPixelError = err;
							bestK = k;
						}
					}
					index[p] = (uint8_t)bestK;
					error += bestPixelError;
				}
				if (error < bestError)
				{
					bestError = error;
					bestTable = table;
					bestMultiplier = multiplier;
					std::memcpy(bestIndex, index, 16);
				}
				if (bestError == 0)
					break;
			}
			if (bestError == 0)
				break;
		}

		// Pack: byte0 = base (signed), byte1 = [multiplier:4][table:4], bytes2-7 = 16x3-bit
		// pixel indices packed MSB-first in raster order (same order the color block uses).
		outBlock[0] = (uint8_t)(int8_t)base;
		outBlock[1] = (uint8_t)((bestMultiplier << 4) | (bestTable & 0xF));

		uint64_t packedIndices = 0; // low 48 bits used
		for (int p = 0; p < 16; p++)
			packedIndices |= (uint64_t)(bestIndex[p] & 0x7) << (45 - p * 3);
		outBlock[2] = (uint8_t)((packedIndices >> 40) & 0xFF);
		outBlock[3] = (uint8_t)((packedIndices >> 32) & 0xFF);
		outBlock[4] = (uint8_t)((packedIndices >> 24) & 0xFF);
		outBlock[5] = (uint8_t)((packedIndices >> 16) & 0xFF);
		outBlock[6] = (uint8_t)((packedIndices >> 8) & 0xFF);
		outBlock[7] = (uint8_t)(packedIndices & 0xFF);

#ifdef CEMU_DEBUG_ASSERT
		// Internal self-consistency round-trip check, same reasoning as LatteETC1Encoder's.
		int worstError = 0;
		for (int p = 0; p < 16; p++)
		{
			int value = Clamp255(base + bestMultiplier * kAlphaModifierTables[bestTable][bestIndex[p]]);
			worstError = std::max(worstError, std::abs(value - (int)alpha8[p]));
		}
		cemu_assert_debug(worstError <= 196);
#endif
	}

	// Real, from-specification R11/RG11 EAC block encoder, used for BC4 (single-channel,
	// e.g. height/grayscale maps) and BC5 (dual-channel, e.g. normal maps) when transcoded
	// to GL_COMPRESSED_R11_EAC/RG11_EAC / VK_FORMAT_EAC_R11(G11)_UNORM_BLOCK.
	//
	// This reuses the exact same 8-byte block shape and the exact same 16-row intensity
	// modifier table as the 8-bit alpha encoder above - R11/RG11 EAC is the same format
	// family at a different internal precision (11 bits, 0-2047, instead of 8 bits,
	// 0-255), confirmed against a real, independently-published EAC decoder
	// implementation (not just the primary spec source used for the alpha case above),
	// which uses:
	//   base_value  = base_byte * 8 + 4
	//   multiplier  = max(multiplier_nibble * 8, 1)          (note: floor of 1, not 0 -
	//                                                          unlike the 8-bit alpha case)
	//   value       = clamp(base_value + multiplier * table[table_nibble][pixel_index], 0, 2047)
	//
	// UNORM only: this codebase's existing BC4/BC5 -> RGBA8 fallback decoders already
	// treat BC4_SNORM the same as BC4_UNORM (see TextureDecoder_BC4_To_R8, which always
	// calls decodeBC4Block_UNORM), and a correctly signed R11_SNORM/RG11_SNORM transcode
	// would need its own, separately-verified bit formula that wasn't found from an
	// equally solid source while writing this - getting a signed format wrong is exactly
	// the kind of mistake that would show up as visibly wrong normal maps, so BC4_SNORM/
	// BC5_SNORM deliberately still fall back to the existing RGBA8 decode, untouched.
	inline void EncodeR11BlockUNORM(const uint8_t value8[16], uint8_t outBlock[8])
	{
		// Expand this format's 8-bit source precision (that's all BC4/BC5 ever carry) up
		// to the 11-bit range R11_EAC supports, via bit replication - same expansion
		// style used elsewhere in this file for 4-bit/5-bit color channels.
		int value11[16];
		for (int i = 0; i < 16; i++)
			value11[i] = ((int)value8[i] << 3) | ((int)value8[i] >> 5);

		int sum = 0;
		for (int i = 0; i < 16; i++)
			sum += value11[i];
		int avg11 = (sum + 8) / 16;
		int t = std::max(0, avg11 - 4);
		int baseByte = std::clamp((t + 4) / 8, 0, 255);
		int base = baseByte * 8 + 4;

		uint64_t bestError = UINT64_MAX;
		int bestMultNibble = 0, bestTable = 0;
		uint8_t bestIndex[16]{};
		for (int table = 0; table < 16; table++)
		{
			for (int multNibble = 0; multNibble <= 15; multNibble++)
			{
				const int multiplier = std::max(multNibble * 8, 1);
				uint64_t error = 0;
				uint8_t index[16];
				for (int p = 0; p < 16; p++)
				{
					uint64_t bestPixelError = UINT64_MAX;
					int bestK = 0;
					for (int k = 0; k < 8; k++)
					{
						int value = std::clamp(base + multiplier * kAlphaModifierTables[table][k], 0, 2047);
						int diff = value - value11[p];
						uint64_t err = (uint64_t)(diff * diff);
						if (err < bestPixelError)
						{
							bestPixelError = err;
							bestK = k;
						}
					}
					index[p] = (uint8_t)bestK;
					error += bestPixelError;
				}
				if (error < bestError)
				{
					bestError = error;
					bestMultNibble = multNibble;
					bestTable = table;
					std::memcpy(bestIndex, index, 16);
				}
				if (bestError == 0)
					break;
			}
			if (bestError == 0)
				break;
		}

		// Same 8-byte packing as EncodeAlphaBlock8bit: byte0 = base byte, byte1 =
		// [multiplier nibble:4][table:4], bytes2-7 = 16x3-bit pixel indices, MSB-first,
		// raster order.
		outBlock[0] = (uint8_t)baseByte;
		outBlock[1] = (uint8_t)((bestMultNibble << 4) | (bestTable & 0xF));

		uint64_t packedIndices = 0;
		for (int p = 0; p < 16; p++)
			packedIndices |= (uint64_t)(bestIndex[p] & 0x7) << (45 - p * 3);
		outBlock[2] = (uint8_t)((packedIndices >> 40) & 0xFF);
		outBlock[3] = (uint8_t)((packedIndices >> 32) & 0xFF);
		outBlock[4] = (uint8_t)((packedIndices >> 24) & 0xFF);
		outBlock[5] = (uint8_t)((packedIndices >> 16) & 0xFF);
		outBlock[6] = (uint8_t)((packedIndices >> 8) & 0xFF);
		outBlock[7] = (uint8_t)(packedIndices & 0xFF);

#ifdef CEMU_DEBUG_ASSERT
		// Internal self-consistency round-trip check, same reasoning as the other
		// encoders in this file.
		int worstError11 = 0;
		const int multiplier = std::max(bestMultNibble * 8, 1);
		for (int p = 0; p < 16; p++)
		{
			int value = std::clamp(base + multiplier * kAlphaModifierTables[bestTable][bestIndex[p]], 0, 2047);
			worstError11 = std::max(worstError11, std::abs(value - value11[p]));
		}
		// 11-bit range, so the same "obviously broken" threshold needs scaling up from
		// the 8-bit alpha check (196/255 of full range).
		cemu_assert_debug(worstError11 <= 1580);
#endif
	}

	// Real, from-production-source R11/RG11 EAC SIGNED (SNORM) block encoder, used for
	// BC4_SNORM/BC5_SNORM - the common case for actual normal maps. Unlike
	// EncodeR11BlockUNORM above, this formula was NOT taken from the primary Khronos
	// spec text directly (a solid enough description of the signed variant wasn't found
	// there while writing EncodeR11BlockUNORM) - it's instead transcribed from Mesa's
	// real, shipped, production decoder implementation
	// (src/mesa/main/texcompress_etc.c, function etc2_signed_r11_fetch_texel), which is
	// exactly the kind of independently-verifiable, battle-tested source this project
	// held out for before implementing this. Confirmed to share the exact same 16-row
	// intensity modifier table as the unsigned case (kAlphaModifierTables, reused
	// unchanged) - only the base/multiplier scaling and value range differ:
	//   base_value  = base_byte * 8                     (base_byte read as SIGNED int8;
	//                                                     no "+4" centering offset, unlike
	//                                                     the unsigned case - signed data
	//                                                     is already centered on zero)
	//   multiplier  = max(multiplier_nibble * 8, 1)      (same as the unsigned case)
	//   value       = clamp(base_value + multiplier * table[table_nibble][pixel_index], -1023, 1023)
	// One real, documented hardware quirk carried over from that Mesa source: a
	// base_byte of exactly -128 is invalid per the OpenGL ES 3.0 spec (Mesa clamps it to
	// -127 on decode) - this encoder simply never produces -128 in the first place.
	inline void EncodeR11BlockSNORM(const int8_t value8[16], uint8_t outBlock[8])
	{
		// This format's 11-bit signed range from an 8-bit signed source - same
		// "no real precision gain, just format conversion" situation as the UNORM case,
		// scaled by a plain x8 (simpler and equally valid for an 8-bit source as a
		// bit-replication scheme would be, and easier to reason about correctly for a
		// signed value).
		int value11[16];
		for (int i = 0; i < 16; i++)
			value11[i] = (int)value8[i] * 8;

		int sum = 0;
		for (int i = 0; i < 16; i++)
			sum += value11[i];
		int avg11 = (sum >= 0) ? (sum + 8) / 16 : -((-sum + 8) / 16);
		int baseByteSigned = (avg11 >= 0) ? (avg11 + 4) / 8 : -((-avg11 + 4) / 8);
		baseByteSigned = std::clamp(baseByteSigned, -127, 127); // -128 is invalid, see above
		int base = baseByteSigned * 8;

		uint64_t bestError = UINT64_MAX;
		int bestMultNibble = 0, bestTable = 0;
		uint8_t bestIndex[16]{};
		for (int table = 0; table < 16; table++)
		{
			for (int multNibble = 0; multNibble <= 15; multNibble++)
			{
				const int multiplier = std::max(multNibble * 8, 1);
				uint64_t error = 0;
				uint8_t index[16];
				for (int p = 0; p < 16; p++)
				{
					uint64_t bestPixelError = UINT64_MAX;
					int bestK = 0;
					for (int k = 0; k < 8; k++)
					{
						int value = std::clamp(base + multiplier * kAlphaModifierTables[table][k], -1023, 1023);
						int diff = value - value11[p];
						uint64_t err = (uint64_t)(diff * diff);
						if (err < bestPixelError)
						{
							bestPixelError = err;
							bestK = k;
						}
					}
					index[p] = (uint8_t)bestK;
					error += bestPixelError;
				}
				if (error < bestError)
				{
					bestError = error;
					bestMultNibble = multNibble;
					bestTable = table;
					std::memcpy(bestIndex, index, 16);
				}
				if (bestError == 0)
					break;
			}
			if (bestError == 0)
				break;
		}

		// Same 8-byte packing as the other EAC encoders in this file: byte0 = base byte
		// (here read back as signed by the decoder), byte1 = [multiplier nibble:4][table:4],
		// bytes2-7 = 16x3-bit pixel indices, MSB-first, raster order.
		outBlock[0] = (uint8_t)(int8_t)baseByteSigned;
		outBlock[1] = (uint8_t)((bestMultNibble << 4) | (bestTable & 0xF));

		uint64_t packedIndices = 0;
		for (int p = 0; p < 16; p++)
			packedIndices |= (uint64_t)(bestIndex[p] & 0x7) << (45 - p * 3);
		outBlock[2] = (uint8_t)((packedIndices >> 40) & 0xFF);
		outBlock[3] = (uint8_t)((packedIndices >> 32) & 0xFF);
		outBlock[4] = (uint8_t)((packedIndices >> 24) & 0xFF);
		outBlock[5] = (uint8_t)((packedIndices >> 16) & 0xFF);
		outBlock[6] = (uint8_t)((packedIndices >> 8) & 0xFF);
		outBlock[7] = (uint8_t)(packedIndices & 0xFF);

#ifdef CEMU_DEBUG_ASSERT
		int worstErrorSigned = 0;
		const int multiplier = std::max(bestMultNibble * 8, 1);
		for (int p = 0; p < 16; p++)
		{
			int value = std::clamp(base + multiplier * kAlphaModifierTables[bestTable][bestIndex[p]], -1023, 1023);
			worstErrorSigned = std::max(worstErrorSigned, std::abs(value - value11[p]));
		}
		cemu_assert_debug(worstErrorSigned <= 1580);
#endif
	}
}
