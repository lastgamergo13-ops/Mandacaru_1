#include <signal.h>
#include <execinfo.h>
#include <string.h>
#include <string>
#include "config/CemuConfig.h"
#include "util/helpers/StringHelpers.h"
#include "ExceptionHandler.h"

#include "Cafe/HW/Espresso/Debugger/GDBStub.h"
#include "Cafe/HW/Espresso/Debugger/GDBBreakpoints.h"
#include "Cemu/Logging/CemuLogging.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/CrashCrumb.h"

#if defined(__ANDROID__)
#include "Cafe/HW/Espresso/PPCState.h"
#include "Cafe/HW/Espresso/Recompiler/PPCRecompiler.h"
#endif

#if defined(__ANDROID__)
#include <ucontext.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include "Common/DiagnosticsFlags.h"
#include <exception>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <mutex>
#include <unordered_set>
#include <dlfcn.h>
#include <unordered_map>
#include <thread>
#include <chrono>
#include <atomic>
#include <sys/syscall.h>

// ---------------------------------------------------------------------------------------------
// Watchdog: lightweight per-thread "what am I doing right now" status registry + a background
// thread that periodically logs it together with current memory usage. Purpose: if the process
// dies WITHOUT our signal handler running at all (frozen/ANR-killed by the system watchdog, or
// OOM-killed by the kernel -- neither raises a catchable signal), log.txt still has the last
// known status of every tracked thread and the memory trend leading up to it, instead of just
// stopping dead with no explanation.
// ---------------------------------------------------------------------------------------------
namespace
{
	struct WatchdogEntry
	{
		std::string threadName;
		std::string status;
	};

	std::mutex s_watchdogMutex;
	std::unordered_map<pid_t, WatchdogEntry> s_watchdogEntries;
	std::atomic_bool s_watchdogThreadStarted{false};

	// Lightweight, lock-free "last HLE call made by this thread" tracker.
	// cafeExportCallWrapper (see OSUtil.h) funnels through here on EVERY single
	// HLE-exported function call - which can be an extremely hot path (potentially
	// millions of calls/sec) - so unlike Watchdog_SetThreadStatus, this must never take
	// a lock or allocate. Slots are claimed once per real thread (cached in a
	// thread_local index afterwards) and updated with plain relaxed atomic stores of
	// static string-literal pointers (lib/func names are always literals from
	// cafeExportRegister, never freed) - cheap enough to call unconditionally.
	struct LastHleCallSlot
	{
		std::atomic<pid_t> tid{0};
		std::atomic<const char*> lib{nullptr};
		std::atomic<const char*> func{nullptr};
	};
	constexpr int kMaxLastHleCallSlots = 64;
	LastHleCallSlot s_lastHleCallSlots[kMaxLastHleCallSlots];

	// tid of the real thread currently holding coreinit's global scheduler lock, or 0
	// if it's free, plus the raw return address it was acquired from (resolved
	// best-effort at report time - see Watchdog_MarkSchedulerLockAcquired/Released).
	std::atomic<pid_t> s_schedulerLockHolderTid{0};
	std::atomic<void*> s_schedulerLockHolderAddr{nullptr};

	// Best-effort resolves a code address to "libname!symbol+offset" via dladdr (works
	// off the dynamic symbol table, so it degrades gracefully rather than failing
	// outright on a release build where local/static symbols have been stripped) and
	// always appends the raw address too, since that alone is still enough to locate the
	// call site with addr2line/objdump against the matching unstripped .so from the build.
	std::string ResolveCodeAddress(void* addr)
	{
		if (!addr)
			return "(unknown)";
		Dl_info info{};
		if (dladdr(addr, &info) && info.dli_fname)
		{
			const char* libName = info.dli_fname;
			// keep just the filename, the full path adds noise
			if (const char* lastSlash = strrchr(libName, '/'))
				libName = lastSlash + 1;
			if (info.dli_sname)
			{
				uintptr_t offset = (uintptr_t)addr - (uintptr_t)info.dli_saddr;
				return fmt::format("{}!{}+0x{:x} (0x{:x})", libName, boost::core::demangle(info.dli_sname), offset, (uintptr_t)addr);
			}
			uintptr_t offsetInLib = (uintptr_t)addr - (uintptr_t)info.dli_fbase;
			return fmt::format("{}+0x{:x} (0x{:x})", libName, offsetInLib, (uintptr_t)addr);
		}
		return fmt::format("0x{:x}", (uintptr_t)addr);
	}

	// returns current resident set size in KB, or -1 if it couldn't be read
	long AndroidReadCurrentRSSKb()
	{
		FILE* f = fopen("/proc/self/status", "r");
		if (!f)
			return -1;
		long rssKb = -1;
		char line[256];
		while (fgets(line, sizeof(line), f))
		{
			if (strncmp(line, "VmRSS:", 6) == 0)
			{
				sscanf(line + 6, "%ld", &rssKb);
				break;
			}
		}
		fclose(f);
		return rssKb;
	}

	// Builds and logs the same diagnostic dump used by the periodic heartbeat (RSS,
	// scheduler lock holder, every tracked/untracked thread's status and last HLE call).
	// label distinguishes what triggered this particular dump in the log (e.g. the
	// regular 15s heartbeat vs. a forced scheduler-lock-timeout recovery).
	void DumpWatchdogDiagnostics(std::string_view label)
	{
		long rssKb = AndroidReadCurrentRSSKb();
		std::string statusDump;
		std::unordered_set<pid_t> dumpedTids;
		{
			std::lock_guard lock(s_watchdogMutex);
			for (auto& [tid, entry] : s_watchdogEntries)
			{
				dumpedTids.insert(tid);
				std::string lastHle;
				for (int i = 0; i < kMaxLastHleCallSlots; i++)
				{
					if (s_lastHleCallSlots[i].tid.load(std::memory_order_relaxed) == tid)
					{
						const char* lib = s_lastHleCallSlots[i].lib.load(std::memory_order_relaxed);
						const char* func = s_lastHleCallSlots[i].func.load(std::memory_order_relaxed);
						if (lib && func)
							lastHle = fmt::format(" [last HLE call: {}.{}]", lib, func);
						break;
					}
				}
				statusDump += fmt::format("\n    tid={} \"{}\": {}{}", tid, entry.threadName, entry.status.empty() ? "(idle)" : entry.status, lastHle);
			}
		}
		// Some real threads (e.g. background loader/worker threads) make HLE calls
		// but never call Watchdog_SetThreadStatus, so they'd otherwise be invisible
		// here even while holding critical locks (like the scheduler lock above).
		// Show them too, using only the lock-free last-HLE-call tracker.
		for (int i = 0; i < kMaxLastHleCallSlots; i++)
		{
			pid_t tid = s_lastHleCallSlots[i].tid.load(std::memory_order_relaxed);
			if (tid == 0 || dumpedTids.count(tid))
				continue;
			const char* lib = s_lastHleCallSlots[i].lib.load(std::memory_order_relaxed);
			const char* func = s_lastHleCallSlots[i].func.load(std::memory_order_relaxed);
			statusDump += fmt::format("\n    tid={} \"(untracked thread)\": [last HLE call: {}.{}]", tid, lib ? lib : "?", func ? func : "?");
		}
		pid_t lockHolderTid = s_schedulerLockHolderTid.load(std::memory_order_relaxed);
		std::string lockHolderDesc = "not held";
		if (lockHolderTid != 0)
		{
			void* lockHolderAddr = s_schedulerLockHolderAddr.load(std::memory_order_relaxed);
			lockHolderDesc = fmt::format("tid={}, acquired at {}", lockHolderTid, ResolveCodeAddress(lockHolderAddr));
		}
		cemuLog_log(LogType::Force, "[Watchdog] {} - RSS: {}MB - scheduler lock: {} - tracked threads:{}",
			label, rssKb > 0 ? rssKb / 1024 : -1, lockHolderDesc, statusDump);
	}

	void WatchdogThreadFunc()
	{
		long lastRssKb = -1;
		int iteration = 0;
		while (true)
		{
			std::this_thread::sleep_for(std::chrono::seconds(3));
			long rssKb = AndroidReadCurrentRSSKb();

			// immediate warning on a large jump, independent of the periodic heartbeat below --
			// this is the sign of a leak/runaway allocation that's about to get us OOM-killed
			if (rssKb > 0 && lastRssKb > 0 && (rssKb - lastRssKb) > 200 * 1024)
			{
				cemuLog_log(LogType::Force, "[Watchdog] Memory usage jumped by {}MB in the last ~3s (now {}MB) - possible leak/runaway allocation, OOM-kill may follow",
					(rssKb - lastRssKb) / 1024, rssKb / 1024);
			}
			lastRssKb = rssKb;

			// V11.47: detector de travamento da thread da GPU (Latte). Se o status "processing frame N" nao muda por ~9s,
			// pede (SIGUSR2) que a propria thread imprima onde esta parada (backtrace). Mostra qual chamada do driver
			// GL/Vulkan travou (ex.: congelamento do Mario Kart 8 ao entrar na corrida). No maximo 2 dumps por travamento.
			{
				static std::string s_stallLastStatus;
				static pid_t s_stallLastTid = 0;
				static int s_stallSame = 0;
				std::string curStatus;
				pid_t curTid = 0;
				{
					std::lock_guard lock(s_watchdogMutex);
					for (auto& [tid, entry] : s_watchdogEntries)
					{
						if (entry.status.rfind("GPU thread - processing frame", 0) == 0)
						{
							curStatus = entry.status;
							curTid = tid;
							break;
						}
					}
				}
				if (curTid != 0 && curTid == s_stallLastTid && curStatus == s_stallLastStatus)
					s_stallSame++;
				else
					s_stallSame = 0;
				s_stallLastTid = curTid;
				s_stallLastStatus = curStatus;
				if (curTid != 0 && (s_stallSame == 3 || s_stallSame == 6))
				{
					cemuLog_log(LogType::Force, "[Stall] thread da GPU (tid={}) sem avancar ha ~{}s ({}); pedindo backtrace", curTid, s_stallSame * 3, curStatus);
					if (s_stallSame == 3)
						Diag::DumpRecentTextures("travamento da GPU"); // so escreve se "Diagnostico de texturas" estiver ligado
					syscall(SYS_tgkill, getpid(), curTid, SIGUSR2);
				}
				// Preso dentro do compilador de shader do driver GL (logs do Moto G54: libusc.so / glCompileShader)?
				// ATENCAO: uma compilacao LENTA (~27 s no Adreno) termina sozinha e o shader e essencial; pular esse shader deixava o jogo
				// branco/preto na abertura seguinte. Por isso so vai para a quarentena quem ficar preso ~2 minutos (s_stallSame conta de
				// 3 em 3 s: 40 = 120 s), ou seja, um travamento de verdade.
				if (curTid != 0 && s_stallSame == 40 && CrashCrumb::g_glCompileActiveGlobal.load() && CrashCrumb::g_glQuarantinePath[0] != 0)
				{
					const unsigned long long b = CrashCrumb::g_glCompileBaseGlobal, a = CrashCrumb::g_glCompileAuxGlobal, t = CrashCrumb::g_glCompileTypeGlobal;
					if (FILE* qf = fopen(CrashCrumb::g_glQuarantinePath, "a"))
					{
						fprintf(qf, "%016llx %016llx %016llx\n", b, a, t);
						fclose(qf);
						CrashLog_WriteLine(fmt::format("[Quarentena] thread da GPU presa ha 2 min no compilador GLSL do driver: shader {:016x} aux={:016x} gravado em gl_shader_quarantine.txt (pulado na proxima abertura)", b, a));
					}
				}
			}

			iteration++;
			if ((iteration % 5) == 0) // ~every 15s
				DumpWatchdogDiagnostics("heartbeat");
		}
	}

	void EnsureWatchdogThreadStarted()
	{
		bool expected = false;
		if (s_watchdogThreadStarted.compare_exchange_strong(expected, true))
		{
			std::thread(WatchdogThreadFunc).detach();
		}
	}
}

void Watchdog_SetThreadStatus(std::string_view status)
{
	EnsureWatchdogThreadStarted();
	pid_t tid = gettid();
	std::lock_guard lock(s_watchdogMutex);
	auto& entry = s_watchdogEntries[tid];
	if (entry.threadName.empty())
	{
		char threadName[16] = {};
		pthread_getname_np(pthread_self(), threadName, sizeof(threadName));
		entry.threadName = threadName;
	}
	entry.status = std::string(status);
}

void Watchdog_SetLastHleCall(const char* lib, const char* func)
{
	thread_local int s_cachedSlot = -1;
	if (s_cachedSlot < 0)
	{
		pid_t tid = gettid();
		for (int i = 0; i < kMaxLastHleCallSlots; i++)
		{
			pid_t expected = 0;
			if (s_lastHleCallSlots[i].tid.load(std::memory_order_relaxed) == tid)
			{
				s_cachedSlot = i;
				break;
			}
			if (s_lastHleCallSlots[i].tid.compare_exchange_strong(expected, tid, std::memory_order_relaxed))
			{
				s_cachedSlot = i;
				break;
			}
		}
		if (s_cachedSlot < 0)
			return; // ran out of slots (should never happen - far more than our real thread count)
	}
	s_lastHleCallSlots[s_cachedSlot].lib.store(lib, std::memory_order_relaxed);
	s_lastHleCallSlots[s_cachedSlot].func.store(func, std::memory_order_relaxed);
}

void Watchdog_MarkSchedulerLockAcquired(void* callerAddr)
{
	s_schedulerLockHolderTid.store(gettid(), std::memory_order_relaxed);
	s_schedulerLockHolderAddr.store(callerAddr, std::memory_order_relaxed);
}

void Watchdog_MarkSchedulerLockReleased()
{
	s_schedulerLockHolderTid.store(0, std::memory_order_relaxed);
	s_schedulerLockHolderAddr.store(nullptr, std::memory_order_relaxed);
}

void Watchdog_DumpDiagnosticsNow(std::string_view label)
{
	EnsureWatchdogThreadStarted();
	DumpWatchdogDiagnostics(label);
}

void Watchdog_ClearThreadStatus()
{
	pid_t tid = gettid();
	std::lock_guard lock(s_watchdogMutex);
	auto it = s_watchdogEntries.find(tid);
	if (it != s_watchdogEntries.end())
		it->second.status.clear();
}

// looks up the last status set by the given thread, empty string if none was ever set
std::string AndroidGetWatchdogStatusForThread(pid_t tid)
{
	std::lock_guard lock(s_watchdogMutex);
	auto it = s_watchdogEntries.find(tid);
	if (it == s_watchdogEntries.end() || it->second.status.empty())
		return {};
	return it->second.status;
}

// captured by AndroidTerminateHandler and printed by AndroidPrintCrashContext once the resulting
// SIGABRT reaches handlerDumpingSignal. Kept separate (instead of calling CrashLog_Create()/Write
// here) because CrashLog_Create() only succeeds once -- calling it here would make the real
// signal handler's own CrashLog_Create() call fail and skip printing the backtrace entirely.
static std::string s_terminateExceptionMessage;

// std::terminate() is called for uncaught C++ exceptions, noexcept violations, exceptions thrown
// while unwinding, etc. By default it just calls abort() (-> SIGABRT, already handled below), but
// without this we'd have no idea *which* exception caused it or what its .what() message was.
void AndroidTerminateHandler()
{
	if (auto exPtr = std::current_exception())
	{
		try
		{
			std::rethrow_exception(exPtr);
		}
		catch (const std::exception& e)
		{
			s_terminateExceptionMessage = fmt::format("std::exception: {}", e.what());
		}
		catch (...)
		{
			s_terminateExceptionMessage = "unknown exception (not derived from std::exception)";
		}
	}
	else
	{
		s_terminateExceptionMessage = "std::terminate called with no active exception (direct terminate()/abort() call, a noexcept violation, or an exception thrown while already unwinding)";
	}
	std::abort();
}

// human readable si_code, covers the codes that actually show up for the signals we handle
const char* AndroidSignalCodeToString(int sig, int code)
{
	switch (sig)
	{
	case SIGSEGV:
		switch (code)
		{
		case SEGV_MAPERR: return "SEGV_MAPERR (address not mapped, e.g. null/wild pointer)";
		case SEGV_ACCERR: return "SEGV_ACCERR (no permission for that memory, e.g. write to read-only/exec-only page)";
		}
		break;
	case SIGBUS:
		switch (code)
		{
		case BUS_ADRALN: return "BUS_ADRALN (misaligned access)";
		case BUS_ADRERR: return "BUS_ADRERR (address does not exist)";
		}
		break;
	case SIGILL:
		switch (code)
		{
		case ILL_ILLOPC: return "ILL_ILLOPC (illegal opcode)";
		case ILL_ILLOPN: return "ILL_ILLOPN (illegal operand)";
		}
		break;
	}
	return nullptr;
}
#endif

#if BOOST_OS_LINUX && !defined(__ANDROID__)
#include "ELFSymbolTable.h"
#endif

#if defined(__ANDROID__)
#include <unwind.h>
#include <dlfcn.h>
#include <cxxabi.h>

// backtrace()/backtrace_symbols() from execinfo.h don't exist on Android's bionic libc.
// Use _Unwind_Backtrace (provided by libgcc/libunwind, always available via the NDK) plus
// dladdr() to reconstruct a tombstone-like "pc <offset> <library> (<symbol>+<offset>)" trace.
struct AndroidBacktraceState
{
	void** current;
	void** end;
};

static _Unwind_Reason_Code AndroidUnwindCallback(struct _Unwind_Context* context, void* arg)
{
	AndroidBacktraceState* state = static_cast<AndroidBacktraceState*>(arg);
	uintptr_t pc = _Unwind_GetIP(context);
	if (pc)
	{
		if (state->current == state->end)
			return _URC_END_OF_STACK;
		*state->current++ = reinterpret_cast<void*>(pc);
	}
	return _URC_NO_REASON;
}

size_t AndroidCaptureBacktrace(void** buffer, size_t maxFrames)
{
	AndroidBacktraceState state = { buffer, buffer + maxFrames };
	_Unwind_Backtrace(AndroidUnwindCallback, &state);
	return state.current - buffer;
}

void AndroidPrintBacktrace(void** buffer, size_t size)
{
	for (size_t i = 0; i < size; i++)
	{
		void* addr = buffer[i];
		Dl_info info{};
		if (dladdr(addr, &info) && info.dli_fname)
		{
			uintptr_t relativeAddr = reinterpret_cast<uintptr_t>(addr) - reinterpret_cast<uintptr_t>(info.dli_fbase);
			std::string symbolPart;
			if (info.dli_sname)
			{
				int demangleStatus = 0;
				char* demangled = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &demangleStatus);
				uintptr_t symOffset = reinterpret_cast<uintptr_t>(addr) - reinterpret_cast<uintptr_t>(info.dli_saddr);
				symbolPart = fmt::format(" ({}+0x{:x})", (demangleStatus == 0 && demangled) ? demangled : info.dli_sname, symOffset);
				if (demangled)
					free(demangled);
			}
			CrashLog_WriteLine(fmt::format("  #{:02} pc 0x{:08x}  {}{}", i, relativeAddr, info.dli_fname, symbolPart));
		}
		else
		{
			CrashLog_WriteLine(fmt::format("  #{:02} pc 0x{:016x}  <unknown>", i, reinterpret_cast<uintptr_t>(addr)));
		}
	}
}

#if defined(__aarch64__)
void AndroidPrintRegisters(void* context)
{
	ucontext_t* uc = static_cast<ucontext_t*>(context);
	auto& mc = uc->uc_mcontext;
	CrashLog_WriteLine(fmt::format("  pc  0x{:016x}  sp  0x{:016x}  pstate 0x{:016x}", (uint64_t)mc.pc, (uint64_t)mc.sp, (uint64_t)mc.pstate));
	for (int i = 0; i < 30; i += 2)
		CrashLog_WriteLine(fmt::format("  x{:<2} 0x{:016x}  x{:<2} 0x{:016x}", i, (uint64_t)mc.regs[i], i + 1, (uint64_t)mc.regs[i + 1]));
	CrashLog_WriteLine(fmt::format("  x30(lr) 0x{:016x}", (uint64_t)mc.regs[30]));
}
#endif

void AndroidPrintCrashContext(int sig, siginfo_t* info, void* context)
{
	const char* codeDescription = AndroidSignalCodeToString(sig, info->si_code);
	if (sig == SIGSEGV || sig == SIGBUS || sig == SIGILL)
	{
		CrashLog_WriteLine(fmt::format("Fault address: 0x{:016x}{}", reinterpret_cast<uintptr_t>(info->si_addr),
			codeDescription ? fmt::format("  ({})", codeDescription) : ""));
	}

	if (sig == SIGABRT && !s_terminateExceptionMessage.empty())
		CrashLog_WriteLine(fmt::format("Uncaught C++ exception: {}", s_terminateExceptionMessage));

	char threadName[16] = {};
	pthread_getname_np(pthread_self(), threadName, sizeof(threadName));
	pid_t crashingTid = gettid();
	CrashLog_WriteLine(fmt::format("Crashing thread: tid={}  name=\"{}\"", crashingTid, threadName));

	std::string watchdogStatus = AndroidGetWatchdogStatusForThread(crashingTid);
	if (!watchdogStatus.empty())
		CrashLog_WriteLine(fmt::format("Last known status of this thread: {}", watchdogStatus));
	if (CrashCrumb::g_text)
		CrashLog_WriteLine(fmt::format("Ultima operacao Vulkan/FrameGen desta thread (migalha #{}): {}", CrashCrumb::g_count, CrashCrumb::g_text));
	else
		CrashLog_WriteLine("Ultima operacao Vulkan/FrameGen desta thread: (nenhuma registrada - a thread nao estava em chamada Vulkan rastreada)");

	// vk_trace.txt tambem em crash por sinal (ex.: SIGSEGV do Turnip no vkQueueSubmit): ultimos pipelines enviados a GPU.
	if (CrashCrumb::g_vkCrashDumpHook)
		CrashCrumb::g_vkCrashDumpHook();

	// Quarentena automatica: se esta thread estava DENTRO de vkCreateGraphicsPipelines quando o driver crashou, grava os
	// hashes VS/PS/GS desse pipeline em vk_quarantine.txt. Na proxima abertura ele e pulado (o jogo pode perder um efeito,
	// mas nao derruba mais o app). So usa open/write/close com buffer na pilha (seguro dentro de tratador de sinal).
	// V11.54: o crash foi dentro do driver compilando pipeline: o cache de pipelines em disco pode estar envenenado/corrompido -> apaga
	// (unlink e seguro em tratador de sinal). Na proxima abertura o driver recompila com cache limpo.
	if (CrashCrumb::g_pipeActive && CrashCrumb::g_pipelineCachePath[0] != 0)
	{
		if (unlink(CrashCrumb::g_pipelineCachePath) == 0)
			CrashLog_WriteLine("[PipelineCache] crash dentro de vkCreateGraphicsPipelines: cache de pipelines do driver APAGADO (sera recriado limpo)");
	}
	if (CrashCrumb::g_pipeActive && CrashCrumb::g_quarantinePath[0] != 0)
	{
		char line[64];
		const unsigned long long vals[3] = { CrashCrumb::g_pipeVs, CrashCrumb::g_pipePs, CrashCrumb::g_pipeGs };
		int pos = 0;
		for (int v = 0; v < 3; v++)
		{
			for (int shift = 60; shift >= 0; shift -= 4)
				line[pos++] = "0123456789abcdef"[(vals[v] >> shift) & 0xF];
			line[pos++] = (v == 2) ? '\n' : ' ';
		}
		int fd = open(CrashCrumb::g_quarantinePath, O_WRONLY | O_CREAT | O_APPEND, 0644);
		if (fd >= 0)
		{
			ssize_t written = write(fd, line, (size_t)pos);
			(void)written;
			close(fd);
			CrashLog_WriteLine("[Quarentena] o crash foi DENTRO de vkCreateGraphicsPipelines: pipeline gravado em vk_quarantine.txt (sera pulado na proxima abertura)");
		}
	}
	// Idem para o OpenGL ES: crash dentro do compilador de shader do driver -> gl_shader_quarantine.txt (shader e pulado na proxima abertura)
	if (CrashCrumb::g_glShaderActive && CrashCrumb::g_glQuarantinePath[0] != 0)
	{
		char line[64];
		const unsigned long long vals[3] = { CrashCrumb::g_glBase, CrashCrumb::g_glAux, CrashCrumb::g_glType };
		int pos = 0;
		for (int v = 0; v < 3; v++)
		{
			for (int shift = 60; shift >= 0; shift -= 4)
				line[pos++] = "0123456789abcdef"[(vals[v] >> shift) & 0xF];
			line[pos++] = (v == 2) ? '\n' : ' ';
		}
		int fd = open(CrashCrumb::g_glQuarantinePath, O_WRONLY | O_CREAT | O_APPEND, 0644);
		if (fd >= 0)
		{
			ssize_t written = write(fd, line, (size_t)pos);
			(void)written;
			close(fd);
			CrashLog_WriteLine("[Quarentena] o crash foi DENTRO da compilacao de um shader GLSL: gravado em gl_shader_quarantine.txt (sera pulado na proxima abertura)");
		}
	}

#if defined(__aarch64__)
	AndroidPrintRegisters(context);
#endif

	// If this thread was running the PPC interpreter/recompiler, print the emulated CPU state.
	// PPCInterpreter_getCurrentInstance() is thread_local and returns null on any thread that
	// isn't currently executing emulated PPC code (most threads), so this is always safe to call.
	PPCInterpreter_t* hCPU = PPCInterpreter_getCurrentInstance();
	if (hCPU)
	{
		CrashLog_WriteLine("PPC interpreter/recompiler state (this thread was executing emulated code):");
		CrashLog_WriteLine(fmt::format("  instructionPointer (PPC PC) = 0x{:08x}", hCPU->instructionPointer));
		CrashLog_WriteLine(fmt::format("  LR = 0x{:08x}  CTR = 0x{:08x}  XER = 0x{:08x}", hCPU->spr.LR, hCPU->spr.CTR, hCPU->spr.XER));
		CrashLog_WriteLine(fmt::format("  gpr[1] (PPC SP) = 0x{:08x}  coreInterruptMask = 0x{:08x}", hCPU->gpr[1], hCPU->coreInterruptMask));
		CrashLog_WriteLine(fmt::format("  reservedMemAddr = 0x{:08x}  memoryException = {}", hCPU->reservedMemAddr, hCPU->memoryException));
		for (int i = 0; i < 32; i += 4)
		{
			CrashLog_WriteLine(fmt::format("  r{:<2} 0x{:08x}  r{:<2} 0x{:08x}  r{:<2} 0x{:08x}  r{:<2} 0x{:08x}",
				i, hCPU->gpr[i], i + 1, hCPU->gpr[i + 1], i + 2, hCPU->gpr[i + 2], i + 3, hCPU->gpr[i + 3]));
		}
	}
#if defined(__aarch64__)
	else
	{
		// PPCInterpreter_getCurrentInstance() relies on a thread_local variable, which can be
		// unset/unreliable specifically while this thread is executing JIT-recompiled code
		// (the recompiler keeps hCPU in a dedicated register instead of touching the
		// thread_local at all - see HCPU_REG/x29 in BackendAArch64.cpp). As a fallback, read
		// that same dedicated register directly from the crash context: if this thread really
		// was executing recompiled code, x29 holds the hCPU pointer regardless of the
		// thread_local's state.
		ucontext_t* uc = static_cast<ucontext_t*>(context);
		uint64_t x29 = (uint64_t)uc->uc_mcontext.regs[29];
		// verificação de sanidade básica: ponteiros de heap/mmap válidos no Android sempre
		// ficam bem acima desse limite. Sem isso, um x29 com lixo óbvio (visto em crashes
		// reais - ex: valor 0x75) fazia o handler tentar ler memória inválida e crashar de
		// novo NELE MESMO, cortando o resto do log logo depois desta seção.
		constexpr uint64_t kMinPlausibleHeapPointer = 0x1000000; // 16 MB
		PPCInterpreter_t* hCPUFromReg = (x29 >= kMinPlausibleHeapPointer) ? reinterpret_cast<PPCInterpreter_t*>(x29) : nullptr;
		if (hCPUFromReg)
		{
			CrashLog_WriteLine("PPC interpreter/recompiler state (inferred from JIT register x29/HCPU_REG - this thread may have been executing recompiled code; the thread_local instance was unset):");
			CrashLog_WriteLine(fmt::format("  instructionPointer (PPC PC) = 0x{:08x}", hCPUFromReg->instructionPointer));
			CrashLog_WriteLine(fmt::format("  LR = 0x{:08x}  CTR = 0x{:08x}  XER = 0x{:08x}", hCPUFromReg->spr.LR, hCPUFromReg->spr.CTR, hCPUFromReg->spr.XER));
			CrashLog_WriteLine(fmt::format("  gpr[1] (PPC SP) = 0x{:08x}  coreInterruptMask = 0x{:08x}", hCPUFromReg->gpr[1], hCPUFromReg->coreInterruptMask));
			CrashLog_WriteLine(fmt::format("  reservedMemAddr = 0x{:08x}  memoryException = {}", hCPUFromReg->reservedMemAddr, hCPUFromReg->memoryException));
			for (int i = 0; i < 32; i += 4)
			{
				CrashLog_WriteLine(fmt::format("  r{:<2} 0x{:08x}  r{:<2} 0x{:08x}  r{:<2} 0x{:08x}  r{:<2} 0x{:08x}",
					i, hCPUFromReg->gpr[i], i + 1, hCPUFromReg->gpr[i + 1], i + 2, hCPUFromReg->gpr[i + 2], i + 3, hCPUFromReg->gpr[i + 3]));
			}
			// x27/x28 are the recompiler's other two dedicated registers (instance data / emulated
			// memory base) - printing them helps confirm whether this really looks like JIT code
			CrashLog_WriteLine(fmt::format("  x27 (PPC_REC_INSTANCE_REG) = 0x{:016x}  x28 (MEM_BASE_REG) = 0x{:016x}",
				(uint64_t)uc->uc_mcontext.regs[27], (uint64_t)uc->uc_mcontext.regs[28]));
		}
	}
#endif
	// histórico das últimas entradas em código recompilado (todas as threads), útil pra
	// identificar exatamente qual função estava rodando e o que veio logo antes de um
	// crash - mesmo que essa thread específica não tenha hCPU disponível acima
	PPCRecompiler_dumpRecentJitEnterHistoryToCrashLog();
	PPCRecompiler_dumpRecentHleCallHistoryToCrashLog();
}

void AndroidPrintLoadedLibraries()
{
	FILE* f = fopen("/proc/self/maps", "r");
	if (!f)
		return;
	CrashLog_WriteLine("Loaded libraries:");
	char line[512];
	std::string lastPath;
	while (fgets(line, sizeof(line), f))
	{
		std::string_view lineView(line);
		size_t pathStart = lineView.find('/');
		if (pathStart == std::string_view::npos)
			continue;
		std::string_view path = lineView.substr(pathStart);
		while (!path.empty() && (path.back() == '\n' || path.back() == '\r'))
			path.remove_suffix(1);
		if (path.size() < 3 || path.substr(path.size() - 3) != ".so")
			continue;
		if (path == lastPath) // skip additional segments (r-x, rw-, ...) of a library we already printed
			continue;
		lastPath = std::string(path);
		size_t spacePos = lineView.find(' ');
		std::string_view addrRange = lineView.substr(0, spacePos);
		CrashLog_WriteLine(fmt::format("  {}  {}", addrRange, path));
	}
	fclose(f);
}

#endif

#if BOOST_OS_LINUX && !defined(__ANDROID__)
void DemangleAndPrintBacktrace(char** backtrace, size_t size)
{
	ELFSymbolTable symTable;
	for (char** i = backtrace; i < backtrace + size; i++)
	{
		std::string_view traceLine{*i};

		// basic check to see if the backtrace line matches expected format
		size_t parenthesesOpen = traceLine.find_last_of('(');
		size_t parenthesesClose = traceLine.find_last_of(')');
		size_t offsetPlus = traceLine.find_last_of('+');
		if (!parenthesesOpen || !parenthesesClose || !offsetPlus ||
			 offsetPlus < parenthesesOpen || offsetPlus > parenthesesClose)
		{
			// fall back to default string
            CrashLog_WriteLine(traceLine);
			continue;
		}

		// attempt to resolve symbol from regular symbol table if missing from dynamic symbol table
		uint64 newOffset = -1;
		std::string_view symbolName = traceLine.substr(parenthesesOpen+1, offsetPlus-parenthesesOpen-1);
		if (symbolName.empty())
		{
			uint64 symbolOffset = StringHelpers::ToInt64(traceLine.substr(offsetPlus+1,offsetPlus+1-parenthesesClose-1));
			symbolName = symTable.OffsetToSymbol(symbolOffset, newOffset);
		}

        CrashLog_WriteLine(traceLine.substr(0, parenthesesOpen+1), false);

        CrashLog_WriteLine(boost::core::demangle(symbolName.empty() ? "" : symbolName.data()), false);

		// print relative or existing symbol offset.
        CrashLog_WriteLine("+", false);
		if (newOffset != -1)
		{
            CrashLog_WriteLine(fmt::format("0x{:x}", newOffset), false);
            CrashLog_WriteLine(traceLine.substr(parenthesesClose));
		}
		else
		{
            CrashLog_WriteLine(traceLine.substr(offsetPlus+1));
		}
	}
}
#endif

// handle signals that would dump core, print stacktrace and then dump depending on config
void handlerDumpingSignal(int sig, siginfo_t *info, void *context)
{
#if defined(ARCH_X86_64) && BOOST_OS_LINUX
	// Check for hardware breakpoints
	if (info->si_signo == SIGTRAP && info->si_code == TRAP_HWBKPT)
	{
		uint64 dr6 = _ReadDR6();
		g_gdbstub->HandleAccessException(dr6);
		return;
	}
#endif

    if(!CrashLog_Create())
        return; // give up if crashlog was already created

    char* sigName = strsignal(sig);
	if (sigName)
	{
		printf("%s!\n", sigName);
	}
	else
	{
		// should never be the case
		printf("Unknown core dumping signal!\n");
	}

	void* backtraceArray[128];
	size_t size;

	// get void*'s for all entries on the stack
#if !defined(__ANDROID__)
	size = backtrace(backtraceArray, 128);
#else
	size = AndroidCaptureBacktrace(backtraceArray, 128);
#endif
    // replace the deepest entry with the actual crash address
#if defined(ARCH_X86_64) && BOOST_OS_LINUX > 0
    ucontext_t *uc = (ucontext_t *)context;
    backtraceArray[0] = (void *)uc->uc_mcontext.gregs[REG_RIP];
#elif defined(__ANDROID__) && defined(__aarch64__)
	// make sure frame 0 is the actual faulting PC, in case the unwinder doesn't
	// manage to walk through the kernel's signal trampoline correctly
	if (size > 0)
		backtraceArray[0] = (void *)((ucontext_t *)context)->uc_mcontext.pc;
#endif

    CrashLog_WriteLine(fmt::format("Error: signal {}:", sig));

#if defined(__ANDROID__)
	AndroidPrintCrashContext(sig, info, context);
#endif

#if BOOST_OS_LINUX && !defined(__ANDROID__)
	char** symbol_trace = backtrace_symbols(backtraceArray, size);

	if (symbol_trace)
	{
        DemangleAndPrintBacktrace(symbol_trace, size);
		free(symbol_trace);
	}
	else
	{
        CrashLog_WriteLine("Failed to read backtrace");
	}
#elif !defined(__ANDROID__)
	backtrace_symbols_fd(backtraceArray, size, STDERR_FILENO);
#else
	if (size > 0)
		AndroidPrintBacktrace(backtraceArray, size);
	else
		CrashLog_WriteLine("Backtrace not available on Android");
	AndroidPrintLoadedLibraries();
#endif

    std::cerr << fmt::format("\nStacktrace and additional info written to:") << std::endl;
    std::cerr << cemuLog_GetLogFilePath().generic_string() << std::endl;

    CrashLog_SetOutputChannels(false, true);
    ExceptionHandler_LogGeneralInfo();
    CrashLog_SetOutputChannels(true, true);

	if (GetConfig().crash_dump == CrashDump::Enabled)
	{
		// reset signal handler to default and re-raise signal to dump core
		signal(sig, SIG_DFL);
		raise(sig);
		return;
	}
	// exit process ignoring all issues
	_Exit(1);
}

void handler_SIGINT(int sig)
{
	/*
	 * Received when pressing CTRL + C in a console
	 * Ideally should be exiting cleanly after saving settings but currently
	 * there's no clean exit pathway (at least on linux) and exiting the app
	 * by any mean ends up with a SIGABRT from the standard library destroying
	 * threads.
	 */
	_Exit(0);
}

#if defined(__ANDROID__)
void ExceptionHandler_InstallAltStackForCurrentThread()
{
	// sigaltstack() is per-thread and needs its own dedicated memory per thread (never share one
	// buffer between two threads that could both be handling a signal at the same time).
	// Deliberately never freed: this is only ever called for a handful of long-lived threads
	// (main thread, the PPC core scheduler threads) and there's no safe point to free it before
	// the thread exits.
	auto* altStackMem = new uint8_t[SIGSTKSZ * 4];
	stack_t altStack{};
	altStack.ss_sp = altStackMem;
	altStack.ss_size = SIGSTKSZ * 4;
	altStack.ss_flags = 0;
	sigaltstack(&altStack, nullptr);
}
#endif

#if defined(__ANDROID__)
// V11.47: SIGUSR2 enviado pelo watchdog a uma thread travada: imprime o backtrace dela no log (nao encerra o app)
static void handlerStallDump(int sig, siginfo_t* info, void* context)
{
	void* frames[40];
	size_t n = AndroidCaptureBacktrace(frames, 40);
	CrashLog_WriteLine("[Stall] backtrace da thread travada:");
	AndroidPrintBacktrace(frames, n);
}
#endif

void ExceptionHandler_Init()
{
#if defined(__ANDROID__)
	// Run the signal handler on a separate stack. Without this, a crash caused by a stack
	// overflow (a very real possibility with the recursive PPC recompiler / deep C++ call
	// chains) has no stack space left to run handlerDumpingSignal at all -- the process would
	// just die silently with nothing useful in log.txt. Note: sigaltstack() is per-thread, so
	// this only covers the thread that calls ExceptionHandler_Init() (normally main()/JNI init).
	// Other important threads (PPC core scheduler threads) install their own via
	// ExceptionHandler_InstallAltStackForCurrentThread(), see coreinit_Thread.cpp.
	ExceptionHandler_InstallAltStackForCurrentThread();

	// catch uncaught C++ exceptions (see AndroidTerminateHandler) so their .what() message ends
	// up in the crash log instead of just "signal 6" with no context
	std::set_terminate(AndroidTerminateHandler);
#endif

	struct sigaction action;
	action.sa_flags = 0;
	sigfillset(&action.sa_mask); // don't allow signals to be interrupted

	action.sa_handler = handler_SIGINT;
	sigaction(SIGINT, &action, nullptr);
	sigaction(SIGTERM, &action, nullptr);

    action.sa_flags = SA_SIGINFO;
#if defined(__ANDROID__)
    action.sa_flags |= SA_ONSTACK;
#endif
    action.sa_handler = nullptr;
	action.sa_sigaction = handlerDumpingSignal;
	sigaction(SIGABRT, &action, nullptr);
	sigaction(SIGBUS, &action, nullptr);
	sigaction(SIGFPE, &action, nullptr);
	sigaction(SIGILL, &action, nullptr);
	sigaction(SIGIOT, &action, nullptr);
	sigaction(SIGQUIT, &action, nullptr);
	sigaction(SIGSEGV, &action, nullptr);
	sigaction(SIGSYS, &action, nullptr);
	sigaction(SIGTRAP, &action, nullptr);
#if defined(__ANDROID__)
	{
		struct sigaction stallAction{};
		stallAction.sa_flags = SA_SIGINFO | SA_RESTART;
		sigemptyset(&stallAction.sa_mask);
		stallAction.sa_sigaction = handlerStallDump;
		sigaction(SIGUSR2, &stallAction, nullptr);
	}
#endif
}
