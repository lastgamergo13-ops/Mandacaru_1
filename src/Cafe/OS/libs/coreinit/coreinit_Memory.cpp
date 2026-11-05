#if defined(__ANDROID__)
#include "Cafe/HW/Espresso/Interpreter/PPCInterpreterInternal.h"
#include "Cafe/OS/libs/coreinit/coreinit_Thread.h"
#endif
#include "Common/DiagnosticsFlags.h"
#include "Cafe/OS/common/OSCommon.h"
#include "coreinit_Memory.h"
#include "Cafe/HW/Latte/Core/LatteBufferCache.h"
#include "Cafe/OS/RPL/rpl.h"
#include "Cafe/GraphicPack/GraphicPack2.h"
#include "Cafe/CafeSystem.h"

namespace coreinit
{

	void DCInvalidateRange(MPTR addr, uint32 size)
	{
		MPTR addrEnd = (addr + size + 0x1F) & ~0x1F;
		addr &= ~0x1F;
		//LatteBufferCache_notifyDCFlush(addr, addrEnd - addr);
	}

	void DCFlushRange(MPTR addr, uint32 size)
	{
		MPTR addrEnd = (addr + size + 0x1F) & ~0x1F;
		addr &= ~0x1F;
		LatteBufferCache_notifyDCFlush(addr, addrEnd - addr);
	}

	void DCFlushRangeNoSync(MPTR addr, uint32 size)
	{
		MPTR addrEnd = (addr + size + 0x1F) & ~0x1F;
		addr &= ~0x1F;
		LatteBufferCache_notifyDCFlush(addr, addrEnd - addr);
	}

	void DCStoreRange(MPTR addr, uint32 size)
	{
		MPTR addrEnd = (addr + size + 0x1F) & ~0x1F;
		addr &= ~0x1F;
		//LatteBufferCache_notifyDCFlush(addr, addrEnd - addr);
	}

	void DCStoreRangeNoSync(MPTR addr, uint32 size)
	{
		MPTR addrEnd = (addr + size + 0x1F) & ~0x1F;
		addr &= ~0x1F;
		LatteBufferCache_notifyDCFlush(addr, addrEnd - addr);
	}

	void DCZeroRange(MPTR addr, uint32 size)
	{
		MPTR alignedAddr = addr & ~31;
		uint32 cachlineOffset = addr & 31;
		uint32 blocks = (cachlineOffset + size + 31) / 32;

		if (blocks > 0)
		{
			memset(memory_getPointerFromVirtualOffset(alignedAddr), 0x00, blocks * 32);
			LatteBufferCache_notifyDCFlush(alignedAddr, blocks * 32);
		}
	}

	bool OSIsAddressRangeDCValid(uint32 startOffset, uint32 range)
	{
		uint32 endOffset = startOffset + range - 1;
		uint32 boundaryLow = 0xE8000000;
		uint32 boundaryHigh = 0xEC000000;
		if (startOffset < boundaryLow || startOffset >= boundaryHigh)
			return false;
		if (endOffset < boundaryLow || endOffset >= boundaryHigh)
			return false;
		return true;
	}

#if defined(__ANDROID__)
	// DIAGNOSTICO (Diag::Flag::GuestWatch): registra quando uma funcao de COPIA do emulador (memcpy/memmove/memset/OSBlockMove/OSBlockSet/snprintf)
	// escreve na regiao das tabelas do alocador do Watch Dogs (0x10760000-0x10762000). Os crashes mostram ali fragmentos de TEXTO ("sndE", "xtur",
	// "ryAr"): se o autor for uma funcao do emulador, aparece aqui com os argumentos e o chamador (LR); se nao aparecer, quem escreve e o proprio
	// codigo do jogo.
	void AndroidLogRegionWrite(const char* fn, uint32 dst, uint32 srcOrValue, uint32 size, bool srcIsValue)
	{
		if (!Diag::IsEnabled(Diag::Flag::GuestWatch))
			return;
		const uint64 end = (uint64)dst + (uint64)size;
		if (end <= 0x10760000ull || dst >= 0x10762000u)
			return;
		static std::atomic<uint32> s_count{0};
		if (s_count.fetch_add(1) >= 150)
			return;
		PPCInterpreter_t* h = PPCInterpreter_getCurrentInstance();
		OSThread_t* cur = OSGetCurrentThread();
		std::string preview;
		if (!srcIsValue && memory_isAddressRangeAccessible(srcOrValue, 16))
		{
			const uint8* p = (const uint8*)memory_getPointerFromVirtualOffset(srcOrValue);
			for (int i = 0; i < 16; i++)
			{
				preview += fmt::format("{:02x}", p[i]);
				if ((i & 3) == 3)
					preview += ' ';
			}
			preview += "\"";
			for (int i = 0; i < 16; i++)
				preview += (p[i] >= 0x20 && p[i] < 0x7F) ? (char)p[i] : '.';
			preview += "\"";
		}
		cemuLog_log(LogType::Force, "[Escrita] {}(destino={:08x}, {}={:08x}, tamanho={:#x}) chamada de LR={:08x} thread=\"{}\" conteudo da origem: {}", fn, dst, srcIsValue ? "valor" : "origem", srcOrValue, size,
			h ? h->spr.LR : 0u, (cur && cur->threadName.GetMPTR() != 0) ? cur->threadName.GetPtr() : "?", preview.empty() ? "-" : preview);
	}
#endif

	void* coreinit_memset(void* dst, uint32 value, uint32 size)
	{
#if defined(__ANDROID__)
		AndroidLogRegionWrite("memset", memory_getVirtualOffsetFromPointer(dst), value, size, true);
#endif
		memset(dst, value, size);
		return dst;
	}

	void* coreinit_memcpy(MEMPTR<void> dst, MEMPTR<void> src, uint32 size)
	{
		if (dst.GetMPTR() == 0xFFFFFFFF)
		{
			// games this was seen in: The Swapper
			// this may be a bug in the game. The last few bytes of the address space are writable, but wrap-around behavior of COS memcpy is unknown
			cemu_assert_debug(false);
		}
#if defined(__ANDROID__)
		AndroidLogRegionWrite("memcpy", dst.GetMPTR(), src.GetMPTR(), size, false);
#endif
		if (size > 0)
		{
			memcpy(dst.GetPtr(), src.GetPtr(), size);
			// always flushes the cache!
			LatteBufferCache_notifyDCFlush(dst.GetMPTR(), size);
		}

		return dst.GetPtr();
	}

	void* coreinit_memmove(MEMPTR<void> dst, void* src, uint32 size)
	{
#if defined(__ANDROID__)
		AndroidLogRegionWrite("memmove", dst.GetMPTR(), memory_getVirtualOffsetFromPointer(src), size, false);
#endif
		if (size > 0)
		{
			memmove(dst.GetPtr(), src, size);
			// always flushes the cache!
			LatteBufferCache_notifyDCFlush(dst.GetMPTR(), size);
		}
		return dst.GetPtr();
	}

	void* OSBlockMove(MEMPTR<void> dst, MEMPTR<void> src, uint32 size, bool flushDC)
	{
#if defined(__ANDROID__)
		AndroidLogRegionWrite("OSBlockMove", dst.GetMPTR(), src.GetMPTR(), size, false);
#endif
		if (size > 0)
		{
			memmove(dst.GetPtr(), src.GetPtr(), size);
			if (flushDC)
				LatteBufferCache_notifyDCFlush(dst.GetMPTR(), size);
		}
		return dst.GetPtr();
	}

	void* OSBlockSet(MEMPTR<void> dst, uint32 value, uint32 size)
	{
#if defined(__ANDROID__)
		AndroidLogRegionWrite("OSBlockSet", dst.GetMPTR(), value & 0xFF, size, true);
#endif
		memset(dst.GetPtr(), value&0xFF, size);
		return dst.GetPtr();
	}

	MPTR OSEffectiveToPhysical(MPTR effectiveAddr)
	{
		MPTR physicalAddr = memory_virtualToPhysical(effectiveAddr);
		return physicalAddr;
	}

	void OSMemoryBarrier()
	{
		// no-op
	}

	void OSGetMemBound(sint32 memType, MEMPTR<void>* offsetOutput, uint32be* sizeOutput)
	{
		MPTR memAddr = MPTR_NULL;
		uint32 memSize = 0;

		/*
 		Data taken from browser dump:
		type	start		size
		MEM1	0xF4000000	0x02000000
		MEM2	0x106DE000	0x170C2000
		*/
		if (memType == 1)
		{
			// MEM1
			memAddr = mmuRange_MEM1.getBase();
			memSize = mmuRange_MEM1.getSize();
		}
		else if (memType == 2)
		{
			// MEM2
			uint32 currentRPLAllocatorOffset = RPLLoader_GetDataAllocatorAddr();

			// due to differences in our library implementations we currently allocate less memory for the OS/RPLs than on the actual hardware,
			// as a result more memory is available to games
			// however, some games crash due to internal overflows if there is too much memory available

			// here we artificially reduce the available memory for the affected games
			uint64 titleId = CafeSystem::GetForegroundTitleId();
			if (
				titleId == 0x0005000010132400ULL || // Lego Marvel Super Heroes (EU)
				titleId == 0x0005000010132B00ULL || // Lego Marvel Super Heroes (US)
				titleId == 0x0005000010194200ull || // Lego Dimensions (US)
				titleId == 0x0005000010195D00ull || // Lego Dimensions (EU)
				titleId == 0x00050000101A6200ull || // Lego Jurassic World (US)
				titleId == 0x00050000101A5C00 || // Lego Jurassic World (EU)
				titleId == 0x000500001014DE00 || // The Lego Movie Videogame (US)
				titleId == 0x000500001014E000 || // The Lego Movie Videogame (EU)
				titleId == 0x0005000010168D00 || // Lego The Hobbit (EU)
				titleId == 0x000500001016A700 || // Lego The Hobbit (JP)
				// The Hobbit US title id?
				titleId == 0x00050000101DAB00 || // Lego Star Wars: The Force Awakens  (US)
				titleId == 0x00050000101DAA00 ||  // Lego Star Wars: The Force Awakens  (EU)
				// LEGO Batman 3: BEYOND GOTHAM 
				titleId == 0x000500001016A400 || // EU
				titleId == 0x000500001016AD00 || // US
				// Lego Marvel Avengers
				titleId == 0x00050000101BE900 || // EU
				titleId == 0x00050000101BEF00 || // US
				// LEGO BATMAN 2: DC Super Heroes 
				titleId == 0x0005000010135500 || // EU
				titleId == 0x0005000010135E00 // US
				)
			{
				cemuLog_logDebug(LogType::Force, "Hack: Reduce available memory to simulate loaded RPLs");
				currentRPLAllocatorOffset += (48 * 1024 * 1024); // 48MB
			}
#if defined(__ANDROID__)
			{
				// TESTE: o Watch Dogs pega TODA a MEM2 oferecida de uma vez e a divide com o alocador proprio; informar menos memoria evitou o
				// crash do inicio do jogo. O Wii U real reserva mais memoria para o sistema/RPLs do que nos. Valor escolhido na tela
				// (Experimental > Logs e Diagnostico); so uma das chaves fica ligada.
				uint32 reduceMB = 0;
				if (Diag::IsEnabled(Diag::Flag::ReduceMem512))
					reduceMB = 512;
				else if (Diag::IsEnabled(Diag::Flag::ReduceMem384))
					reduceMB = 384;
				else if (Diag::IsEnabled(Diag::Flag::ReduceMem256))
					reduceMB = 256;
				else if (Diag::IsEnabled(Diag::Flag::ReduceGameMem))
					reduceMB = 128;
				else if (Diag::IsEnabled(Diag::Flag::ReduceMem64))
					reduceMB = 64;
				if (reduceMB)
				{
					currentRPLAllocatorOffset += (reduceMB * 1024 * 1024);
					static uint32 s_loggedFor = 0;
					if (s_loggedFor != reduceMB)
					{
						s_loggedFor = reduceMB;
						cemuLog_log(LogType::Force, "[Memoria] teste ligado: MEM2 informada ao jogo comeca em 0x{:08x} ({} MB a menos)", currentRPLAllocatorOffset, reduceMB);
					}
				}
			}
#endif
			memAddr = currentRPLAllocatorOffset;
			memSize = mmuRange_MEM2.getEnd() - currentRPLAllocatorOffset;
		}
		else
		{
			cemu_assert_debug(false);
		}
#if defined(__ANDROID__)
		{
			// DIAGNOSTICO: o que o jogo recebe como memoria disponivel (tipo 1 = MEM1, 2 = MEM2)
			static std::atomic<uint32> s_memBoundLogs{0};
			if (s_memBoundLogs.fetch_add(1) < 8)
				cemuLog_log(LogType::Force, "[Memoria] OSGetMemBound(tipo={}) -> inicio=0x{:08x} tamanho=0x{:08x} ({} MB) fim=0x{:08x}", memType, memAddr, memSize, memSize / (1024 * 1024), memAddr + memSize);
		}
#endif
		if (offsetOutput)
			*offsetOutput = memAddr;
		if (sizeOutput)
			*sizeOutput = memSize;
	}

	void InitializeMemory()
	{
		cafeExportRegister("coreinit", DCInvalidateRange, LogType::Placeholder);
		cafeExportRegister("coreinit", DCFlushRange, LogType::Placeholder);
		cafeExportRegister("coreinit", DCFlushRangeNoSync, LogType::Placeholder);
		cafeExportRegister("coreinit", DCStoreRange, LogType::Placeholder);
		cafeExportRegister("coreinit", DCStoreRangeNoSync, LogType::Placeholder);
		cafeExportRegister("coreinit", DCZeroRange, LogType::Placeholder);
		cafeExportRegister("coreinit", OSIsAddressRangeDCValid, LogType::Placeholder);

		cafeExportRegisterFunc(coreinit_memcpy, "coreinit", "memcpy", LogType::Placeholder);
		cafeExportRegisterFunc(coreinit_memset, "coreinit", "memset", LogType::Placeholder);
		cafeExportRegisterFunc(coreinit_memmove, "coreinit", "memmove", LogType::Placeholder);
		cafeExportRegister("coreinit", OSBlockMove, LogType::Placeholder);
		cafeExportRegister("coreinit", OSBlockSet, LogType::Placeholder);

		cafeExportRegister("coreinit", OSEffectiveToPhysical, LogType::Placeholder);
		cafeExportRegister("coreinit", OSMemoryBarrier, LogType::Placeholder);

		cafeExportRegister("coreinit", OSGetMemBound, LogType::Placeholder);
	}

}
