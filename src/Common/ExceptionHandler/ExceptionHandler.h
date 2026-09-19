#pragma once

void ExceptionHandler_Init();

bool CrashLog_Create();
void CrashLog_SetOutputChannels(bool writeToStdErr, bool writeToLogTxt);
void CrashLog_WriteLine(std::string_view text, bool newLine = true);
void CrashLog_WriteHeader(const char* header);

void ExceptionHandler_LogGeneralInfo();

#if defined(__ANDROID__)
// sigaltstack() is per-thread. ExceptionHandler_Init() only installs one for the thread that
// calls it (normally main/JNI init). Call this at the start of any other thread where a crash
// should still be able to run the handler even if that thread's own stack is exhausted (e.g.
// the PPC recompiler/scheduler threads, which recurse deeply) -- otherwise a stack-overflow
// crash on that thread dies silently with nothing in log.txt.
void ExceptionHandler_InstallAltStackForCurrentThread();

// Registers the calling thread's name + tid + a short "what it's doing right now" status string
// with the watchdog (see WatchdogThread.cpp). Call again to update the status as the thread
// progresses through distinct phases of work; last value set is what shows up in log.txt if the
// process dies without the normal signal handler running (freeze, OOM-kill, ANR).
void Watchdog_SetThreadStatus(std::string_view status);
void Watchdog_ClearThreadStatus();

// Records the (library, function) name of the most recent HLE-exported call made by the
// calling thread, using lock-free relaxed atomics only - safe to call unconditionally from
// cafeExportCallWrapper (OSUtil.h), which is the single choke point every HLE call funnels
// through and can fire millions of times/sec. Shows up in the watchdog heartbeat next to a
// thread's status, so a thread stuck "running" with no coreinit-level wait recorded can still
// be pinned down to whichever native function it last entered.
void Watchdog_SetLastHleCall(const char* lib, const char* func);

// Tracks which real thread currently holds coreinit's single global scheduler lock
// (see coreinit_Scheduler.cpp / __OSLockScheduler). That lock is taken and released
// constantly by nearly every coreinit synchronization primitive, and it being leaked
// (acquired but never released, e.g. by an unbalanced early-return somewhere) would
// freeze the ENTIRE emulated OS permanently - every other thread that ever calls
// __OSLockScheduler again blocks forever. Called only from __OSLockScheduler/
// __OSUnlockScheduler themselves, so the call frequency matches the lock's own
// (high, but nowhere near the volume of raw HLE calls) - cheap atomic store, no lock.
// callerAddr should be __builtin_return_address(0) from the call site - this makes the
// tracking fully general: ANY call site anywhere in the native code that ever acquires
// this lock is automatically identifiable in the watchdog heartbeat (best-effort
// resolved to a library+symbol name via dladdr, always with the raw address as a
// guaranteed fallback even when symbols are stripped) - no per-file instrumentation
// needed to catch a leak, including from code nobody has looked at yet.
void Watchdog_MarkSchedulerLockAcquired(void* callerAddr);
void Watchdog_MarkSchedulerLockReleased();
#endif
