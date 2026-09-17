#include "Cafe/OS/common/OSCommon.h"
#include "coreinit_Scheduler.h"
#include <atomic>

// NOTE: this used to be `thread_local`, and on non-Android platforms the lock
// primitive used to be a plain pthread_mutex_t / CRITICAL_SECTION. Both of
// those choices silently assumed that __OSLockScheduler() and the matching
// __OSUnlockScheduler() always happen on the same real OS thread - true in
// the old Boost.Context/ucontext fiber model (one real OS thread hosts many
// fibers, switching its own stack), but FALSE on Android now that every
// Fiber is backed by its own real pthread (see FiberUnix.cpp): the lock is
// routinely taken on one core's scheduler thread and released from whichever
// different fiber thread was just switched into (e.g. __OSThreadCoreIdle
// calls __OSUnlockScheduler() as literally its first action after being
// woken up on its own separate thread). Unlocking a normal pthread_mutex_t
// from a non-owning thread is undefined behavior, and a thread_local counter
// desyncs permanently the moment lock/unlock cross a real thread boundary.
// Since this is a single global (non-recursive) lock, there is at most one
// holder process-wide at any time regardless of which real thread is asking,
// so a plain atomic counter (not thread_local) already gives the right
// answer for __OSHasSchedulerLock() no matter which thread checks it.
std::atomic<sint32> s_schedulerLockCount{0};

#if BOOST_OS_WINDOWS
#include <synchapi.h>
// Semaphore instead of CRITICAL_SECTION for the same cross-thread-release
// reason as the Android path below - kept consistent across platforms even
// though only Android currently exercises the cross-thread release pattern.
HANDLE s_semSchedulerLock;
struct SchedulerLockWinInit { SchedulerLockWinInit() { s_semSchedulerLock = CreateSemaphoreA(nullptr, 1, 1, nullptr); } } s_schedulerLockWinInit;
#else
#include <semaphore.h>
// Binary semaphore instead of pthread_mutex_t: semaphores have no concept of
// thread ownership, so sem_post() from a different thread than the one that
// called sem_wait() is well-defined (unlike pthread_mutex_unlock() from a
// non-owning thread, which is UB). Initialized to 1 (unlocked).
sem_t s_semSchedulerLock;
struct SchedulerLockPosixInit { SchedulerLockPosixInit() { sem_init(&s_semSchedulerLock, 0, 1); } } s_schedulerLockPosixInit;
#endif

void __OSLockScheduler(void* obj)
{
#if BOOST_OS_WINDOWS
	WaitForSingleObject(s_semSchedulerLock, INFINITE);
#else
	sem_wait(&s_semSchedulerLock);
#endif
	sint32 newCount = s_schedulerLockCount.fetch_add(1, std::memory_order_acq_rel) + 1;
	cemu_assert_debug(newCount <= 1); // >= 2 should not happen. Scheduler lock does not allow recursion
}

bool __OSHasSchedulerLock()
{
	return s_schedulerLockCount.load(std::memory_order_acquire) > 0;
}

bool __OSTryLockScheduler(void* obj)
{
	bool r;
#if BOOST_OS_WINDOWS
	r = WaitForSingleObject(s_semSchedulerLock, 0) == WAIT_OBJECT_0;
#else
	r = sem_trywait(&s_semSchedulerLock) == 0;
#endif
	if (r)
	{
		s_schedulerLockCount.fetch_add(1, std::memory_order_acq_rel);
		return true;
	}
	return false;
}

void __OSUnlockScheduler(void* obj)
{
	sint32 newCount = s_schedulerLockCount.fetch_sub(1, std::memory_order_acq_rel) - 1;
	cemu_assert_debug(newCount >= 0);
#if BOOST_OS_WINDOWS
	ReleaseSemaphore(s_semSchedulerLock, 1, nullptr);
#else
	sem_post(&s_semSchedulerLock);
#endif
}

namespace coreinit
{
	uint32 OSIsInterruptEnabled()
	{
		PPCInterpreter_t* hCPU = PPCInterpreter_getCurrentInstance();
		if (hCPU == nullptr)
			return 0;

		return hCPU->coreInterruptMask;
	}

	// disables interrupts and scheduling
	uint32 OSDisableInterrupts()
	{
		PPCInterpreter_t* hCPU = PPCInterpreter_getCurrentInstance();
		if (hCPU == nullptr)
			return 0;
		uint32 prevInterruptMask = hCPU->coreInterruptMask;
		if (hCPU->coreInterruptMask != 0)
		{
			// we have no efficient method to turn off scheduling completely, so instead we just increase the remaining cycles
			if (hCPU->remainingCycles >= 0x40000000)
				cemuLog_log(LogType::Force, "OSDisableInterrupts(): Warning - Interrupts already disabled but the mask was still set? remCycles {:08x} LR {:08x}", hCPU->remainingCycles, hCPU->spr.LR);
			hCPU->remainingCycles += 0x40000000;
		}
		hCPU->coreInterruptMask = 0;
		return prevInterruptMask;
	}

	uint32 OSRestoreInterrupts(uint32 interruptMask)
	{
		PPCInterpreter_t* hCPU = PPCInterpreter_getCurrentInstance();
		if (hCPU == nullptr)
			return 0;
		uint32 prevInterruptMask = hCPU->coreInterruptMask;
		if (hCPU->coreInterruptMask == 0 && interruptMask != 0)
		{
			hCPU->remainingCycles -= 0x40000000;
		}
		hCPU->coreInterruptMask = interruptMask;
		return prevInterruptMask;
	}

	uint32 OSEnableInterrupts()
	{
		PPCInterpreter_t* hCPU = PPCInterpreter_getCurrentInstance();
		uint32 prevInterruptMask = hCPU->coreInterruptMask;
		OSRestoreInterrupts(1);
		return prevInterruptMask;
	}

	void InitializeSchedulerLock()
	{
#if BOOST_OS_WINDOWS
		InitializeCriticalSection(&s_csSchedulerLock);
#else
		pthread_mutexattr_t ma;
		pthread_mutexattr_init(&ma);
		pthread_mutexattr_settype(&ma, PTHREAD_MUTEX_RECURSIVE);
		pthread_mutex_init(&s_ptmSchedulerLock, &ma);
#endif
		cafeExportRegister("coreinit", __OSLockScheduler, LogType::Placeholder);
		cafeExportRegister("coreinit", __OSUnlockScheduler, LogType::Placeholder);

		cafeExportRegister("coreinit", OSDisableInterrupts, LogType::CoreinitThread);
		cafeExportRegister("coreinit", OSEnableInterrupts, LogType::CoreinitThread);
		cafeExportRegister("coreinit", OSRestoreInterrupts, LogType::CoreinitThread);
	}
};
