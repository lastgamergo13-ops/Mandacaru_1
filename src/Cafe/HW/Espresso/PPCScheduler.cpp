#include "Cafe/OS/libs/gx2/GX2.h"
#include "Cafe/HW/Latte/Core/Latte.h"
#include "Cafe/OS/libs/coreinit/coreinit_Alarm.h"
#include "Cafe/OS/libs/coreinit/coreinit_Thread.h"
#include "Cafe/HW/Latte/Core/LattePerformanceMonitor.h"

#include "Cafe/HW/Espresso/Recompiler/PPCRecompiler.h"
#include "Cafe/CafeSystem.h"

#if defined(__ANDROID__)
#include "Common/ExceptionHandler/ExceptionHandler.h"
#endif

uint32 ppcThreadQuantum = 45000; // execute 45000 instructions before thread reschedule happens, this value can be overwritten by game profiles

void PPCInterpreter_relinquishTimeslice()
{
	PPCInterpreter_t* hCPU = PPCInterpreter_getCurrentInstance();
	if( hCPU->remainingCycles >= 0 )
	{
		hCPU->skippedCycles = hCPU->remainingCycles + 1;
		hCPU->remainingCycles = -1;
	}
}

void PPCCore_boostQuantum(sint32 numCycles)
{
	PPCInterpreter_t* hCPU = PPCInterpreter_getCurrentInstance();
	hCPU->remainingCycles += numCycles;
}

void PPCCore_deboostQuantum(sint32 numCycles)
{
	PPCInterpreter_t* hCPU = PPCInterpreter_getCurrentInstance();
	hCPU->remainingCycles -= numCycles;
}

namespace coreinit
{
	void __OSThreadSwitchToNext();
}

void PPCCore_switchToScheduler()
{
	cemu_assert_debug(__OSHasSchedulerLock() == false); // scheduler lock must not be hold past thread time slice
	cemu_assert_debug(PPCInterpreter_getCurrentInstance()->coreInterruptMask != 0 || CafeSystem::GetForegroundTitleId() == 0x000500001019e600);
#if defined(__ANDROID__)
	// This runs on every PPC core's normal timeslice-expiry reschedule - NOT triggered by
	// any HLE-exported call, so without this it's invisible to both the last-HLE-call
	// tracker and the thread-status map (the "untracked thread" case). It's also one of
	// the few places that can leak the scheduler lock across a real OS thread boundary
	// by design (see __OSThreadSwitchToNext/Fiber::Switch): if the fiber this thread
	// switches into is never scheduled back to it, this thread stays parked here forever,
	// _still holding the lock_, which then wedges every other thread in the process. This
	// call is frequent (every ~45000 instructions per core) but cheap enough to always log.
	Watchdog_SetThreadStatus("PPC core: timeslice expired, entering scheduler switch (__OSThreadSwitchToNext)");
#endif
	__OSLockScheduler();
	coreinit::__OSThreadSwitchToNext();
	__OSUnlockScheduler();
#if defined(__ANDROID__)
	Watchdog_SetThreadStatus("PPC core: resumed after scheduler switch, back to running game code");
#endif
}

void PPCCore_switchToSchedulerWithLock()
{
	cemu_assert_debug(__OSHasSchedulerLock() == true); // scheduler lock must be hold
	cemu_assert_debug(PPCInterpreter_getCurrentInstance()->coreInterruptMask != 0 || CafeSystem::GetForegroundTitleId() == 0x000500001019e600);
	coreinit::__OSThreadSwitchToNext();
}

void _PPCCore_callbackExit(PPCInterpreter_t* hCPU)
{
	PPCInterpreter_relinquishTimeslice();
	hCPU->instructionPointer = 0;
}

PPCInterpreter_t* PPCCore_executeCallbackInternal(uint32 functionMPTR)
{
	cemu_assert_debug(functionMPTR != 0);
	PPCInterpreter_t* hCPU = PPCInterpreter_getCurrentInstance();
	// remember LR and instruction pointer
	uint32 lr = hCPU->spr.LR;
	uint32 ip = hCPU->instructionPointer;
	// save area
	hCPU->gpr[1] -= 16 * 4;
	// set LR
	hCPU->spr.LR = PPCInterpreter_makeCallableExportDepr(_PPCCore_callbackExit);
	// set instruction pointer
	hCPU->instructionPointer = functionMPTR;
	// execute code until we return from the function
	while (true)
	{
		hCPU->remainingCycles = ppcThreadQuantum;
		hCPU->skippedCycles = 0;
		if (hCPU->remainingCycles > 0)
		{
			// try to enter recompiler immediately
			PPCRecompiler_attemptEnter(hCPU, hCPU->instructionPointer);
			// execute any remaining instructions in interpreter
			while ((--hCPU->remainingCycles) >= 0)
			{
				PPCInterpreterSlim_executeInstruction(hCPU);
			};
		}
		if (hCPU->instructionPointer == 0)
		{
			// restore remaining cycles
			hCPU->remainingCycles += hCPU->skippedCycles;
			hCPU->skippedCycles = 0;
			break;
		}
		coreinit::OSYieldThread();
	}
	// save area
	hCPU->gpr[1] += 16 * 4;
	// restore LR and instruction pointer
	hCPU->spr.LR = lr;
	hCPU->instructionPointer = ip;
	return hCPU;
}

void PPCCore_init()
{
}
