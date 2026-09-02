#include <signal.h>
#include <execinfo.h>
#include <string.h>
#include <string>
#include "config/CemuConfig.h"
#include "util/helpers/StringHelpers.h"
#include "ExceptionHandler.h"

#include "Cafe/HW/Espresso/Debugger/GDBStub.h"
#include "Cafe/HW/Espresso/Debugger/GDBBreakpoints.h"

#if defined(__ANDROID__)
#include <ucontext.h>
#include <pthread.h>
#include <unistd.h>
#include <exception>
#include <cstdio>
#include <vector>

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
	CrashLog_WriteLine(fmt::format("Crashing thread: tid={}  name=\"{}\"", gettid(), threadName));

#if defined(__aarch64__)
	AndroidPrintRegisters(context);
#endif
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

void ExceptionHandler_Init()
{
#if defined(__ANDROID__)
	// Run the signal handler on a separate stack. Without this, a crash caused by a stack
	// overflow (a very real possibility with the recursive PPC recompiler / deep C++ call
	// chains) has no stack space left to run handlerDumpingSignal at all -- the process would
	// just die silently with nothing useful in log.txt. Note: sigaltstack() is per-thread, so
	// this only covers the thread that calls ExceptionHandler_Init() (normally main()/JNI init).
	static std::vector<uint8_t> s_altStack(SIGSTKSZ * 4);
	stack_t altStack{};
	altStack.ss_sp = s_altStack.data();
	altStack.ss_size = s_altStack.size();
	altStack.ss_flags = 0;
	sigaltstack(&altStack, nullptr);

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
}
