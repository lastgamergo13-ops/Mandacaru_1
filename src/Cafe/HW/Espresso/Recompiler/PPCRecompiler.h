#pragma once
#include <boost/container/small_vector.hpp>

#define PPC_REC_CODE_AREA_START		(0x00000000) // lower bound of executable memory area. Recompiler expects this address to be 0
#define PPC_REC_CODE_AREA_END		(0x10000000) // upper bound of executable memory area
#define PPC_REC_CODE_AREA_SIZE		(PPC_REC_CODE_AREA_END - PPC_REC_CODE_AREA_START)

#define PPC_REC_ALIGN_TO_4MB(__v)	(((__v)+4*1024*1024-1)&~(4*1024*1024-1))

#define PPC_REC_MAX_VIRTUAL_GPR		(40 + 32) // enough to store 32 GPRs + a few SPRs + temp registers (usually only 1-2)

struct ppcRecRange_t
{
	uint32 ppcAddress;
	uint32 ppcSize;
	void* storedRange;
};

// Identifies an absolute host address that a backend embedded directly into the
// generated machine code (e.g. as an immediate operand). Native machine code produced
// by the recompiler is NOT position independent - both backends embed a small number of
// absolute pointers (to PPCRecompiler_virtualHLE, ppcRecompilerInstanceData and memory_base)
// directly into the instruction stream. Those pointers are only valid for the process run
// that generated them (ASLR places them differently every launch), so if compiled code is
// ever persisted to disk (see PPCRecompilerCache.h) and reloaded in a later run, every one
// of these locations has to be re-patched with the *current* run's actual address before
// the code is safe to execute. This struct records where those locations are and what value
// belongs there, at generation time, so that the AOT disk cache can perform that re-patching.
enum class PPCRecRelocType : uint8
{
	HLE_FUNCTION = 0,  // absolute address of PPCRecompiler_virtualHLE
	INSTANCE_DATA = 1, // absolute address of ppcRecompilerInstanceData (only re-embedded per-function on the x64 backend)
	MEMORY_BASE = 2,   // absolute address of memory_base (only re-embedded per-function on the x64 backend)
};

struct PPCRecRelocEntry
{
	uint32 codeOffset; // byte offset (relative to the start of x86Code) where the relocatable load is emitted
	PPCRecRelocType type;
	uint8 regId; // AArch64 only: index of the destination register the value is loaded into. Unused (0) on x64, where the register is implicit in the fixed instruction encoding
};

struct PPCRecFunction_t
{
	struct JumpTableEntry
	{
		MPTR ppcAddr;
		void* hostEntrypoint;
		
		JumpTableEntry(MPTR ppcAddr, void* hostEntrypoint) : ppcAddr(ppcAddr), hostEntrypoint(hostEntrypoint) {};
	};

	uint32 ppcAddress;
	uint32 ppcSize; // ppc code size of function
	void*  x86Code; // pointer to x86 code
	size_t x86Size;
	std::vector<ppcRecRange_t> list_ranges;
	boost::container::small_vector<JumpTableEntry, 2> jumpTableEntries;
	// Every absolute-address embedding this function's generated code contains (see
	// PPCRecRelocEntry above) - empty for functions that made no HLE calls. Only ever
	// consumed by the AOT disk cache (PPCRecompilerCache.cpp); a normal, freshly compiled
	// function that's never cached simply carries this around unused.
	std::vector<PPCRecRelocEntry> relocations;
};

#include "Cafe/HW/Espresso/Recompiler/IML/IMLInstruction.h"
#include "Cafe/HW/Espresso/Recompiler/IML/IMLSegment.h"

struct IMLInstruction* PPCRecompilerImlGen_generateNewEmptyInstruction(struct ppcImlGenContext_t* ppcImlGenContext);

struct ppcImlGenContext_t
{
	class PPCFunctionBoundaryTracker* boundaryTracker;
	uint32* currentInstruction;
	uint32  ppcAddressOfCurrentInstruction;
	IMLSegment* currentOutputSegment;
	struct PPCBasicBlockInfo* currentBasicBlock{};
	// fpr mode
	bool LSQE{ true };
	bool PSE{ true };
	// cycle counter
	uint32 cyclesSinceLastBranch; // used to track ppc cycles
	std::unordered_map<IMLName, IMLReg> mappedRegs;

	uint32 GetMaxRegId() const
	{
		if (mappedRegs.empty())
			return 0;
		return mappedRegs.size()-1;
	}

	// list of segments
	std::vector<IMLSegment*> segmentList2;
	// code generation control
	bool hasFPUInstruction; // if true, PPCEnter macro will create FP_UNAVAIL checks -> Not needed in user mode
	// analysis info
	struct  
	{
		bool modifiesGQR[8];
	}tracking;
	// debug helpers
	uint32 debug_entryPPCAddress{0};

	~ppcImlGenContext_t()
	{
		for (IMLSegment* imlSegment : segmentList2)
			delete imlSegment;
		segmentList2.clear();
	}

	// append raw instruction
	IMLInstruction& emitInst()
	{
		return *PPCRecompilerImlGen_generateNewEmptyInstruction(this);
	}

	IMLSegment* NewSegment()
	{
		IMLSegment* seg = new IMLSegment();
		segmentList2.emplace_back(seg);
		return seg;
	}

	size_t GetSegmentIndex(IMLSegment* seg)
	{
		for (size_t i = 0; i < segmentList2.size(); i++)
		{
			if (segmentList2[i] == seg)
				return i;
		}
		cemu_assert_error();
		return 0;
	}

	IMLSegment* InsertSegment(size_t index)
	{
		IMLSegment* newSeg = new IMLSegment();
		segmentList2.insert(segmentList2.begin() + index, 1, newSeg);
		return newSeg;
	}

	std::span<IMLSegment*> InsertSegments(size_t index, size_t count)
	{
		segmentList2.insert(segmentList2.begin() + index, count, {});
		for (size_t i = index; i < (index + count); i++)
			segmentList2[i] = new IMLSegment();
		return { segmentList2.data() + index, count};
	}

	void UpdateSegmentIndices()
	{
		for (size_t i = 0; i < segmentList2.size(); i++)
			segmentList2[i]->momentaryIndex = (sint32)i;
	}
};

typedef void ATTR_MS_ABI (*PPCREC_JUMP_ENTRY)();

typedef struct  
{
	PPCREC_JUMP_ENTRY ppcRecompilerDirectJumpTable[PPC_REC_ALIGN_TO_4MB(PPC_REC_CODE_AREA_SIZE/4)]; // lookup table for ppc offset to native code function
	// x64 data
	alignas(16) uint64 _x64XMM_xorNegateMaskBottom[2];
	alignas(16) uint64 _x64XMM_xorNegateMaskPair[2];
	alignas(16) uint64 _x64XMM_xorNOTMask[2];
	alignas(16) uint64 _x64XMM_andAbsMaskBottom[2];
	alignas(16) uint64 _x64XMM_andAbsMaskPair[2];
	alignas(16) uint32 _x64XMM_andFloatAbsMaskBottom[4];
	alignas(16) uint64 _x64XMM_singleWordMask[2];
	alignas(16) double _x64XMM_constDouble1_1[2];
	alignas(16) double _x64XMM_constDouble0_0[2];
	alignas(16) float  _x64XMM_constFloat0_0[2];
	alignas(16) float  _x64XMM_constFloat1_1[2];
	alignas(16) float  _x64XMM_constFloatMin[2];
	alignas(16) uint32 _x64XMM_flushDenormalMask1[4];
	alignas(16) uint32 _x64XMM_flushDenormalMaskResetSignBits[4];
	// MXCSR
	uint32 _x64XMM_mxCsr_ftzOn;
	uint32 _x64XMM_mxCsr_ftzOff;
}PPCRecompilerInstanceData_t;

extern PPCRecompilerInstanceData_t* ppcRecompilerInstanceData;

void PPCRecompiler_init();
void PPCRecompiler_Shutdown();

void PPCRecompiler_Enable();
void PPCRecompiler_Disable();

void PPCRecompiler_allocateRange(uint32 startAddress, uint32 size);

void PPCRecompiler_invalidateRange(uint32 startAddr, uint32 endAddr);

extern void ATTR_MS_ABI (*PPCRecompiler_enterRecompilerCode)(uint64 codeMem, uint64 ppcInterpreterInstance);
extern void ATTR_MS_ABI (*PPCRecompiler_leaveRecompilerCode_visited)();
extern void ATTR_MS_ABI (*PPCRecompiler_leaveRecompilerCode_unvisited)();

// Diagnóstico de crash: despeja no log.txt (via CrashLog_WriteLine, síncrono) o histórico
// das últimas chamadas de entrada em código recompilado, na ordem em que aconteceram.
// Só deve ser chamada UMA VEZ, dentro do handler de crash (faz I/O, mas isso não é
// problema ali - a essa altura o processo já está terminando de qualquer forma).
void PPCRecompiler_dumpRecentJitEnterHistoryToCrashLog();
// Diagnóstico de crash: mesma ideia acima, mas para as últimas chamadas HLE (funções de
// sistema/gráficas do Wii U, ex: GX2DrawEx), com o nome legível de cada uma.
void PPCRecompiler_dumpRecentHleCallHistoryToCrashLog();

// recompiler interface

void PPCRecompiler_recompileIfUnvisited(uint32 enterAddress);
void PPCRecompiler_attemptEnter(struct PPCInterpreter_t* hCPU, uint32 enterAddress);
void PPCRecompiler_attemptEnterWithoutRecompile(struct PPCInterpreter_t* hCPU, uint32 enterAddress);
