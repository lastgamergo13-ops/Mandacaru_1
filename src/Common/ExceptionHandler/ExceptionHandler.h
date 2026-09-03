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
#endif
