#include "BackendAArch64.h"

#pragma push_macro("CSIZE")
#undef CSIZE
#include <xbyak_aarch64.h>
#pragma pop_macro("CSIZE")
#include <xbyak_aarch64_util.h>

#include <cstddef>

#include "../PPCRecompiler.h"
#include "Common/precompiled.h"
#include "Common/cpu_features.h"
#include "HW/Espresso/Interpreter/PPCInterpreterInternal.h"
#include "HW/Espresso/Interpreter/PPCInterpreterHelper.h"
#include "HW/Espresso/PPCState.h"
#include "Common/ExceptionHandler/ExceptionHandler.h"

#ifdef __ANDROID__
#include <sys/mman.h>
#include <unistd.h>
#include <mutex>
#include <unordered_map>
#include <cerrno>
#include <atomic>
#endif

using namespace Xbyak_aarch64;

constexpr uint32 TEMP_GPR_1_ID = 25;
constexpr uint32 TEMP_GPR_2_ID = 26;
constexpr uint32 PPC_RECOMPILER_INSTANCE_DATA_REG_ID = 27;
constexpr uint32 MEMORY_BASE_REG_ID = 28;
constexpr uint32 HCPU_REG_ID = 29;

constexpr uint32 TEMP_FPR_ID = 31;

struct FPReg
{
	explicit FPReg(size_t index)
		: index(index), VReg(index), QReg(index), DReg(index), SReg(index), HReg(index), BReg(index)
	{
	}
	const size_t index;
	const ::VReg VReg;
	const ::QReg QReg;
	const ::DReg DReg;
	const ::SReg SReg;
	const ::HReg HReg;
	const ::BReg BReg;
};

struct GPReg
{
	explicit GPReg(size_t index)
		: index(index), XReg(index), WReg(index)
	{
	}
	const size_t index;
	const ::XReg XReg;
	const ::WReg WReg;
};

static const XReg HCPU_REG{HCPU_REG_ID}, PPC_REC_INSTANCE_REG{PPC_RECOMPILER_INSTANCE_DATA_REG_ID}, MEM_BASE_REG{MEMORY_BASE_REG_ID};
static const GPReg TEMP_GPR1{TEMP_GPR_1_ID};
static const GPReg TEMP_GPR2{TEMP_GPR_2_ID};
static const GPReg LR{TEMP_GPR_2_ID};

static const FPReg TEMP_FPR{TEMP_FPR_ID};

static const util::Cpu s_cpu;

#ifdef __ANDROID__
// Em dispositivos Android recentes (principalmente com targetSdk >= 34, que já é o caso
// deste app) o sistema aplica "Memory Deny Write Execute" (MDWE) e/ou proteções W^X
// equivalentes de fabricante (ex: proteções de kernel da Samsung). Isso bloqueia, de forma
// silenciosa em alguns aparelhos, qualquer mapeamento de memória que passe a ser gravável E
// executável ao mesmo tempo - e é exatamente isso que o alocador padrão do xbyak_aarch64 faz
// (aloca a página como RW e depois chama mprotect() pra virar RWX/RE). O mprotect() parece
// funcionar (retorna sucesso), mas na hora de EXECUTAR o código gerado o sistema barra e mata
// o app com SIGSEGV (SEGV_ACCERR) - exatamente o crash relatado ao rodar um jogo.
//
// A correção é nunca deixar uma página ser gravável e executável ao mesmo tempo, e nunca
// fazer uma página gravável virar executável via mprotect(). Para isso, mapeamos a MESMA
// memória física duas vezes, em dois endereços virtuais diferentes: um só leitura+escrita
// (usado pelo recompilador pra gerar o código) e outro só leitura+execução (usado de fato
// pra rodar o código). Como os dois mapeamentos espelham os mesmos offsets da mesma memória
// física, os saltos relativos calculados durante a geração do código continuam válidos na
// hora de executar através do outro endereço.
class DualMapAllocator : public Allocator
{
	struct Mapping
	{
		void* execAddr;
		size_t size;
		int fd;
	};

	inline static std::mutex s_mutex;
	inline static std::unordered_map<void*, Mapping> s_mappings;

  public:
	uint32* alloc(size_t size) override
	{
		size_t pageSize = (size_t)sysconf(_SC_PAGESIZE);
		size = (size + pageSize - 1) & ~(pageSize - 1);

		int fd = memfd_create("cemu_jit_code", 0);
		if (fd < 0)
		{
			cemuLog_log(LogType::Force, "[JIT] memfd_create falhou, errno={}", errno);
			cemuLog_waitForFlush();
			return nullptr;
		}
		if (ftruncate(fd, size) != 0)
		{
			cemuLog_log(LogType::Force, "[JIT] ftruncate falhou, errno={}", errno);
			cemuLog_waitForFlush();
			close(fd);
			return nullptr;
		}
		void* writeAddr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		if (writeAddr == MAP_FAILED)
		{
			cemuLog_log(LogType::Force, "[JIT] mmap RW falhou, errno={}", errno);
			cemuLog_waitForFlush();
			close(fd);
			return nullptr;
		}
		void* execAddr = mmap(nullptr, size, PROT_READ | PROT_EXEC, MAP_SHARED, fd, 0);
		if (execAddr == MAP_FAILED)
		{
			cemuLog_log(LogType::Force, "[JIT] mmap RX falhou, errno={}", errno);
			cemuLog_waitForFlush();
			munmap(writeAddr, size);
			close(fd);
			return nullptr;
		}
		// já podemos fechar o fd - as duas mmap regions continuam válidas até serem
		// desmapeadas, mesmo sem o fd aberto
		close(fd);
		{
			std::lock_guard<std::mutex> lock(s_mutex);
			s_mappings[writeAddr] = { execAddr, size, -1 };
			static std::atomic<int> s_logCount{0};
			if (s_logCount.fetch_add(1) < 500)
				cemuLog_log(LogType::Force, "[JIT] alloc OK size={:x} write={:x} exec={:x} total_mappings={}", size, (uintptr_t)writeAddr, (uintptr_t)execAddr, s_mappings.size());
		}
		return reinterpret_cast<uint32*>(writeAddr);
	}

	void free(uint32* p) override
	{
		if (!p)
			return;
		std::lock_guard<std::mutex> lock(s_mutex);
		auto it = s_mappings.find(p);
		if (it == s_mappings.end())
		{
			cemuLog_log(LogType::Force, "[JIT] free: ponteiro write={:x} nao encontrado em s_mappings", (uintptr_t)p);
			cemuLog_waitForFlush();
			return;
		}
		munmap(it->second.execAddr, it->second.size);
		munmap(p, it->second.size);
		s_mappings.erase(it);
	}

	[[nodiscard]] bool useProtect() const override
	{
		// os dois mapeamentos já nascem com a permissão certa (RW e RX) e nunca mudam -
		// nunca devemos chamar mprotect() nelas
		return false;
	}

	// traduz um ponteiro do mapeamento gravável (top_ / getCode() do CodeArray) para o
	// endereço executável correspondente - é esse endereço que deve de fato ser chamado
	static void* TranslateToExecAddress(void* writeAddr)
	{
		void* execPtr = nullptr;
		bool found = false;
		{
			std::lock_guard<std::mutex> lock(s_mutex);
			for (auto& [base, mapping] : s_mappings)
			{
				if (writeAddr >= base && writeAddr < (uint8*)base + mapping.size)
				{
					execPtr = (uint8*)mapping.execAddr + ((uint8*)writeAddr - (uint8*)base);
					static std::atomic<int> s_logCount{0};
					if (s_logCount.fetch_add(1) < 500)
						cemuLog_log(LogType::Force, "[JIT] translate write={:x} -> exec={:x} (base={:x} size={:x})", (uintptr_t)writeAddr, (uintptr_t)execPtr, (uintptr_t)base, mapping.size);
					found = true;
					break;
				}
			}
			if (!found)
				cemuLog_log(LogType::Force, "[JIT] translate FALHOU: write={:x} nao pertence a nenhuma alocacao (total_mappings={})", (uintptr_t)writeAddr, s_mappings.size());
		}
		if (!found)
		{
			// esse é o log mais crítico de todos: se aparecer, achamos a causa exata do
			// crash (um código pulando pro endereço GRAVÁVEL, não executável, de um bloco
			// JIT que não existe/já foi liberado). Esse caminho só deveria disparar se algo
			// já estiver errado, então o custo do flush aqui é aceitável.
			cemuLog_waitForFlush();
			cemu_assert_suspicious(); // não deveria acontecer: ponteiro não pertence a nenhuma alocação do DualMapAllocator
			return writeAddr;
		}
		return execPtr;
	}
};
#endif

class AArch64Allocator : public Allocator
{
  private:
#ifdef __ANDROID__
	inline static DualMapAllocator s_allocator;
#elif defined(XBYAK_USE_MMAP_ALLOCATOR)
	inline static MmapAllocator s_allocator;
#else
	inline static Allocator s_allocator;
#endif
	Allocator* m_allocatorImpl;
	bool m_freeDisabled = false;

  public:
	AArch64Allocator()
		: m_allocatorImpl(reinterpret_cast<Allocator*>(&s_allocator)) {}

	uint32* alloc(size_t size) override
	{
		return m_allocatorImpl->alloc(size);
	}

	void setFreeDisabled(bool disabled)
	{
		m_freeDisabled = disabled;
	}

	void free(uint32* p) override
	{
		if (!m_freeDisabled)
			m_allocatorImpl->free(p);
	}

	[[nodiscard]] bool useProtect() const override
	{
		return !m_freeDisabled && m_allocatorImpl->useProtect();
	}

	// No Android, o ponteiro retornado por CodeArray::getCode() aponta pro mapeamento
	// GRAVÁVEL, que não é executável. Antes de chamar/saltar pra ele é preciso traduzir
	// pro endereço executável correspondente e garantir que o cache de instruções desse
	// endereço (que é uma mmap fisicamente diferente da usada pra escrever o código) seja
	// invalidado - a invalidação de cache feita internamente pela lib usa o endereço
	// gravável, que não necessariamente alcança as linhas de i-cache do endereço
	// executável. Em outras plataformas isso é um no-op (só retorna o mesmo ponteiro).
	static void* FinalizeExecutableCode(void* codePtr, size_t codeSize)
	{
#ifdef __ANDROID__
		void* execPtr = DualMapAllocator::TranslateToExecAddress(codePtr);
		__builtin___clear_cache((char*)execPtr, (char*)execPtr + codeSize);
		return execPtr;
#else
		return codePtr;
#endif
	}
};

struct UnconditionalJumpInfo
{
	IMLSegment* target;
};

struct ConditionalRegJumpInfo
{
	IMLSegment* target;
	WReg regBool;
	bool mustBeTrue;
};

struct NegativeRegValueJumpInfo
{
	IMLSegment* target;
	WReg regValue;
};

using JumpInfo = std::variant<
	UnconditionalJumpInfo,
	ConditionalRegJumpInfo,
	NegativeRegValueJumpInfo>;

struct AArch64GenContext_t : CodeGenerator
{
	explicit AArch64GenContext_t(Allocator* allocator = nullptr);
	void enterRecompilerCode();
	void leaveRecompilerCode();
	void emitRegisterCanaryCheck(const char* checkpointTag);

	void r_name(IMLInstruction* imlInstruction);
	void name_r(IMLInstruction* imlInstruction);
	bool r_s32(IMLInstruction* imlInstruction);
	bool r_r(IMLInstruction* imlInstruction);
	bool r_r_s32(IMLInstruction* imlInstruction);
	bool r_r_s32_carry(IMLInstruction* imlInstruction);
	bool r_r_r(IMLInstruction* imlInstruction);
	bool r_r_r_carry(IMLInstruction* imlInstruction);
	void compare(IMLInstruction* imlInstruction);
	void compare_s32(IMLInstruction* imlInstruction);
	bool load(IMLInstruction* imlInstruction, bool indexed);
	bool store(IMLInstruction* imlInstruction, bool indexed);
	void atomic_cmp_store(IMLInstruction* imlInstruction);
	bool macro(IMLInstruction* imlInstruction);
	void call_imm(IMLInstruction* imlInstruction);
	bool fpr_load(IMLInstruction* imlInstruction, bool indexed);
	bool fpr_store(IMLInstruction* imlInstruction, bool indexed);
	void fpr_r_r(IMLInstruction* imlInstruction);
	void fpr_r_r_r(IMLInstruction* imlInstruction);
	void fpr_r_r_r_r(IMLInstruction* imlInstruction);
	void fpr_r(IMLInstruction* imlInstruction);
	void fpr_compare(IMLInstruction* imlInstruction);
	void cjump(IMLInstruction* imlInstruction, IMLSegment* imlSegment);
	void jump(IMLSegment* imlSegment);
	void conditionalJumpCycleCheck(IMLSegment* imlSegment);

	static constexpr size_t MAX_JUMP_INSTR_COUNT = 2;
	std::list<std::pair<size_t, JumpInfo>> jumps;
	void prepareJump(JumpInfo&& jumpInfo)
	{
		jumps.emplace_back(getSize(), jumpInfo);
		for (int i = 0; i < MAX_JUMP_INSTR_COUNT; ++i)
			nop();
	}

	// Absolute host addresses (e.g. PPCRecompiler_virtualHLE) embedded in the generated code by
	// movAbsFixed() below. Only valid for this process run (ASLR); the AOT disk cache uses this
	// list to re-patch every such location if this function's code is loaded in a later run.
	std::vector<struct PPCRecRelocEntry> relocations;

	// Loads a 64-bit absolute address into dst using an UNCONDITIONAL, always-4-instruction
	// (16 byte) movz+movk+movk+movk sequence, and records a relocation entry for it.
	// This must NOT use the regular mov(XReg, uint64) pseudo-instruction: that helper emits a
	// variable number of instructions depending on how many 16-bit chunks of the value are zero,
	// which is fine for a value used only within this run, but breaks cache re-patching - the
	// value that gets patched in on a later run will almost certainly have a different zero-chunk
	// pattern (ASLR), which would shift the size/layout of a variable-length encoding and corrupt
	// every instruction after it. A fixed instruction count keeps the patch a simple in-place
	// overwrite of the four immediate fields, independent of the value.
	void movAbsFixed(const XReg& dst, uint64 value, PPCRecRelocType relocType)
	{
		relocations.push_back({ (uint32)getSize(), relocType, (uint8)dst.getIdx() });
		movz(dst, (uint32)(value & 0xFFFF), 0);
		movk(dst, (uint32)((value >> 16) & 0xFFFF), 16);
		movk(dst, (uint32)((value >> 32) & 0xFFFF), 32);
		movk(dst, (uint32)((value >> 48) & 0xFFFF), 48);
	}

	std::map<IMLSegment*, size_t> segmentStarts;
	void storeSegmentStart(IMLSegment* imlSegment)
	{
		segmentStarts[imlSegment] = getSize();
	}

	bool processAllJumps()
	{
		for (auto jump : jumps)
		{
			auto jumpStart = jump.first;
			auto jumpInfo = jump.second;
			bool success = std::visit(
				[&, this](const auto& jump) {
					setSize(jumpStart);
					sint64 targetAddress = segmentStarts.at(jump.target);
					sint64 addressOffset = targetAddress - jumpStart;
					return handleJump(addressOffset, jump);
				},
				jumpInfo);
			if (!success)
			{
				return false;
			}
		}
		return true;
	}

	bool handleJump(sint64 addressOffset, const UnconditionalJumpInfo& jump)
	{
		// in +/-128MB
		if (-0x8000000 <= addressOffset && addressOffset <= 0x7ffffff)
		{
			b(addressOffset);
			return true;
		}

		cemu_assert_suspicious();

		return false;
	}

	bool handleJump(sint64 addressOffset, const ConditionalRegJumpInfo& jump)
	{
		bool mustBeTrue = jump.mustBeTrue;

		// in +/-32KB
		if (-0x8000 <= addressOffset && addressOffset <= 0x7fff)
		{
			if (mustBeTrue)
				tbnz(jump.regBool, 0, addressOffset);
			else
				tbz(jump.regBool, 0, addressOffset);
			return true;
		}

		// in +/-1MB
		if (-0x100000 <= addressOffset && addressOffset <= 0xfffff)
		{
			if (mustBeTrue)
				cbnz(jump.regBool, addressOffset);
			else
				cbz(jump.regBool, addressOffset);
			return true;
		}

		Label skipJump;
		if (mustBeTrue)
			tbz(jump.regBool, 0, skipJump);
		else
			tbnz(jump.regBool, 0, skipJump);
		addressOffset -= 4;

		// in +/-128MB
		if (-0x8000000 <= addressOffset && addressOffset <= 0x7ffffff)
		{
			b(addressOffset);
			L(skipJump);
			return true;
		}

		cemu_assert_suspicious();

		return false;
	}

	bool handleJump(sint64 addressOffset, const NegativeRegValueJumpInfo& jump)
	{
		// in +/-32KB
		if (-0x8000 <= addressOffset && addressOffset <= 0x7fff)
		{
			tbnz(jump.regValue, 31, addressOffset);
			return true;
		}

		// in +/-1MB
		if (-0x100000 <= addressOffset && addressOffset <= 0xfffff)
		{
			tst(jump.regValue, 0x80000000);
			addressOffset -= 4;
			bne(addressOffset);
			return true;
		}

		Label skipJump;
		tbz(jump.regValue, 31, skipJump);
		addressOffset -= 4;

		// in +/-128MB
		if (-0x8000000 <= addressOffset && addressOffset <= 0x7ffffff)
		{
			b(addressOffset);
			L(skipJump);
			return true;
		}

		cemu_assert_suspicious();

		return false;
	}
};

template<std::derived_from<VRegSc> T>
T fpReg(const IMLReg& imlReg)
{
	cemu_assert_debug(imlReg.GetRegFormat() == IMLRegFormat::F64);
	auto regId = imlReg.GetRegID();
	cemu_assert_debug(regId >= IMLArchAArch64::PHYSREG_FPR_BASE && regId < IMLArchAArch64::PHYSREG_FPR_BASE + IMLArchAArch64::PHYSREG_FPR_COUNT);
	return T(regId - IMLArchAArch64::PHYSREG_FPR_BASE);
}

template<std::derived_from<RReg> T>
T gpReg(const IMLReg& imlReg)
{
	auto regFormat = imlReg.GetRegFormat();
	if (std::is_same_v<T, WReg>)
		cemu_assert_debug(regFormat == IMLRegFormat::I32);
	else if (std::is_same_v<T, XReg>)
		cemu_assert_debug(regFormat == IMLRegFormat::I64);
	else
		cemu_assert_unimplemented();

	auto regId = imlReg.GetRegID();
	// cemu_assert_debug não faz nada em builds release - por isso, se o alocador de
	// registradores do IML algum dia devolver um regId fora da faixa alocável, isso passava
	// batido silenciosamente e T(regId) podia acabar sendo um dos registradores RESERVADOS
	// (25=TEMP_GPR1, 26=TEMP_GPR2/LR, 27=PPC_REC_INSTANCE_REG, 28=MEM_BASE_REG,
	// 29=HCPU_REG), corrompendo um deles sem nenhum aviso - exatamente o tipo de corrupção
	// aleatória vista nos crashes em produção. Essa verificação roda sempre (não só em
	// debug) e força um crash imediato e diagnosticável em vez de uma corrupção silenciosa.
	if (regId < IMLArchAArch64::PHYSREG_GPR_BASE || regId >= IMLArchAArch64::PHYSREG_GPR_BASE + IMLArchAArch64::PHYSREG_GPR_COUNT)
	{
		cemuLog_log(LogType::Force, "[JIT] BUG: gpReg() recebeu regId={} fora da faixa alocavel [{}, {}) - isso corromperia um registrador reservado (25=TEMP_GPR1, 26=TEMP_GPR2/LR, 27=PPC_REC_INSTANCE_REG, 28=MEM_BASE_REG, 29=HCPU_REG)",
			regId, IMLArchAArch64::PHYSREG_GPR_BASE, IMLArchAArch64::PHYSREG_GPR_BASE + IMLArchAArch64::PHYSREG_GPR_COUNT);
		cemuLog_waitForFlush();
		abort();
	}
	return T(regId - IMLArchAArch64::PHYSREG_GPR_BASE);
}

template<std::derived_from<VRegSc> To, std::derived_from<VRegSc> From>
To aliasAs(const From& reg)
{
	return To(reg.getIdx());
}

template<std::derived_from<RReg> To, std::derived_from<RReg> From>
To aliasAs(const From& reg)
{
	return To(reg.getIdx());
}

AArch64GenContext_t::AArch64GenContext_t(Allocator* allocator)
	: CodeGenerator(DEFAULT_MAX_CODE_SIZE, AutoGrow, allocator)
{
}

constexpr uint64 ones(uint32 size)
{
	return (size == 64) ? 0xffffffffffffffff : ((uint64)1 << size) - 1;
}

constexpr bool isAdrImmValidFPR(sint32 imm, uint32 bits)
{
	uint32 times = bits / 8;
	uint32 sh = std::countr_zero(times);
	return (0 <= imm && imm <= 4095 * times) && ((uint64)imm & ones(sh)) == 0;
}

constexpr bool isAdrImmValidGPR(sint32 imm, uint32 bits = 32)
{
	uint32 size = std::countr_zero(bits / 8u);
	sint32 times = 1 << size;
	return (0 <= imm && imm <= 4095 * times) && ((uint64)imm & ones(size)) == 0;
}

constexpr bool isAdrImmRangeValid(sint32 rangeStart, sint32 rangeOffset, sint32 bits, std::invocable<sint32, uint32> auto check)
{
	for (sint32 i = rangeStart; i <= rangeStart + rangeOffset; i += bits / 8)
		if (!check(i, bits))
			return false;
	return true;
}

constexpr bool isAdrImmRangeValidGPR(sint32 rangeStart, sint32 rangeOffset, sint32 bits = 32)
{
	return isAdrImmRangeValid(rangeStart, rangeOffset, bits, isAdrImmValidGPR);
}

constexpr bool isAdrImmRangeValidFpr(sint32 rangeStart, sint32 rangeOffset, sint32 bits)
{
	return isAdrImmRangeValid(rangeStart, rangeOffset, bits, isAdrImmValidFPR);
}

// Verify that all of the offsets for the PPCInterpreter_t members that we use in r_name/name_r have a valid imm value for AdrUimm
static_assert(isAdrImmRangeValidGPR(offsetof(PPCInterpreter_t, gpr), sizeof(uint32) * 31));
static_assert(isAdrImmValidGPR(offsetof(PPCInterpreter_t, spr.LR)));
static_assert(isAdrImmValidGPR(offsetof(PPCInterpreter_t, spr.CTR)));
static_assert(isAdrImmValidGPR(offsetof(PPCInterpreter_t, spr.XER)));
static_assert(isAdrImmRangeValidGPR(offsetof(PPCInterpreter_t, spr.UGQR), sizeof(PPCInterpreter_t::spr.UGQR[0]) * (SPR_UGQR7 - SPR_UGQR0)));
static_assert(isAdrImmRangeValidGPR(offsetof(PPCInterpreter_t, temporaryGPR_reg), sizeof(uint32) * 3));
static_assert(isAdrImmValidGPR(offsetof(PPCInterpreter_t, xer_ca), 8));
static_assert(isAdrImmValidGPR(offsetof(PPCInterpreter_t, xer_so), 8));
static_assert(isAdrImmRangeValidGPR(offsetof(PPCInterpreter_t, cr), PPCREC_NAME_CR_LAST - PPCREC_NAME_CR, 8));
static_assert(isAdrImmValidGPR(offsetof(PPCInterpreter_t, reservedMemAddr)));
static_assert(isAdrImmValidGPR(offsetof(PPCInterpreter_t, reservedMemValue)));
static_assert(isAdrImmRangeValidFpr(offsetof(PPCInterpreter_t, fpr), sizeof(FPR_t) * 63, 64));
static_assert(isAdrImmRangeValidFpr(offsetof(PPCInterpreter_t, temporaryFPR), sizeof(FPR_t) * 7, 128));

void AArch64GenContext_t::r_name(IMLInstruction* imlInstruction)
{
	uint32 name = imlInstruction->op_r_name.name;

	if (imlInstruction->op_r_name.regR.GetBaseFormat() == IMLRegFormat::I64)
	{
		XReg regRXReg = gpReg<XReg>(imlInstruction->op_r_name.regR);
		WReg regR = aliasAs<WReg>(regRXReg);
		if (name >= PPCREC_NAME_R0 && name < PPCREC_NAME_R0 + 32)
		{
			ldr(regR, AdrUimm(HCPU_REG, offsetof(PPCInterpreter_t, gpr) + sizeof(uint32) * (name - PPCREC_NAME_R0)));
		}
		else if (name >= PPCREC_NAME_SPR0 && name < PPCREC_NAME_SPR0 + 999)
		{
			uint32 sprIndex = (name - PPCREC_NAME_SPR0);
			if (sprIndex == SPR_LR)
				ldr(regR, AdrUimm(HCPU_REG, offsetof(PPCInterpreter_t, spr.LR)));
			else if (sprIndex == SPR_CTR)
				ldr(regR, AdrUimm(HCPU_REG, offsetof(PPCInterpreter_t, spr.CTR)));
			else if (sprIndex == SPR_XER)
				ldr(regR, AdrUimm(HCPU_REG, offsetof(PPCInterpreter_t, spr.XER)));
			else if (sprIndex >= SPR_UGQR0 && sprIndex <= SPR_UGQR7)
				ldr(regR, AdrUimm(HCPU_REG, offsetof(PPCInterpreter_t, spr.UGQR) + sizeof(PPCInterpreter_t::spr.UGQR[0]) * (sprIndex - SPR_UGQR0)));
			else
				cemu_assert_suspicious();
		}
		else if (name >= PPCREC_NAME_TEMPORARY && name < PPCREC_NAME_TEMPORARY + 4)
		{
			ldr(regR, AdrUimm(HCPU_REG, offsetof(PPCInterpreter_t, temporaryGPR_reg) + sizeof(uint32) * (name - PPCREC_NAME_TEMPORARY)));
		}
		else if (name == PPCREC_NAME_XER_CA)
		{
			ldrb(regR, AdrUimm(HCPU_REG, offsetof(PPCInterpreter_t, xer_ca)));
		}
		else if (name == PPCREC_NAME_XER_SO)
		{
			ldrb(regR, AdrUimm(HCPU_REG, offsetof(PPCInterpreter_t, xer_so)));
		}
		else if (name >= PPCREC_NAME_CR && name <= PPCREC_NAME_CR_LAST)
		{
			ldrb(regR, AdrUimm(HCPU_REG, offsetof(PPCInterpreter_t, cr) + (name - PPCREC_NAME_CR)));
		}
		else if (name == PPCREC_NAME_CPU_MEMRES_EA)
		{
			ldr(regR, AdrUimm(HCPU_REG, offsetof(PPCInterpreter_t, reservedMemAddr)));
		}
		else if (name == PPCREC_NAME_CPU_MEMRES_VAL)
		{
			ldr(regR, AdrUimm(HCPU_REG, offsetof(PPCInterpreter_t, reservedMemValue)));
		}
		else
		{
			cemu_assert_suspicious();
		}
	}
	else if (imlInstruction->op_r_name.regR.GetBaseFormat() == IMLRegFormat::F64)
	{
		auto imlRegR = imlInstruction->op_r_name.regR;

		if (name >= PPCREC_NAME_FPR_HALF && name < (PPCREC_NAME_FPR_HALF + 64))
		{
			uint32 regIndex = (name - PPCREC_NAME_FPR_HALF) / 2;
			uint32 pairIndex = (name - PPCREC_NAME_FPR_HALF) % 2;
			uint32 offset = offsetof(PPCInterpreter_t, fpr) + sizeof(FPR_t) * regIndex + (pairIndex ? sizeof(double) : 0);
			ldr(fpReg<DReg>(imlRegR), AdrUimm(HCPU_REG, offset));
		}
		else if (name >= PPCREC_NAME_TEMPORARY_FPR0 && name < (PPCREC_NAME_TEMPORARY_FPR0 + 8))
		{
			ldr(fpReg<QReg>(imlRegR), AdrUimm(HCPU_REG, offsetof(PPCInterpreter_t, temporaryFPR) + sizeof(FPR_t) * (name - PPCREC_NAME_TEMPORARY_FPR0)));
		}
		else
		{
			cemu_assert_suspicious();
		}
	}
	else
	{
		cemu_assert_suspicious();
	}
}

void AArch64GenContext_t::name_r(IMLInstruction* imlInstruction)
{
	uint32 name = imlInstruction->op_r_name.name;

	if (imlInstruction->op_r_name.regR.GetBaseFormat() == IMLRegFormat::I64)
	{
		XReg regRXReg = gpReg<XReg>(imlInstruction->op_r_name.regR);
		WReg regR = aliasAs<WReg>(regRXReg);
		if (name >= PPCREC_NAME_R0 && name < PPCREC_NAME_R0 + 32)
		{
			str(regR, AdrUimm(HCPU_REG, offsetof(PPCInterpreter_t, gpr) + sizeof(uint32) * (name - PPCREC_NAME_R0)));
		}
		else if (name >= PPCREC_NAME_SPR0 && name < PPCREC_NAME_SPR0 + 999)
		{
			uint32 sprIndex = (name - PPCREC_NAME_SPR0);
			if (sprIndex == SPR_LR)
				str(regR, AdrUimm(HCPU_REG, offsetof(PPCInterpreter_t, spr.LR)));
			else if (sprIndex == SPR_CTR)
				str(regR, AdrUimm(HCPU_REG, offsetof(PPCInterpreter_t, spr.CTR)));
			else if (sprIndex == SPR_XER)
				str(regR, AdrUimm(HCPU_REG, offsetof(PPCInterpreter_t, spr.XER)));
			else if (sprIndex >= SPR_UGQR0 && sprIndex <= SPR_UGQR7)
				str(regR, AdrUimm(HCPU_REG, offsetof(PPCInterpreter_t, spr.UGQR) + sizeof(PPCInterpreter_t::spr.UGQR[0]) * (sprIndex - SPR_UGQR0)));
			else
				cemu_assert_suspicious();
		}
		else if (name >= PPCREC_NAME_TEMPORARY && name < PPCREC_NAME_TEMPORARY + 4)
		{
			str(regR, AdrUimm(HCPU_REG, offsetof(PPCInterpreter_t, temporaryGPR_reg) + sizeof(uint32) * (name - PPCREC_NAME_TEMPORARY)));
		}
		else if (name == PPCREC_NAME_XER_CA)
		{
			strb(regR, AdrUimm(HCPU_REG, offsetof(PPCInterpreter_t, xer_ca)));
		}
		else if (name == PPCREC_NAME_XER_SO)
		{
			strb(regR, AdrUimm(HCPU_REG, offsetof(PPCInterpreter_t, xer_so)));
		}
		else if (name >= PPCREC_NAME_CR && name <= PPCREC_NAME_CR_LAST)
		{
			strb(regR, AdrUimm(HCPU_REG, offsetof(PPCInterpreter_t, cr) + (name - PPCREC_NAME_CR)));
		}
		else if (name == PPCREC_NAME_CPU_MEMRES_EA)
		{
			str(regR, AdrUimm(HCPU_REG, offsetof(PPCInterpreter_t, reservedMemAddr)));
		}
		else if (name == PPCREC_NAME_CPU_MEMRES_VAL)
		{
			str(regR, AdrUimm(HCPU_REG, offsetof(PPCInterpreter_t, reservedMemValue)));
		}
		else
		{
			cemu_assert_suspicious();
		}
	}
	else if (imlInstruction->op_r_name.regR.GetBaseFormat() == IMLRegFormat::F64)
	{
		auto imlRegR = imlInstruction->op_r_name.regR;
		if (name >= PPCREC_NAME_FPR_HALF && name < (PPCREC_NAME_FPR_HALF + 64))
		{
			uint32 regIndex = (name - PPCREC_NAME_FPR_HALF) / 2;
			uint32 pairIndex = (name - PPCREC_NAME_FPR_HALF) % 2;
			sint32 offset = offsetof(PPCInterpreter_t, fpr) + sizeof(FPR_t) * regIndex + pairIndex * sizeof(double);
			str(fpReg<DReg>(imlRegR), AdrUimm(HCPU_REG, offset));
		}
		else if (name >= PPCREC_NAME_TEMPORARY_FPR0 && name < (PPCREC_NAME_TEMPORARY_FPR0 + 8))
		{
			str(fpReg<QReg>(imlRegR), AdrUimm(HCPU_REG, offsetof(PPCInterpreter_t, temporaryFPR) + sizeof(FPR_t) * (name - PPCREC_NAME_TEMPORARY_FPR0)));
		}
		else
		{
			cemu_assert_suspicious();
		}
	}
	else
	{
		cemu_assert_suspicious();
	}
}

bool AArch64GenContext_t::r_r(IMLInstruction* imlInstruction)
{
	WReg regR = gpReg<WReg>(imlInstruction->op_r_r.regR);
	WReg regA = gpReg<WReg>(imlInstruction->op_r_r.regA);

	if (imlInstruction->operation == PPCREC_IML_OP_ASSIGN)
	{
		mov(regR, regA);
	}
	else if (imlInstruction->operation == PPCREC_IML_OP_ENDIAN_SWAP)
	{
		rev(regR, regA);
	}
	else if (imlInstruction->operation == PPCREC_IML_OP_ASSIGN_S8_TO_S32)
	{
		sxtb(regR, regA);
	}
	else if (imlInstruction->operation == PPCREC_IML_OP_ASSIGN_S16_TO_S32)
	{
		sxth(regR, regA);
	}
	else if (imlInstruction->operation == PPCREC_IML_OP_NOT)
	{
		mvn(regR, regA);
	}
	else if (imlInstruction->operation == PPCREC_IML_OP_NEG)
	{
		neg(regR, regA);
	}
	else if (imlInstruction->operation == PPCREC_IML_OP_CNTLZW)
	{
		clz(regR, regA);
	}
	else
	{
		cemuLog_log(LogType::Recompiler, "PPCRecompilerAArch64Gen_imlInstruction_r_r(): Unsupported operation {:x}", imlInstruction->operation);
		return false;
	}
	return true;
}

bool AArch64GenContext_t::r_s32(IMLInstruction* imlInstruction)
{
	sint32 imm32 = imlInstruction->op_r_immS32.immS32;
	WReg reg = gpReg<WReg>(imlInstruction->op_r_immS32.regR);

	if (imlInstruction->operation == PPCREC_IML_OP_ASSIGN)
	{
		mov(reg, imm32);
	}
	else if (imlInstruction->operation == PPCREC_IML_OP_LEFT_ROTATE)
	{
		ror(reg, reg, 32 - (imm32 & 0x1f));
	}
	else
	{
		cemuLog_log(LogType::Recompiler, "PPCRecompilerAArch64Gen_imlInstruction_r_s32(): Unsupported operation {:x}", imlInstruction->operation);
		return false;
	}
	return true;
}

bool AArch64GenContext_t::r_r_s32(IMLInstruction* imlInstruction)
{
	WReg regR = gpReg<WReg>(imlInstruction->op_r_r_s32.regR);
	WReg regA = gpReg<WReg>(imlInstruction->op_r_r_s32.regA);
	sint32 immS32 = imlInstruction->op_r_r_s32.immS32;

	if (imlInstruction->operation == PPCREC_IML_OP_ADD)
	{
		add_imm(regR, regA, immS32, TEMP_GPR1.WReg);
	}
	else if (imlInstruction->operation == PPCREC_IML_OP_SUB)
	{
		sub_imm(regR, regA, immS32, TEMP_GPR1.WReg);
	}
	else if (imlInstruction->operation == PPCREC_IML_OP_AND)
	{
		mov(TEMP_GPR1.WReg, immS32);
		and_(regR, regA, TEMP_GPR1.WReg);
	}
	else if (imlInstruction->operation == PPCREC_IML_OP_OR)
	{
		mov(TEMP_GPR1.WReg, immS32);
		orr(regR, regA, TEMP_GPR1.WReg);
	}
	else if (imlInstruction->operation == PPCREC_IML_OP_XOR)
	{
		mov(TEMP_GPR1.WReg, immS32);
		eor(regR, regA, TEMP_GPR1.WReg);
	}
	else if (imlInstruction->operation == PPCREC_IML_OP_MULTIPLY_SIGNED)
	{
		mov(TEMP_GPR1.WReg, immS32);
		mul(regR, regA, TEMP_GPR1.WReg);
	}
	else if (imlInstruction->operation == PPCREC_IML_OP_LEFT_SHIFT)
	{
		lsl(regR, regA, (uint32)immS32 & 0x1f);
	}
	else if (imlInstruction->operation == PPCREC_IML_OP_RIGHT_SHIFT_U)
	{
		lsr(regR, regA, (uint32)immS32 & 0x1f);
	}
	else if (imlInstruction->operation == PPCREC_IML_OP_RIGHT_SHIFT_S)
	{
		asr(regR, regA, (uint32)immS32 & 0x1f);
	}
	else
	{
		cemuLog_log(LogType::Recompiler, "PPCRecompilerAArch64Gen_imlInstruction_r_r_s32(): Unsupported operation {:x}", imlInstruction->operation);
		cemu_assert_suspicious();
		return false;
	}
	return true;
}

bool AArch64GenContext_t::r_r_s32_carry(IMLInstruction* imlInstruction)
{
	WReg regR = gpReg<WReg>(imlInstruction->op_r_r_s32_carry.regR);
	WReg regA = gpReg<WReg>(imlInstruction->op_r_r_s32_carry.regA);
	WReg regCarry = gpReg<WReg>(imlInstruction->op_r_r_s32_carry.regCarry);

	sint32 immS32 = imlInstruction->op_r_r_s32_carry.immS32;
	if (imlInstruction->operation == PPCREC_IML_OP_ADD)
	{
		adds_imm(regR, regA, immS32, TEMP_GPR1.WReg);
		cset(regCarry, Cond::CS);
	}
	else if (imlInstruction->operation == PPCREC_IML_OP_ADD_WITH_CARRY)
	{
		mov(TEMP_GPR1.WReg, immS32);
		cmp(regCarry, 1);
		adcs(regR, regA, TEMP_GPR1.WReg);
		cset(regCarry, Cond::CS);
	}
	else
	{
		cemu_assert_suspicious();
		return false;
	}

	return true;
}

bool AArch64GenContext_t::r_r_r(IMLInstruction* imlInstruction)
{
	WReg regResult = gpReg<WReg>(imlInstruction->op_r_r_r.regR);
	XReg reg64Result = aliasAs<XReg>(regResult);
	WReg regOperand1 = gpReg<WReg>(imlInstruction->op_r_r_r.regA);
	WReg regOperand2 = gpReg<WReg>(imlInstruction->op_r_r_r.regB);

	if (imlInstruction->operation == PPCREC_IML_OP_ADD)
	{
		add(regResult, regOperand1, regOperand2);
	}
	else if (imlInstruction->operation == PPCREC_IML_OP_SUB)
	{
		sub(regResult, regOperand1, regOperand2);
	}
	else if (imlInstruction->operation == PPCREC_IML_OP_OR)
	{
		orr(regResult, regOperand1, regOperand2);
	}
	else if (imlInstruction->operation == PPCREC_IML_OP_AND)
	{
		and_(regResult, regOperand1, regOperand2);
	}
	else if (imlInstruction->operation == PPCREC_IML_OP_XOR)
	{
		eor(regResult, regOperand1, regOperand2);
	}
	else if (imlInstruction->operation == PPCREC_IML_OP_MULTIPLY_SIGNED)
	{
		mul(regResult, regOperand1, regOperand2);
	}
	else if (imlInstruction->operation == PPCREC_IML_OP_SLW)
	{
		tst(regOperand2, 32);
		lsl(regResult, regOperand1, regOperand2);
		csel(regResult, regResult, wzr, Cond::EQ);
	}
	else if (imlInstruction->operation == PPCREC_IML_OP_SRW)
	{
		tst(regOperand2, 32);
		lsr(regResult, regOperand1, regOperand2);
		csel(regResult, regResult, wzr, Cond::EQ);
	}
	else if (imlInstruction->operation == PPCREC_IML_OP_LEFT_ROTATE)
	{
		neg(TEMP_GPR1.WReg, regOperand2);
		ror(regResult, regOperand1, TEMP_GPR1.WReg);
	}
	else if (imlInstruction->operation == PPCREC_IML_OP_RIGHT_SHIFT_S)
	{
		asr(regResult, regOperand1, regOperand2);
	}
	else if (imlInstruction->operation == PPCREC_IML_OP_RIGHT_SHIFT_U)
	{
		lsr(regResult, regOperand1, regOperand2);
	}
	else if (imlInstruction->operation == PPCREC_IML_OP_LEFT_SHIFT)
	{
		lsl(regResult, regOperand1, regOperand2);
	}
	else if (imlInstruction->operation == PPCREC_IML_OP_DIVIDE_SIGNED)
	{
		sdiv(regResult, regOperand1, regOperand2);
	}
	else if (imlInstruction->operation == PPCREC_IML_OP_DIVIDE_UNSIGNED)
	{
		udiv(regResult, regOperand1, regOperand2);
	}
	else if (imlInstruction->operation == PPCREC_IML_OP_MULTIPLY_HIGH_SIGNED)
	{
		smull(reg64Result, regOperand1, regOperand2);
		lsr(reg64Result, reg64Result, 32);
	}
	else if (imlInstruction->operation == PPCREC_IML_OP_MULTIPLY_HIGH_UNSIGNED)
	{
		umull(reg64Result, regOperand1, regOperand2);
		lsr(reg64Result, reg64Result, 32);
	}
	else
	{
		cemuLog_log(LogType::Recompiler, "PPCRecompilerAArch64Gen_imlInstruction_r_r_r(): Unsupported operation {:x}", imlInstruction->operation);
		return false;
	}
	return true;
}

bool AArch64GenContext_t::r_r_r_carry(IMLInstruction* imlInstruction)
{
	WReg regR = gpReg<WReg>(imlInstruction->op_r_r_r_carry.regR);
	WReg regA = gpReg<WReg>(imlInstruction->op_r_r_r_carry.regA);
	WReg regB = gpReg<WReg>(imlInstruction->op_r_r_r_carry.regB);
	WReg regCarry = gpReg<WReg>(imlInstruction->op_r_r_r_carry.regCarry);

	if (imlInstruction->operation == PPCREC_IML_OP_ADD)
	{
		adds(regR, regA, regB);
		cset(regCarry, Cond::CS);
	}
	else if (imlInstruction->operation == PPCREC_IML_OP_ADD_WITH_CARRY)
	{
		cmp(regCarry, 1);
		adcs(regR, regA, regB);
		cset(regCarry, Cond::CS);
	}
	else
	{
		cemu_assert_suspicious();
		return false;
	}

	return true;
}

Cond ImlCondToArm64Cond(IMLCondition condition)
{
	switch (condition)
	{
	case IMLCondition::EQ:
		return Cond::EQ;
	case IMLCondition::NEQ:
		return Cond::NE;
	case IMLCondition::UNSIGNED_GT:
		return Cond::HI;
	case IMLCondition::UNSIGNED_LT:
		return Cond::LO;
	case IMLCondition::SIGNED_GT:
		return Cond::GT;
	case IMLCondition::SIGNED_LT:
		return Cond::LT;
	default:
	{
		cemu_assert_suspicious();
		return Cond::EQ;
	}
	}
}

void AArch64GenContext_t::compare(IMLInstruction* imlInstruction)
{
	WReg regR = gpReg<WReg>(imlInstruction->op_compare.regR);
	WReg regA = gpReg<WReg>(imlInstruction->op_compare.regA);
	WReg regB = gpReg<WReg>(imlInstruction->op_compare.regB);
	Cond cond = ImlCondToArm64Cond(imlInstruction->op_compare.cond);
	cmp(regA, regB);
	cset(regR, cond);
}

void AArch64GenContext_t::compare_s32(IMLInstruction* imlInstruction)
{
	WReg regR = gpReg<WReg>(imlInstruction->op_compare.regR);
	WReg regA = gpReg<WReg>(imlInstruction->op_compare.regA);
	sint32 imm = imlInstruction->op_compare_s32.immS32;
	auto cond = ImlCondToArm64Cond(imlInstruction->op_compare.cond);
	cmp_imm(regA, imm, TEMP_GPR1.WReg);
	cset(regR, cond);
}

void AArch64GenContext_t::cjump(IMLInstruction* imlInstruction, IMLSegment* imlSegment)
{
	auto regBool = gpReg<WReg>(imlInstruction->op_conditional_jump.registerBool);
	prepareJump(ConditionalRegJumpInfo{
		.target = imlSegment->nextSegmentBranchTaken,
		.regBool = regBool,
		.mustBeTrue = imlInstruction->op_conditional_jump.mustBeTrue,
	});
}

void AArch64GenContext_t::jump(IMLSegment* imlSegment)
{
	prepareJump(UnconditionalJumpInfo{.target = imlSegment->nextSegmentBranchTaken});
}

void AArch64GenContext_t::conditionalJumpCycleCheck(IMLSegment* imlSegment)
{
	ldr(TEMP_GPR1.WReg, AdrUimm(HCPU_REG, offsetof(PPCInterpreter_t, remainingCycles)));
	prepareJump(NegativeRegValueJumpInfo{
		.target = imlSegment->nextSegmentBranchTaken,
		.regValue = TEMP_GPR1.WReg,
	});
}

// buffer circular de diagnóstico: guarda as últimas chamadas HLE (funções de sistema/
// gráficas do Wii U, como GX2DrawEx) de todas as threads, com o NOME legível da função -
// sem I/O no caminho quente, só grava num array na memória. Só é lido pelo handler de
// crash (PPCRecompiler_dumpRecentHleCallHistoryToCrashLog), útil pra confirmar se o
// crash está relacionado a uma função gráfica específica.
namespace
{
	struct HleCallLogEntry
	{
		uint32 ppcAddr;
		uint32 hleFuncId;
		uint32 threadId;
	};
	constexpr size_t kHleCallRingBufferSize = 64;
	HleCallLogEntry s_hleCallRingBuffer[kHleCallRingBufferSize]{};
	std::atomic<uint64> s_hleCallRingCounter{0};
} // namespace

void PPCRecompiler_dumpRecentHleCallHistoryToCrashLog()
{
	uint64 totalCount = s_hleCallRingCounter.load();
	uint64 numEntries = totalCount < kHleCallRingBufferSize ? totalCount : kHleCallRingBufferSize;
	CrashLog_WriteLine(fmt::format("Recent HLE calls (most recent last, {} of {} total):", numEntries, totalCount));
	uint64 startIdx = totalCount < kHleCallRingBufferSize ? 0 : totalCount - kHleCallRingBufferSize;
	for (uint64 i = startIdx; i < totalCount; i++)
	{
		const auto& entry = s_hleCallRingBuffer[i % kHleCallRingBufferSize];
		const std::string& name = PPCInterpreter_getHLEName(entry.hleFuncId);
		CrashLog_WriteLine(fmt::format("  [{}] tid={} ppcAddr=0x{:08x} hleFuncId={} name=\"{}\"", i, entry.threadId, entry.ppcAddr, entry.hleFuncId, name.empty() ? "?" : name));
	}
}

void* PPCRecompiler_virtualHLE(PPCInterpreter_t* ppcInterpreter, uint32 hleFuncId)
{
	if (hleFuncId != 0xFFD0)
	{
		uint64 idx = s_hleCallRingCounter.fetch_add(1) % kHleCallRingBufferSize;
#if BOOST_OS_WINDOWS
		uint32 threadId = (uint32)GetCurrentThreadId();
#else
		uint32 threadId = (uint32)gettid();
#endif
		s_hleCallRingBuffer[idx] = { ppcInterpreter->instructionPointer, hleFuncId, threadId };
	}
	void* prevRSPTemp = ppcInterpreter->rspTemp;
	if (hleFuncId == 0xFFD0)
	{
		ppcInterpreter->remainingCycles -= 500; // let subtract about 500 cycles for each HLE call
		ppcInterpreter->gpr[3] = 0;
		PPCInterpreter_nextInstruction(ppcInterpreter);
		return ppcInterpreter;
	}
	else
	{
		auto hleCall = PPCInterpreter_getHLECall(hleFuncId);
		cemu_assert(hleCall != nullptr);
		hleCall(ppcInterpreter);
	}
	ppcInterpreter->rspTemp = prevRSPTemp;
	// IMPORTANTE: o código JIT gerado (macro() logo abaixo, case PPCREC_IML_MACRO_HLE) pega
	// o valor de retorno desta função e recoloca ele direto no registrador dedicado HCPU_REG.
	// Por isso é essencial devolver exatamente o parâmetro recebido (que está sempre correto),
	// e NUNCA reconsultar PPCInterpreter_getCurrentInstance() - essa thread_local não é
	// mantida atualizada durante a execução de código recompilado no Android (o JIT usa o
	// registrador dedicado em vez dela), então podia devolver um ponteiro desatualizado/nulo
	// aqui, corrompendo HCPU_REG e derrubando o emulador logo na primeira chamada HLE.
	return ppcInterpreter;
}

// Diagnóstico: chamado pelo "canário" inserido no início de macro() sempre que x27
// (PPC_REC_INSTANCE_REG) ou x28 (MEM_BASE_REG) não batem com os valores globais
// esperados (que NUNCA mudam durante toda a sessão do emulador). Isso não corrige nada,
// só avisa - a execução continua com o valor (ainda errado) que já estava lá, então o
// crash de sempre ainda deve acontecer logo em seguida, mas agora com uma linha no log
// mostrando exatamente entre quais instruções a corrupção já tinha acontecido.
void PPCRecompiler_reportRegisterCorruption(const char* checkpointTag, uint32 whichReg, uint64 actualValue, uint64 expectedValue)
{
	static std::atomic<int> s_count{0};
	if (s_count.fetch_add(1) < 20)
	{
		cemuLog_log(LogType::Force, "[JIT] CANARIO ({}): x{} = 0x{:x} mas deveria ser 0x{:x} (registrador reservado corrompido)", checkpointTag, whichReg, actualValue, expectedValue);
		cemuLog_waitForFlush();
	}
}

// emite código que compara x27/x28 contra os globais que eles deveriam sempre igualar,
// e reporta (sem corrigir, sem parar a execução) se estiverem errados. Ver comentário em
// PPCRecompiler_reportRegisterCorruption. Chamado tanto no início de cada macro() quanto
// logo depois de qualquer chamada pra código C++ (HLE, call_imm), pra restringir ao
// máximo ONDE exatamente a corrupção acontece.
void AArch64GenContext_t::emitRegisterCanaryCheck(const char* checkpointTag)
{
	{
		mov(TEMP_GPR1.XReg, (uint64)ppcRecompilerInstanceData);
		cmp(PPC_REC_INSTANCE_REG, TEMP_GPR1.XReg);
		Label x27ok;
		beq(x27ok);
		str(x30, AdrPreImm(sp, -16));
		mov(x0, (uint64)checkpointTag);
		mov(x1, 27);
		mov(x2, PPC_REC_INSTANCE_REG);
		mov(x3, TEMP_GPR1.XReg);
		mov(TEMP_GPR2.XReg, (uint64)PPCRecompiler_reportRegisterCorruption);
		blr(TEMP_GPR2.XReg);
		ldr(x30, AdrPostImm(sp, 16));
		L(x27ok);
	}
	{
		mov(TEMP_GPR1.XReg, (uint64)memory_base);
		cmp(MEM_BASE_REG, TEMP_GPR1.XReg);
		Label x28ok;
		beq(x28ok);
		str(x30, AdrPreImm(sp, -16));
		mov(x0, (uint64)checkpointTag);
		mov(x1, 28);
		mov(x2, MEM_BASE_REG);
		mov(x3, TEMP_GPR1.XReg);
		mov(TEMP_GPR2.XReg, (uint64)PPCRecompiler_reportRegisterCorruption);
		blr(TEMP_GPR2.XReg);
		ldr(x30, AdrPostImm(sp, 16));
		L(x28ok);
	}
}

bool AArch64GenContext_t::macro(IMLInstruction* imlInstruction)
{
	// canário: detecta o mais cedo possível se x27/x28 saíram dos valores globais
	// esperados. Inserido no início de macro() porque essa função roda com bastante
	// frequência (toda vez que o código recompilado faz um branch, chamada, HLE, etc)
	// mas sem o overhead de rodar a cada instrução aritmética simples.
	emitRegisterCanaryCheck("macro_entry");
	if (imlInstruction->operation == PPCREC_IML_MACRO_B_TO_REG)
	{
		WReg branchDstReg = gpReg<WReg>(imlInstruction->op_macro.paramReg);

		mov(TEMP_GPR1.WReg, offsetof(PPCRecompilerInstanceData_t, ppcRecompilerDirectJumpTable));
		add(TEMP_GPR1.WReg, TEMP_GPR1.WReg, branchDstReg, ShMod::LSL, 1);
		ldr(TEMP_GPR1.XReg, AdrExt(PPC_REC_INSTANCE_REG, TEMP_GPR1.WReg, ExtMod::UXTW));
		mov(LR.WReg, branchDstReg);
		br(TEMP_GPR1.XReg);
		return true;
	}
	else if (imlInstruction->operation == PPCREC_IML_MACRO_BL)
	{
		uint32 newLR = imlInstruction->op_macro.param + 4;

		mov(TEMP_GPR1.WReg, newLR);
		str(TEMP_GPR1.WReg, AdrUimm(HCPU_REG, offsetof(PPCInterpreter_t, spr.LR)));

		uint32 newIP = imlInstruction->op_macro.param2;
		uint64 lookupOffset = (uint64)offsetof(PPCRecompilerInstanceData_t, ppcRecompilerDirectJumpTable) + (uint64)newIP * 2ULL;
		mov(TEMP_GPR1.XReg, lookupOffset);
		ldr(TEMP_GPR1.XReg, AdrReg(PPC_REC_INSTANCE_REG, TEMP_GPR1.XReg));
		mov(LR.WReg, newIP);
		br(TEMP_GPR1.XReg);
		return true;
	}
	else if (imlInstruction->operation == PPCREC_IML_MACRO_B_FAR)
	{
		uint32 newIP = imlInstruction->op_macro.param2;
		uint64 lookupOffset = (uint64)offsetof(PPCRecompilerInstanceData_t, ppcRecompilerDirectJumpTable) + (uint64)newIP * 2ULL;
		mov(TEMP_GPR1.XReg, lookupOffset);
		ldr(TEMP_GPR1.XReg, AdrReg(PPC_REC_INSTANCE_REG, TEMP_GPR1.XReg));
		mov(LR.WReg, newIP);
		br(TEMP_GPR1.XReg);
		return true;
	}
	else if (imlInstruction->operation == PPCREC_IML_MACRO_LEAVE)
	{
		uint32 currentInstructionAddress = imlInstruction->op_macro.param;
		mov(TEMP_GPR1.XReg, (uint64)offsetof(PPCRecompilerInstanceData_t, ppcRecompilerDirectJumpTable)); // newIP = 0 special value for recompiler exit
		ldr(TEMP_GPR1.XReg, AdrReg(PPC_REC_INSTANCE_REG, TEMP_GPR1.XReg));
		mov(LR.WReg, currentInstructionAddress);
		br(TEMP_GPR1.XReg);
		return true;
	}
	else if (imlInstruction->operation == PPCREC_IML_MACRO_DEBUGBREAK)
	{
		brk(0xf000);
		return true;
	}
	else if (imlInstruction->operation == PPCREC_IML_MACRO_COUNT_CYCLES)
	{
		uint32 cycleCount = imlInstruction->op_macro.param;
		AdrUimm adrCycles = AdrUimm(HCPU_REG, offsetof(PPCInterpreter_t, remainingCycles));
		ldr(TEMP_GPR1.WReg, adrCycles);
		sub_imm(TEMP_GPR1.WReg, TEMP_GPR1.WReg, cycleCount, TEMP_GPR2.WReg);
		str(TEMP_GPR1.WReg, adrCycles);
		return true;
	}
	else if (imlInstruction->operation == PPCREC_IML_MACRO_HLE)
	{
		uint32 ppcAddress = imlInstruction->op_macro.param;
		uint32 funcId = imlInstruction->op_macro.param2;
		Label cyclesLeftLabel;

		// update instruction pointer
		mov(TEMP_GPR1.WReg, ppcAddress);
		str(TEMP_GPR1.WReg, AdrUimm(HCPU_REG, offsetof(PPCInterpreter_t, instructionPointer)));
		// set parameters
		// Protege os 3 registradores reservados do recompilador (x27/x28/x29) contra
		// qualquer coisa que a função C++ chamada abaixo (ou algo que ela chame
		// internamente) possa fazer com eles - por exemplo, o compilador pode usar
		// "outline atomics" (chamadas geradas automaticamente pra operações atômicas)
		// que usam uma convenção de registradores não-padrão, livre pra usar qualquer
		// registrador de propósito geral sem saber que esses 3 são reservados aqui.
		// Salvando e restaurando manualmente, ficamos protegidos independente da causa.
		stp(x27, x28, AdrPreImm(sp, -16));
		stp(x29, x30, AdrPreImm(sp, -16));

		mov(x0, HCPU_REG);
		mov(w1, funcId);
		// call HLE function
		// NOTE: must use movAbsFixed() here, not mov(reg, imm64) - see its comment for why
		// (this address is only valid for this process run; the AOT disk cache needs a
		// fixed-size, always-patchable instruction sequence to safely reuse this function's
		// code in a later run).
		movAbsFixed(TEMP_GPR1.XReg, (uint64)PPCRecompiler_virtualHLE, PPCRecRelocType::HLE_FUNCTION);
		blr(TEMP_GPR1.XReg);

		ldp(x29, x30, AdrPostImm(sp, 16));
		ldp(x27, x28, AdrPostImm(sp, 16));
		emitRegisterCanaryCheck("after_hle_call");

		// check if cycles where decreased beyond zero, if yes -> leave recompiler
		ldr(TEMP_GPR1.WReg, AdrUimm(HCPU_REG, offsetof(PPCInterpreter_t, remainingCycles)));
		tbz(TEMP_GPR1.WReg, 31, cyclesLeftLabel); // check if negative

		mov(TEMP_GPR1.XReg, offsetof(PPCRecompilerInstanceData_t, ppcRecompilerDirectJumpTable));
		ldr(TEMP_GPR1.XReg, AdrReg(PPC_REC_INSTANCE_REG, TEMP_GPR1.XReg));
		ldr(LR.WReg, AdrUimm(HCPU_REG, offsetof(PPCInterpreter_t, instructionPointer)));
		// branch to recompiler exit
		br(TEMP_GPR1.XReg);

		L(cyclesLeftLabel);
		// check if instruction pointer was changed
		// assign new instruction pointer to LR.WReg
		ldr(LR.WReg, AdrUimm(HCPU_REG, offsetof(PPCInterpreter_t, instructionPointer)));
		mov(TEMP_GPR1.XReg, offsetof(PPCRecompilerInstanceData_t, ppcRecompilerDirectJumpTable));
		add(TEMP_GPR1.XReg, TEMP_GPR1.XReg, LR.XReg, ShMod::LSL, 1);
		ldr(TEMP_GPR1.XReg, AdrReg(PPC_REC_INSTANCE_REG, TEMP_GPR1.XReg));
		// branch to [ppcRecompilerDirectJumpTable + PPCInterpreter_t::instructionPointer * 2]
		br(TEMP_GPR1.XReg);
		return true;
	}
	else
	{
		cemuLog_log(LogType::Recompiler, "Unknown recompiler macro operation %d\n", imlInstruction->operation);
		cemu_assert_suspicious();
	}
	return false;
}

bool AArch64GenContext_t::load(IMLInstruction* imlInstruction, bool indexed)
{
	cemu_assert_debug(imlInstruction->op_storeLoad.registerData.GetRegFormat() == IMLRegFormat::I32);
	cemu_assert_debug(imlInstruction->op_storeLoad.registerMem.GetRegFormat() == IMLRegFormat::I32);
	if (indexed)
		cemu_assert_debug(imlInstruction->op_storeLoad.registerMem2.GetRegFormat() == IMLRegFormat::I32);

	sint32 memOffset = imlInstruction->op_storeLoad.immS32;
	bool signExtend = imlInstruction->op_storeLoad.flags2.signExtend;
	bool switchEndian = imlInstruction->op_storeLoad.flags2.swapEndian;
	WReg memReg = gpReg<WReg>(imlInstruction->op_storeLoad.registerMem);
	WReg dataReg = gpReg<WReg>(imlInstruction->op_storeLoad.registerData);

	add_imm(TEMP_GPR1.WReg, memReg, memOffset, TEMP_GPR1.WReg);
	if (indexed)
		add(TEMP_GPR1.WReg, TEMP_GPR1.WReg, gpReg<WReg>(imlInstruction->op_storeLoad.registerMem2));

	auto adr = AdrExt(MEM_BASE_REG, TEMP_GPR1.WReg, ExtMod::UXTW);
	if (imlInstruction->op_storeLoad.copyWidth == 32)
	{
		ldr(dataReg, adr);
		if (switchEndian)
			rev(dataReg, dataReg);
	}
	else if (imlInstruction->op_storeLoad.copyWidth == 16)
	{
		if (switchEndian)
		{
			ldrh(dataReg, adr);
			rev(dataReg, dataReg);
			if (signExtend)
				asr(dataReg, dataReg, 16);
			else
				lsr(dataReg, dataReg, 16);
		}
		else
		{
			if (signExtend)
				ldrsh(dataReg, adr);
			else
				ldrh(dataReg, adr);
		}
	}
	else if (imlInstruction->op_storeLoad.copyWidth == 8)
	{
		if (signExtend)
			ldrsb(dataReg, adr);
		else
			ldrb(dataReg, adr);
	}
	else
	{
		return false;
	}
	return true;
}

bool AArch64GenContext_t::store(IMLInstruction* imlInstruction, bool indexed)
{
	cemu_assert_debug(imlInstruction->op_storeLoad.registerData.GetRegFormat() == IMLRegFormat::I32);
	cemu_assert_debug(imlInstruction->op_storeLoad.registerMem.GetRegFormat() == IMLRegFormat::I32);
	if (indexed)
		cemu_assert_debug(imlInstruction->op_storeLoad.registerMem2.GetRegFormat() == IMLRegFormat::I32);

	WReg dataReg = gpReg<WReg>(imlInstruction->op_storeLoad.registerData);
	WReg memReg = gpReg<WReg>(imlInstruction->op_storeLoad.registerMem);
	sint32 memOffset = imlInstruction->op_storeLoad.immS32;
	bool swapEndian = imlInstruction->op_storeLoad.flags2.swapEndian;

	add_imm(TEMP_GPR1.WReg, memReg, memOffset, TEMP_GPR1.WReg);
	if (indexed)
		add(TEMP_GPR1.WReg, TEMP_GPR1.WReg, gpReg<WReg>(imlInstruction->op_storeLoad.registerMem2));
	AdrExt adr = AdrExt(MEM_BASE_REG, TEMP_GPR1.WReg, ExtMod::UXTW);
	if (imlInstruction->op_storeLoad.copyWidth == 32)
	{
		if (swapEndian)
		{
			rev(TEMP_GPR2.WReg, dataReg);
			str(TEMP_GPR2.WReg, adr);
		}
		else
		{
			str(dataReg, adr);
		}
	}
	else if (imlInstruction->op_storeLoad.copyWidth == 16)
	{
		if (swapEndian)
		{
			rev(TEMP_GPR2.WReg, dataReg);
			lsr(TEMP_GPR2.WReg, TEMP_GPR2.WReg, 16);
			strh(TEMP_GPR2.WReg, adr);
		}
		else
		{
			strh(dataReg, adr);
		}
	}
	else if (imlInstruction->op_storeLoad.copyWidth == 8)
	{
		strb(dataReg, adr);
	}
	else
	{
		return false;
	}
	return true;
}

void AArch64GenContext_t::atomic_cmp_store(IMLInstruction* imlInstruction)
{
	WReg outReg = gpReg<WReg>(imlInstruction->op_atomic_compare_store.regBoolOut);
	WReg eaReg = gpReg<WReg>(imlInstruction->op_atomic_compare_store.regEA);
	WReg valReg = gpReg<WReg>(imlInstruction->op_atomic_compare_store.regWriteValue);
	WReg cmpValReg = gpReg<WReg>(imlInstruction->op_atomic_compare_store.regCompareValue);

	if (s_cpu.isAtomicSupported())
	{
		mov(TEMP_GPR2.WReg, cmpValReg);
		add(TEMP_GPR1.XReg, MEM_BASE_REG, eaReg, ExtMod::UXTW);
		casal(TEMP_GPR2.WReg, valReg, AdrNoOfs(TEMP_GPR1.XReg));
		cmp(TEMP_GPR2.WReg, cmpValReg);
		cset(outReg, Cond::EQ);
	}
	else
	{
		Label notEqual;
		Label storeFailed;

		add(TEMP_GPR1.XReg, MEM_BASE_REG, eaReg, ExtMod::UXTW);
		L(storeFailed);
		ldaxr(TEMP_GPR2.WReg, AdrNoOfs(TEMP_GPR1.XReg));
		cmp(TEMP_GPR2.WReg, cmpValReg);
		bne(notEqual);
		stlxr(TEMP_GPR2.WReg, valReg, AdrNoOfs(TEMP_GPR1.XReg));
		cbnz(TEMP_GPR2.WReg, storeFailed);

		L(notEqual);
		cset(outReg, Cond::EQ);
	}
}

bool AArch64GenContext_t::fpr_load(IMLInstruction* imlInstruction, bool indexed)
{
	const IMLReg& dataReg = imlInstruction->op_storeLoad.registerData;
	SReg dataSReg = fpReg<SReg>(dataReg);
	DReg dataDReg = fpReg<DReg>(dataReg);
	WReg realRegisterMem = gpReg<WReg>(imlInstruction->op_storeLoad.registerMem);
	WReg indexReg = indexed ? gpReg<WReg>(imlInstruction->op_storeLoad.registerMem2) : wzr;
	sint32 adrOffset = imlInstruction->op_storeLoad.immS32;
	uint8 mode = imlInstruction->op_storeLoad.mode;

	if (mode == PPCREC_FPR_LD_MODE_SINGLE)
	{
		add_imm(TEMP_GPR1.WReg, realRegisterMem, adrOffset, TEMP_GPR1.WReg);
		if (indexed)
			add(TEMP_GPR1.WReg, TEMP_GPR1.WReg, indexReg);
		ldr(TEMP_GPR2.WReg, AdrExt(MEM_BASE_REG, TEMP_GPR1.WReg, ExtMod::UXTW));
		rev(TEMP_GPR2.WReg, TEMP_GPR2.WReg);
		fmov(dataSReg, TEMP_GPR2.WReg);

		if (imlInstruction->op_storeLoad.flags2.notExpanded)
		{
			// leave value as single
		}
		else
		{
			fcvt(dataDReg, dataSReg);
		}
	}
	else if (mode == PPCREC_FPR_LD_MODE_DOUBLE)
	{
		add_imm(TEMP_GPR1.WReg, realRegisterMem, adrOffset, TEMP_GPR1.WReg);
		if (indexed)
			add(TEMP_GPR1.WReg, TEMP_GPR1.WReg, indexReg);
		ldr(TEMP_GPR2.XReg, AdrExt(MEM_BASE_REG, TEMP_GPR1.WReg, ExtMod::UXTW));
		rev(TEMP_GPR2.XReg, TEMP_GPR2.XReg);
		fmov(dataDReg, TEMP_GPR2.XReg);
	}
	else
	{
		return false;
	}
	return true;
}

// store to memory
bool AArch64GenContext_t::fpr_store(IMLInstruction* imlInstruction, bool indexed)
{
	const IMLReg& dataImlReg = imlInstruction->op_storeLoad.registerData;
	DReg dataDReg = fpReg<DReg>(dataImlReg);
	SReg dataSReg = fpReg<SReg>(dataImlReg);
	WReg memReg = gpReg<WReg>(imlInstruction->op_storeLoad.registerMem);
	WReg indexReg = indexed ? gpReg<WReg>(imlInstruction->op_storeLoad.registerMem2) : wzr;
	sint32 memOffset = imlInstruction->op_storeLoad.immS32;
	uint8 mode = imlInstruction->op_storeLoad.mode;

	if (mode == PPCREC_FPR_ST_MODE_SINGLE)
	{
		add_imm(TEMP_GPR1.WReg, memReg, memOffset, TEMP_GPR1.WReg);
		if (indexed)
			add(TEMP_GPR1.WReg, TEMP_GPR1.WReg, indexReg);

		if (imlInstruction->op_storeLoad.flags2.notExpanded)
		{
			// value is already in single format
			fmov(TEMP_GPR2.WReg, dataSReg);
		}
		else
		{
			fcvt(TEMP_FPR.SReg, dataDReg);
			fmov(TEMP_GPR2.WReg, TEMP_FPR.SReg);
		}
		rev(TEMP_GPR2.WReg, TEMP_GPR2.WReg);
		str(TEMP_GPR2.WReg, AdrExt(MEM_BASE_REG, TEMP_GPR1.WReg, ExtMod::UXTW));
	}
	else if (mode == PPCREC_FPR_ST_MODE_DOUBLE)
	{
		add_imm(TEMP_GPR1.WReg, memReg, memOffset, TEMP_GPR1.WReg);
		if (indexed)
			add(TEMP_GPR1.WReg, TEMP_GPR1.WReg, indexReg);
		fmov(TEMP_GPR2.XReg, dataDReg);
		rev(TEMP_GPR2.XReg, TEMP_GPR2.XReg);
		str(TEMP_GPR2.XReg, AdrExt(MEM_BASE_REG, TEMP_GPR1.WReg, ExtMod::UXTW));
	}
	else if (mode == PPCREC_FPR_ST_MODE_UI32_FROM_PS0)
	{
		add_imm(TEMP_GPR1.WReg, memReg, memOffset, TEMP_GPR1.WReg);
		if (indexed)
			add(TEMP_GPR1.WReg, TEMP_GPR1.WReg, indexReg);
		fmov(TEMP_GPR2.WReg, dataSReg);
		rev(TEMP_GPR2.WReg, TEMP_GPR2.WReg);
		str(TEMP_GPR2.WReg, AdrExt(MEM_BASE_REG, TEMP_GPR1.WReg, ExtMod::UXTW));
	}
	else
	{
		cemu_assert_suspicious();
		cemuLog_log(LogType::Recompiler, "PPCRecompilerAArch64Gen_imlInstruction_fpr_store(): Unsupported mode %d\n", mode);
		return false;
	}
	return true;
}

// FPR op FPR
void AArch64GenContext_t::fpr_r_r(IMLInstruction* imlInstruction)
{
	auto imlRegR = imlInstruction->op_fpr_r_r.regR;
	auto imlRegA = imlInstruction->op_fpr_r_r.regA;

	if (imlInstruction->operation == PPCREC_IML_OP_FPR_FLOAT_TO_INT)
	{
		fcvtzs(gpReg<WReg>(imlRegR), fpReg<DReg>(imlRegA));
		return;
	}
	else if (imlInstruction->operation == PPCREC_IML_OP_FPR_INT_TO_FLOAT)
	{
		scvtf(fpReg<DReg>(imlRegR), gpReg<WReg>(imlRegA));
		return;
	}
	else if (imlInstruction->operation == PPCREC_IML_OP_FPR_BITCAST_INT_TO_FLOAT)
	{
		cemu_assert_debug(imlRegR.GetRegFormat() == IMLRegFormat::F64); // assuming target is always F64 for now
		// exact operation depends on size of types. Floats are automatically promoted to double if the target is F64
		DReg regFprDReg = fpReg<DReg>(imlRegR);
		SReg regFprSReg = fpReg<SReg>(imlRegR);
		if (imlRegA.GetRegFormat() == IMLRegFormat::I32)
		{
			fmov(regFprSReg, gpReg<WReg>(imlRegA));
			// float to double
			fcvt(regFprDReg, regFprSReg);
		}
		else if (imlRegA.GetRegFormat() == IMLRegFormat::I64)
		{
			fmov(regFprDReg, gpReg<XReg>(imlRegA));
		}
		else
		{
			cemu_assert_unimplemented();
		}
		return;
	}

	DReg regR = fpReg<DReg>(imlRegR);
	DReg regA = fpReg<DReg>(imlRegA);

	if (imlInstruction->operation == PPCREC_IML_OP_FPR_ASSIGN)
	{
		fmov(regR, regA);
	}
	else if (imlInstruction->operation == PPCREC_IML_OP_FPR_MULTIPLY)
	{
		fmul(regR, regR, regA);
	}
	else if (imlInstruction->operation == PPCREC_IML_OP_FPR_DIVIDE)
	{
		fdiv(regR, regR, regA);
	}
	else if (imlInstruction->operation == PPCREC_IML_OP_FPR_ADD)
	{
		fadd(regR, regR, regA);
	}
	else if (imlInstruction->operation == PPCREC_IML_OP_FPR_SUB)
	{
		fsub(regR, regR, regA);
	}
	else if (imlInstruction->operation == PPCREC_IML_OP_FPR_FCTIWZ)
	{
		fcvtzs(regR, regA);
	}
	else
	{
		cemu_assert_suspicious();
	}
}

void AArch64GenContext_t::fpr_r_r_r(IMLInstruction* imlInstruction)
{
	DReg regR = fpReg<DReg>(imlInstruction->op_fpr_r_r_r.regR);
	DReg regA = fpReg<DReg>(imlInstruction->op_fpr_r_r_r.regA);
	DReg regB = fpReg<DReg>(imlInstruction->op_fpr_r_r_r.regB);

	if (imlInstruction->operation == PPCREC_IML_OP_FPR_MULTIPLY)
	{
		fmul(regR, regA, regB);
	}
	else if (imlInstruction->operation == PPCREC_IML_OP_FPR_ADD)
	{
		fadd(regR, regA, regB);
	}
	else if (imlInstruction->operation == PPCREC_IML_OP_FPR_SUB)
	{
		fsub(regR, regA, regB);
	}
	else
	{
		cemu_assert_suspicious();
	}
}

/*
 * FPR = op (fprA, fprB, fprC)
 */
void AArch64GenContext_t::fpr_r_r_r_r(IMLInstruction* imlInstruction)
{
	DReg regR = fpReg<DReg>(imlInstruction->op_fpr_r_r_r_r.regR);
	DReg regA = fpReg<DReg>(imlInstruction->op_fpr_r_r_r_r.regA);
	DReg regB = fpReg<DReg>(imlInstruction->op_fpr_r_r_r_r.regB);
	DReg regC = fpReg<DReg>(imlInstruction->op_fpr_r_r_r_r.regC);

	if (imlInstruction->operation == PPCREC_IML_OP_FPR_SELECT)
	{
		fcmp(regA, 0.0);
		fcsel(regR, regC, regB, Cond::GE);
	}
	else
	{
		cemu_assert_suspicious();
	}
}

void AArch64GenContext_t::fpr_r(IMLInstruction* imlInstruction)
{
	DReg regRDReg = fpReg<DReg>(imlInstruction->op_fpr_r.regR);
	SReg regRSReg = fpReg<SReg>(imlInstruction->op_fpr_r.regR);

	if (imlInstruction->operation == PPCREC_IML_OP_FPR_NEGATE)
	{
		fneg(regRDReg, regRDReg);
	}
	else if (imlInstruction->operation == PPCREC_IML_OP_FPR_LOAD_ONE)
	{
		fmov(regRDReg, 1.0);
	}
	else if (imlInstruction->operation == PPCREC_IML_OP_FPR_ABS)
	{
		fabs(regRDReg, regRDReg);
	}
	else if (imlInstruction->operation == PPCREC_IML_OP_FPR_NEGATIVE_ABS)
	{
		fabs(regRDReg, regRDReg);
		fneg(regRDReg, regRDReg);
	}
	else if (imlInstruction->operation == PPCREC_IML_OP_FPR_ROUND_TO_SINGLE_PRECISION_BOTTOM)
	{
		// convert to 32bit single
		fcvt(regRSReg, regRDReg);
		// convert back to 64bit double
		fcvt(regRDReg, regRSReg);
	}
	else if (imlInstruction->operation == PPCREC_IML_OP_FPR_EXPAND_F32_TO_F64)
	{
		// convert bottom to 64bit double
		fcvt(regRDReg, regRSReg);
	}
	else
	{
		cemu_assert_unimplemented();
	}
}

Cond ImlFPCondToArm64Cond(IMLCondition cond)
{
	switch (cond)
	{
	case IMLCondition::UNORDERED_GT:
		return Cond::GT;
	case IMLCondition::UNORDERED_LT:
		return Cond::MI;
	case IMLCondition::UNORDERED_EQ:
		return Cond::EQ;
	case IMLCondition::UNORDERED_U:
		return Cond::VS;
	default:
	{
		cemu_assert_suspicious();
		return Cond::EQ;
	}
	}
}

void AArch64GenContext_t::fpr_compare(IMLInstruction* imlInstruction)
{
	WReg regR = gpReg<WReg>(imlInstruction->op_fpr_compare.regR);
	DReg regA = fpReg<DReg>(imlInstruction->op_fpr_compare.regA);
	DReg regB = fpReg<DReg>(imlInstruction->op_fpr_compare.regB);
	auto cond = ImlFPCondToArm64Cond(imlInstruction->op_fpr_compare.cond);
	fcmp(regA, regB);
	cset(regR, cond);
}

void AArch64GenContext_t::call_imm(IMLInstruction* imlInstruction)
{
	// mesma proteção aplicada em PPCREC_IML_MACRO_HLE (ver comentário lá) - qualquer
	// chamada pra código C++ arbitrário pode, em teoria, mexer nesses 3 registradores
	// reservados sem que isso apareça em nenhuma busca no código-fonte (ex: "outline
	// atomics" do compilador). Salvar/restaurar manualmente nos protege de qualquer forma.
	stp(x27, x28, AdrPreImm(sp, -16));
	stp(x29, x30, AdrPreImm(sp, -16));
	mov(TEMP_GPR1.XReg, imlInstruction->op_call_imm.callAddress);
	blr(TEMP_GPR1.XReg);
	ldp(x29, x30, AdrPostImm(sp, 16));
	ldp(x27, x28, AdrPostImm(sp, 16));
	emitRegisterCanaryCheck("after_call_imm");
}

bool PPCRecompiler_generateAArch64Code(struct PPCRecFunction_t* PPCRecFunction, struct ppcImlGenContext_t* ppcImlGenContext)
{
	AArch64Allocator allocator;
	AArch64GenContext_t aarch64GenContext{&allocator};

	// generate iml instruction code
	bool codeGenerationFailed = false;
	for (IMLSegment* segIt : ppcImlGenContext->segmentList2)
	{
		if (codeGenerationFailed)
			break;
		segIt->x64Offset = aarch64GenContext.getSize();

		aarch64GenContext.storeSegmentStart(segIt);

		for (size_t i = 0; i < segIt->imlList.size(); i++)
		{
			IMLInstruction* imlInstruction = segIt->imlList.data() + i;
			if (imlInstruction->type == PPCREC_IML_TYPE_R_NAME)
			{
				aarch64GenContext.r_name(imlInstruction);
			}
			else if (imlInstruction->type == PPCREC_IML_TYPE_NAME_R)
			{
				aarch64GenContext.name_r(imlInstruction);
			}
			else if (imlInstruction->type == PPCREC_IML_TYPE_R_R)
			{
				if (!aarch64GenContext.r_r(imlInstruction))
					codeGenerationFailed = true;
			}
			else if (imlInstruction->type == PPCREC_IML_TYPE_R_S32)
			{
				if (!aarch64GenContext.r_s32(imlInstruction))
					codeGenerationFailed = true;
			}
			else if (imlInstruction->type == PPCREC_IML_TYPE_R_R_S32)
			{
				if (!aarch64GenContext.r_r_s32(imlInstruction))
					codeGenerationFailed = true;
			}
			else if (imlInstruction->type == PPCREC_IML_TYPE_R_R_S32_CARRY)
			{
				if (!aarch64GenContext.r_r_s32_carry(imlInstruction))
					codeGenerationFailed = true;
			}
			else if (imlInstruction->type == PPCREC_IML_TYPE_R_R_R)
			{
				if (!aarch64GenContext.r_r_r(imlInstruction))
					codeGenerationFailed = true;
			}
			else if (imlInstruction->type == PPCREC_IML_TYPE_R_R_R_CARRY)
			{
				if (!aarch64GenContext.r_r_r_carry(imlInstruction))
					codeGenerationFailed = true;
			}
			else if (imlInstruction->type == PPCREC_IML_TYPE_COMPARE)
			{
				aarch64GenContext.compare(imlInstruction);
			}
			else if (imlInstruction->type == PPCREC_IML_TYPE_COMPARE_S32)
			{
				aarch64GenContext.compare_s32(imlInstruction);
			}
			else if (imlInstruction->type == PPCREC_IML_TYPE_CONDITIONAL_JUMP)
			{
				aarch64GenContext.cjump(imlInstruction, segIt);
			}
			else if (imlInstruction->type == PPCREC_IML_TYPE_JUMP)
			{
				aarch64GenContext.jump(segIt);
			}
			else if (imlInstruction->type == PPCREC_IML_TYPE_CJUMP_CYCLE_CHECK)
			{
				aarch64GenContext.conditionalJumpCycleCheck(segIt);
			}
			else if (imlInstruction->type == PPCREC_IML_TYPE_MACRO)
			{
				if (!aarch64GenContext.macro(imlInstruction))
					codeGenerationFailed = true;
			}
			else if (imlInstruction->type == PPCREC_IML_TYPE_LOAD)
			{
				if (!aarch64GenContext.load(imlInstruction, false))
					codeGenerationFailed = true;
			}
			else if (imlInstruction->type == PPCREC_IML_TYPE_LOAD_INDEXED)
			{
				if (!aarch64GenContext.load(imlInstruction, true))
					codeGenerationFailed = true;
			}
			else if (imlInstruction->type == PPCREC_IML_TYPE_STORE)
			{
				if (!aarch64GenContext.store(imlInstruction, false))
					codeGenerationFailed = true;
			}
			else if (imlInstruction->type == PPCREC_IML_TYPE_STORE_INDEXED)
			{
				if (!aarch64GenContext.store(imlInstruction, true))
					codeGenerationFailed = true;
			}
			else if (imlInstruction->type == PPCREC_IML_TYPE_ATOMIC_CMP_STORE)
			{
				aarch64GenContext.atomic_cmp_store(imlInstruction);
			}
			else if (imlInstruction->type == PPCREC_IML_TYPE_CALL_IMM)
			{
				aarch64GenContext.call_imm(imlInstruction);
			}
			else if (imlInstruction->type == PPCREC_IML_TYPE_NO_OP)
			{
				// no op
			}
			else if (imlInstruction->type == PPCREC_IML_TYPE_FPR_LOAD)
			{
				if (!aarch64GenContext.fpr_load(imlInstruction, false))
					codeGenerationFailed = true;
			}
			else if (imlInstruction->type == PPCREC_IML_TYPE_FPR_LOAD_INDEXED)
			{
				if (!aarch64GenContext.fpr_load(imlInstruction, true))
					codeGenerationFailed = true;
			}
			else if (imlInstruction->type == PPCREC_IML_TYPE_FPR_STORE)
			{
				if (!aarch64GenContext.fpr_store(imlInstruction, false))
					codeGenerationFailed = true;
			}
			else if (imlInstruction->type == PPCREC_IML_TYPE_FPR_STORE_INDEXED)
			{
				if (!aarch64GenContext.fpr_store(imlInstruction, true))
					codeGenerationFailed = true;
			}
			else if (imlInstruction->type == PPCREC_IML_TYPE_FPR_R_R)
			{
				aarch64GenContext.fpr_r_r(imlInstruction);
			}
			else if (imlInstruction->type == PPCREC_IML_TYPE_FPR_R_R_R)
			{
				aarch64GenContext.fpr_r_r_r(imlInstruction);
			}
			else if (imlInstruction->type == PPCREC_IML_TYPE_FPR_R_R_R_R)
			{
				aarch64GenContext.fpr_r_r_r_r(imlInstruction);
			}
			else if (imlInstruction->type == PPCREC_IML_TYPE_FPR_R)
			{
				aarch64GenContext.fpr_r(imlInstruction);
			}
			else if (imlInstruction->type == PPCREC_IML_TYPE_FPR_COMPARE)
			{
				aarch64GenContext.fpr_compare(imlInstruction);
			}
			else
			{
				codeGenerationFailed = true;
				cemu_assert_suspicious();
				cemuLog_log(LogType::Recompiler, "PPCRecompiler_generateAArch64Code(): Unsupported iml type {}", imlInstruction->type);
			}
		}
	}

	// handle failed code generation
	if (codeGenerationFailed)
	{
		return false;
	}

	const size_t codeSize = aarch64GenContext.getSize();
	if (!aarch64GenContext.processAllJumps())
	{
		cemuLog_log(LogType::Recompiler, "PPCRecompiler_generateAArch64Code(): some jumps exceeded the +/-128MB offset.");
		return false;
	}
	aarch64GenContext.setSize(codeSize);

	aarch64GenContext.readyRE();

	// set code
	PPCRecFunction->x86Code = AArch64Allocator::FinalizeExecutableCode(aarch64GenContext.getCode<void*>(), aarch64GenContext.getSize());
	PPCRecFunction->x86Size = aarch64GenContext.getMaxSize();
	PPCRecFunction->relocations = std::move(aarch64GenContext.relocations);
	// set free disabled to skip freeing the code from the CodeGenerator destructor
	allocator.setFreeDisabled(true);
	return true;
}

void PPCRecompiler_cleanupAArch64Code(void* code, size_t size)
{
	AArch64Allocator allocator;
	if (allocator.useProtect())
		CodeArray::protect(code, size, CodeArray::PROTECT_RW);
	allocator.free(static_cast<uint32*>(code));
}

// AOT cache support: re-emits a previously generated (and persisted) function's raw
// machine code into fresh memory, patching in this run's actual absolute addresses at
// the locations AArch64GenContext_t::relocations recorded when it was first compiled
// (see the big comment on PPCRecRelocEntry in PPCRecompiler.h, and on movAbsFixed()
// above, for why that's necessary). Returns nullptr on any sanity-check failure (never
// crashes/asserts in release builds on a corrupt cache entry - callers must gracefully
// fall back to a normal recompile in that case).
void* PPCRecompiler_injectAArch64CachedCode(const uint8* code, size_t codeSize, const std::vector<PPCRecRelocEntry>& relocations)
{
	// AArch64 instructions are always 4 bytes; anything else can only be a corrupt cache entry
	if (codeSize == 0 || (codeSize % 4) != 0)
		return nullptr;

	// declaration order matters here: 'allocator' must outlive 'codeGen' so that CodeArray's
	// destructor can still safely call into it (see the identical pattern in PPCRecompiler_generateAArch64Code)
	AArch64Allocator allocator;
	CodeGenerator codeGen(codeSize, 0 /* ALLOC_BUF, fixed size, allocator sets it RWE/RW immediately */, &allocator);
	uint8* buf = const_cast<uint8*>(codeGen.getCode()); // writable address - see FinalizeExecutableCode's comment
	memcpy(buf, code, codeSize);

	for (auto& reloc : relocations)
	{
		// every relocation on this backend is a movAbsFixed() sequence: exactly 4 instructions / 16 bytes
		if ((size_t)reloc.codeOffset + 16 > codeSize)
		{
			cemu_assert_debug(false); // corrupt/truncated cache entry
			return nullptr;
		}
		uint64 patchValue;
		switch (reloc.type)
		{
		case PPCRecRelocType::HLE_FUNCTION:
			patchValue = (uint64)PPCRecompiler_virtualHLE;
			break;
		default:
			cemu_assert_debug(false); // this backend never emits any other relocation type
			return nullptr;
		}
		// re-emit the exact same fixed movz+movk*3 sequence directly into the target buffer,
		// using the same (tested) encoder that produced it, rather than hand-encoding the bits
		CodeGenerator patchGen(16, buf + reloc.codeOffset, nullptr);
		XReg dst(reloc.regId);
		patchGen.movz(dst, (uint32)(patchValue & 0xFFFF), 0);
		patchGen.movk(dst, (uint32)((patchValue >> 16) & 0xFFFF), 16);
		patchGen.movk(dst, (uint32)((patchValue >> 32) & 0xFFFF), 32);
		patchGen.movk(dst, (uint32)((patchValue >> 48) & 0xFFFF), 48);
	}

	codeGen.setSize(codeSize);
	codeGen.readyRE(); // flips to read+execute (where applicable) and flushes the writable mapping's icache
	allocator.setFreeDisabled(true);
	// on Android this translates from the writable address to the separate executable
	// address (W^X dual-mapping) and additionally clears the icache for THAT address
	// range - readyRE() above only covers the writable mapping, which is not the one the
	// CPU actually fetches instructions from once execution jumps here. On every other
	// platform this is a no-op that returns buf unchanged. Must match exactly what
	// PPCRecompiler_generateAArch64Code does for freshly generated code.
	return AArch64Allocator::FinalizeExecutableCode(buf, codeSize);
}

void AArch64GenContext_t::enterRecompilerCode()
{
	constexpr size_t STACK_SIZE = 160 /* x19 .. x30 + v8.d[0] .. v15.d[0] */;
	static_assert(STACK_SIZE % 16 == 0);
	sub(sp, sp, STACK_SIZE);
	mov(x9, sp);

	stp(x19, x20, AdrPostImm(x9, 16));
	stp(x21, x22, AdrPostImm(x9, 16));
	stp(x23, x24, AdrPostImm(x9, 16));
	stp(x25, x26, AdrPostImm(x9, 16));
	stp(x27, x28, AdrPostImm(x9, 16));
	stp(x29, x30, AdrPostImm(x9, 16));
	st4((v8.d - v11.d)[0], AdrPostImm(x9, 32));
	st4((v12.d - v15.d)[0], AdrPostImm(x9, 32));
	// Le o ENDERECO ATUAL das variaveis globais (nao um valor fixo gravado aqui) toda
	// vez que essa funcao roda. Isso e essencial porque PPCRecompiler_init() libera e
	// realoca ppcRecompilerInstanceData/memory_base cada vez que um titulo e preparado
	// (ex: usuario volta ao menu e abre outro jogo, ou relanca o mesmo, sem fechar o
	// app) - mas este trampolim so e gerado UMA VEZ por processo (guard
	// initializedInterfaceFunctions). Se gravassemos o valor como constante fixa aqui,
	// x27/x28 ficariam apontando pra memoria ja liberada em qualquer sessao de jogo
	// depois da primeira, causando corrupcao/crash so nesses casos - exatamente o tipo
	// de comportamento inconsistente observado nos logs de crash.
	mov(HCPU_REG, x1); // call argument 2
	mov(TEMP_GPR1.XReg, (uint64)&ppcRecompilerInstanceData);
	ldr(PPC_REC_INSTANCE_REG, AdrUimm(TEMP_GPR1.XReg, 0));
	mov(TEMP_GPR1.XReg, (uint64)&memory_base);
	ldr(MEM_BASE_REG, AdrUimm(TEMP_GPR1.XReg, 0));

	// branch to recFunc
	blr(x0); // call argument 1

	mov(x9, sp);
	ldp(x19, x20, AdrPostImm(x9, 16));
	ldp(x21, x22, AdrPostImm(x9, 16));
	ldp(x23, x24, AdrPostImm(x9, 16));
	ldp(x25, x26, AdrPostImm(x9, 16));
	ldp(x27, x28, AdrPostImm(x9, 16));
	ldp(x29, x30, AdrPostImm(x9, 16));
	ld4((v8.d - v11.d)[0], AdrPostImm(x9, 32));
	ld4((v12.d - v15.d)[0], AdrPostImm(x9, 32));

	add(sp, sp, STACK_SIZE);

	ret();
}

void AArch64GenContext_t::leaveRecompilerCode()
{
	str(LR.WReg, AdrUimm(HCPU_REG, offsetof(PPCInterpreter_t, instructionPointer)));
	ret();
}

bool initializedInterfaceFunctions = false;
AArch64Allocator enterRecompilerCode_allocator;
AArch64GenContext_t enterRecompilerCode_ctx{&enterRecompilerCode_allocator};

AArch64Allocator leaveRecompilerCode_unvisited_allocator;
AArch64GenContext_t leaveRecompilerCode_unvisited_ctx{&leaveRecompilerCode_unvisited_allocator};
AArch64Allocator leaveRecompilerCode_visited_allocator;
AArch64GenContext_t leaveRecompilerCode_visited_ctx{&leaveRecompilerCode_visited_allocator};
void PPCRecompilerAArch64Gen_generateRecompilerInterfaceFunctions()
{
	if (initializedInterfaceFunctions)
		return;
	initializedInterfaceFunctions = true;

	enterRecompilerCode_ctx.enterRecompilerCode();
	enterRecompilerCode_ctx.readyRE();
	PPCRecompiler_enterRecompilerCode = decltype(PPCRecompiler_enterRecompilerCode)(AArch64Allocator::FinalizeExecutableCode(enterRecompilerCode_ctx.getCode<void*>(), enterRecompilerCode_ctx.getSize()));

	leaveRecompilerCode_unvisited_ctx.leaveRecompilerCode();
	leaveRecompilerCode_unvisited_ctx.readyRE();
	PPCRecompiler_leaveRecompilerCode_unvisited = decltype(PPCRecompiler_leaveRecompilerCode_unvisited)(AArch64Allocator::FinalizeExecutableCode(leaveRecompilerCode_unvisited_ctx.getCode<void*>(), leaveRecompilerCode_unvisited_ctx.getSize()));

	leaveRecompilerCode_visited_ctx.leaveRecompilerCode();
	leaveRecompilerCode_visited_ctx.readyRE();
	PPCRecompiler_leaveRecompilerCode_visited = decltype(PPCRecompiler_leaveRecompilerCode_visited)(AArch64Allocator::FinalizeExecutableCode(leaveRecompilerCode_visited_ctx.getCode<void*>(), leaveRecompilerCode_visited_ctx.getSize()));
}
