#include "PPCRecompilerCache.h"
#include <atomic>
#include "Cemu/FileCache/FileCache.h"
#include "config/ActiveSettings.h"
#include "config/CemuConfig.h"
#include "util/crypto/crc32.h"
#include "util/helpers/Serializer.h"
#include "Cafe/HW/MMU/MMU.h"

#if defined(__linux__) || defined(__ANDROID__)
#include <link.h>
#include <elf.h>
#include <cstring>
#endif
#if defined(ARCH_X86_64)
#include "BackendX64/BackendX64.h"
#elif defined(__aarch64__)
#include "BackendAArch64/BackendAArch64.h"
#endif

namespace
{
	// bump this whenever the on-disk layout (or the meaning of a relocation type) changes,
	// so that old, incompatible cache files are simply treated as empty rather than crash-loaded
	constexpr uint32 kCacheFormatVersion = 2; // V11.43: v1 gravou codigo com enderecos absolutos (canario/call_imm) que quebram apos reiniciar

	// entry point table is stored as flat (ppcAddress, codeOffset) pairs rather than the
	// std::pair the rest of the recompiler uses internally, so it can be treated as POD data
	struct CacheEntryPoint
	{
		uint32 ppcAddress;
		uint32 codeOffset;
	};

	FileCache* s_aotCache = nullptr;
	std::atomic<uint32> s_aotHits{0}, s_aotMisses{0}, s_aotStores{0}; // V11.39: contadores para o log
	std::atomic<uint32> s_aotLookups{0}, s_aotRejectedCRC{0}, s_aotRejectedOther{0}, s_aotNotCacheable{0}; // diagnostico: motivos de nao aproveitar o cache

	bool IsAOTCacheEnabled()
	{
		return GetConfig().enable_recompiler_aot_cache.GetValue();
	}

	// V11.55: identidade do BUILD que gerou o codigo nativo. O codigo JIT em cache embute offsets de structs do interpretador,
	// convencoes de registradores/frame do backend e enderecos relativos a tabelas; se o app for recompilado (ou o backend
	// AArch64 mudar) e o cache antigo for reaproveitado, o app executa codigo velho e corrompe memoria (crashes "aleatorios" ate
	// dentro do driver da GPU). A constante kCacheFormatVersion e manual e nao cobre isso. Aqui usamos o GNU build-id do .so que
	// contem este codigo (muda a cada link); sem build-id, cai para data/hora de compilacao deste arquivo.
	uint32 GetBuildIdentityHash()
	{
		uint64 h = 1469598103934665603ull;
		auto mix = [&h](const void* d, size_t n) { for (size_t i = 0; i < n; i++) { h ^= ((const uint8*)d)[i]; h *= 1099511628211ull; } };
		bool found = false;
#if defined(__linux__) || defined(__ANDROID__)
		struct Ctx { uintptr_t addr; uint64* h; bool found; } ctx{ (uintptr_t)&GetBuildIdentityHash, &h, false };
		dl_iterate_phdr([](dl_phdr_info* info, size_t, void* user) -> int
		{
			Ctx* cx = (Ctx*)user;
			bool contains = false;
			for (int i = 0; i < info->dlpi_phnum && !contains; i++)
			{
				const auto& ph = info->dlpi_phdr[i];
				if (ph.p_type != PT_LOAD)
					continue;
				const uintptr_t base = info->dlpi_addr + ph.p_vaddr;
				contains = cx->addr >= base && cx->addr < base + ph.p_memsz;
			}
			if (!contains)
				return 0;
			for (int i = 0; i < info->dlpi_phnum; i++)
			{
				const auto& ph = info->dlpi_phdr[i];
				if (ph.p_type != PT_NOTE)
					continue;
				const uint8* p = (const uint8*)(info->dlpi_addr + ph.p_vaddr);
				const uint8* end = p + ph.p_memsz;
				while (p + sizeof(ElfW(Nhdr)) <= end)
				{
					const ElfW(Nhdr)* nh = (const ElfW(Nhdr)*)p;
					const uint8* name = p + sizeof(ElfW(Nhdr));
					const uint8* desc = name + ((nh->n_namesz + 3u) & ~3u);
					if (desc > end || desc + nh->n_descsz > end)
						break;
					if (nh->n_type == NT_GNU_BUILD_ID && nh->n_namesz == 4 && memcmp(name, "GNU", 4) == 0 && nh->n_descsz > 0)
					{
						for (uint32 k = 0; k < nh->n_descsz; k++)
						{
							*cx->h ^= desc[k];
							*cx->h *= 1099511628211ull;
						}
						cx->found = true;
						return 1;
					}
					p = desc + ((nh->n_descsz + 3u) & ~3u);
				}
			}
			return 1;
		}, &ctx);
		found = ctx.found;
#endif
		if (!found)
		{
			static const char kBuildStamp[] = __DATE__ " " __TIME__;
			mix(kBuildStamp, sizeof(kBuildStamp));
		}
		static bool s_logged = false;
		if (!s_logged)
		{
			s_logged = true;
			cemuLog_log(LogType::Force, "[AOT] identidade do build para o cache do recompilador: {:08x} ({})", (uint32)(h ^ (h >> 32)), found ? "GNU build-id" : "data/hora de compilacao");
		}
		return (uint32)(h ^ (h >> 32));
	}

	uint32 GetCacheExtraVersion()
	{
#if defined(ARCH_X86_64)
		constexpr uint32 archTag = 1;
#elif defined(__aarch64__)
		constexpr uint32 archTag = 2;
#else
		constexpr uint32 archTag = 0;
#endif
		// the cache file itself is already per-title (see PPCRecompilerCache_Load), so this only
		// needs to guard against format changes and mixing code between architectures/builds
		static const uint32 s_buildIdentity = GetBuildIdentityHash();
		return ((kCacheFormatVersion * 0x100) + archTag + 0x50436baa) ^ s_buildIdentity;
	}
}

std::string PPCRecompilerCache_GetStatsString()
{
	if (!IsAOTCacheEnabled())
		return "[AOT] cache do recompilador DESLIGADO nas configuracoes";
	if (!s_aotCache)
		return "[AOT] cache do recompilador ligado, mas o arquivo NAO esta aberto";
	const uint32 hits = s_aotHits, misses = s_aotMisses;
	const uint32 total = hits + misses;
	return fmt::format("[AOT] {} consultas: {} acertos ({}%), {} falhas (compilou na hora), {} rejeitadas por CRC do codigo do jogo, {} rejeitadas por outros motivos, {} gravadas, {} nao gravaveis (endereco absoluto)",
		total, hits, total ? (hits * 100 / total) : 0, misses, (uint32)s_aotRejectedCRC, (uint32)s_aotRejectedOther, (uint32)s_aotStores, (uint32)s_aotNotCacheable);
}

void PPCRecompilerCache_Load(TitleId titleId)
{
	PPCRecompilerCache_Unload();
	if (!IsAOTCacheEnabled())
		return;
#if !defined(ARCH_X86_64) && !defined(__aarch64__)
	// no injector implemented for this architecture - disable rather than pretend to cache
	return;
#else
	std::error_code ec;
	fs::create_directories(ActiveSettings::GetCachePath("recompilerCache"), ec);
	fs::path cachePath = ActiveSettings::GetCachePath("recompilerCache/{:016x}_ppcRec.bin", (uint64)titleId);
	s_aotHits = 0; s_aotMisses = 0; s_aotStores = 0; s_aotLookups = 0; s_aotRejectedCRC = 0; s_aotRejectedOther = 0; s_aotNotCacheable = 0;
	s_aotCache = FileCache::Open(cachePath, true, GetCacheExtraVersion());
	if (!s_aotCache)
	{
		cemuLog_log(LogType::Force, "Unable to open or create recompiler AOT cache file \"{}\"", _pathToUtf8(cachePath));
		return;
	}
	// this cache is consulted on every cold function entry, so keep lookups cheap rather than paying zlib decompression on the hot path
	s_aotCache->UseCompression(false);
	cemuLog_log(LogType::Force, "[AOT] cache do recompilador aberto: {} ({} funcoes ja gravadas no arquivo)", _pathToUtf8(cachePath), (sint32)s_aotCache->GetFileCount());
#endif
}

void PPCRecompilerCache_Unload()
{
	if (s_aotCache)
	{
		cemuLog_log(LogType::Force, "[AOT] cache do recompilador: {} funcoes carregadas do cache, {} nao estavam no cache, {} gravadas nesta sessao", (uint32)s_aotHits, (uint32)s_aotMisses, (uint32)s_aotStores);
		delete s_aotCache;
		s_aotCache = nullptr;
	}
}

PPCRecFunction_t* PPCRecompilerCache_TryLoad(const PPCFunctionBoundaryTracker::PPCRange_t& range, std::vector<std::pair<MPTR, uint32>>& entryPointsOut)
{
	entryPointsOut.clear();
	if (!IsAOTCacheEnabled() || !s_aotCache)
		return nullptr;

	{
		// a cada 500 consultas escreve um resumo no log (o resumo final so sairia ao fechar o jogo, o que nunca acontece num crash)
		const uint32 n = ++s_aotLookups;
		if (n % 500 == 0)
			cemuLog_log(LogType::Force, "{}", PPCRecompilerCache_GetStatsString());
	}
	std::vector<uint8> blob;
	if (!s_aotCache->GetFile({ (uint64)range.startAddress, 0 }, blob))
	{
		s_aotMisses++;
		return nullptr; // cache miss
	}

	MemStreamReader reader(blob.data(), (sint32)blob.size());
	uint32 formatVersion = reader.readBE<uint32>();
	uint32 ppcAddress = reader.readBE<uint32>();
	uint32 ppcSize = reader.readBE<uint32>();
	uint32 storedCRC = reader.readBE<uint32>();
	std::vector<CacheEntryPoint> entryPointsRaw = reader.readPODVector<CacheEntryPoint>();
	std::vector<PPCRecRelocEntry> relocations = reader.readPODVector<PPCRecRelocEntry>();
	uint32 codeSize = reader.readBE<uint32>();
	std::span<uint8> codeBytes = reader.readDataNoCopy(codeSize);
	if (reader.hasError())
	{
		s_aotRejectedOther++;
		return nullptr; // truncated/corrupt entry - ignore and recompile normally
	}

	if (formatVersion != kCacheFormatVersion)
	{
		s_aotRejectedOther++;
		return nullptr;
	}
	if (ppcAddress != range.startAddress || ppcSize != range.length)
	{
		s_aotRejectedOther++;
		return nullptr; // the boundary tracker disagrees with the shape that was cached - safer to just recompile
	}

	// validate the cached code was compiled from the same guest instruction bytes that are
	// currently in memory (protects against game updates, romhacks, or a different game entirely
	// reusing the same title id's cache directory)
	const uint8* srcCode = memory_getPointerFromVirtualOffsetAllowNull(ppcAddress);
	if (!srcCode)
	{
		s_aotRejectedOther++;
		return nullptr;
	}
	if (crc32_calc(srcCode, ppcSize) != storedCRC)
	{
		s_aotRejectedCRC++;
		return nullptr;
	}

	void* execCode = nullptr;
#if defined(ARCH_X86_64)
	execCode = PPCRecompiler_injectX64CachedCode(codeBytes.data(), codeBytes.size(), relocations);
#elif defined(__aarch64__)
	execCode = PPCRecompiler_injectAArch64CachedCode(codeBytes.data(), codeBytes.size(), relocations);
#endif
	if (!execCode)
	{
		s_aotRejectedOther++;
		return nullptr;
	}

	PPCRecFunction_t* ppcRecFunc = new PPCRecFunction_t();
	ppcRecFunc->ppcAddress = ppcAddress;
	ppcRecFunc->ppcSize = ppcSize;
	ppcRecFunc->x86Code = execCode;
	ppcRecFunc->x86Size = codeBytes.size();
	ppcRecRange_t recRange;
	recRange.ppcAddress = ppcAddress;
	recRange.ppcSize = ppcSize;
	recRange.storedRange = nullptr;
	ppcRecFunc->list_ranges.push_back(recRange);

	entryPointsOut.reserve(entryPointsRaw.size());
	for (auto& ep : entryPointsRaw)
		entryPointsOut.emplace_back((MPTR)ep.ppcAddress, ep.codeOffset);

	s_aotHits++;
	return ppcRecFunc;
}

void PPCRecompilerCache_Store(PPCRecFunction_t* ppcRecFunc, const std::vector<std::pair<MPTR, uint32>>& entryPoints)
{
	if (!IsAOTCacheEnabled() || !s_aotCache)
		return;
	if (!ppcRecFunc || !ppcRecFunc->x86Code || ppcRecFunc->x86Size == 0)
		return;
	if (!ppcRecFunc->aotCacheable)
	{
		s_aotNotCacheable++;
		return; // V11.43: contem endereco absoluto nao relocavel
	}

	const uint8* srcCode = memory_getPointerFromVirtualOffsetAllowNull(ppcRecFunc->ppcAddress);
	if (!srcCode)
		return;
	uint32 ppcCRC = crc32_calc(srcCode, ppcRecFunc->ppcSize);

	std::vector<CacheEntryPoint> entryPointsRaw;
	entryPointsRaw.reserve(entryPoints.size());
	for (auto& ep : entryPoints)
		entryPointsRaw.emplace_back(CacheEntryPoint{ (uint32)ep.first, ep.second });

	MemStreamWriter writer(ppcRecFunc->x86Size + 256);
	writer.writeBE<uint32>(kCacheFormatVersion);
	writer.writeBE<uint32>(ppcRecFunc->ppcAddress);
	writer.writeBE<uint32>(ppcRecFunc->ppcSize);
	writer.writeBE<uint32>(ppcCRC);
	writer.writePODVector(entryPointsRaw);
	writer.writePODVector(ppcRecFunc->relocations);
	writer.writeBE<uint32>((uint32)ppcRecFunc->x86Size);
	writer.writeData(ppcRecFunc->x86Code, ppcRecFunc->x86Size);

	std::vector<uint8> blob;
	writer.getResultAndReset(blob);

	// the actual disk write happens on FileCache's own background thread - never blocks recompilation
	s_aotStores++;
	s_aotCache->AddFileAsync({ (uint64)ppcRecFunc->ppcAddress, 0 }, blob.data(), (sint32)blob.size());
}
