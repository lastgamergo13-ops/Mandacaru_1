#pragma once
// Diagnosticos para Android (header-only, sem mudar o CMake).
//  - LogSystemInfo(): aparelho, SoC, CPU (features/frequencias), RAM, swap, temperaturas.
//  - StartPeriodic(): a cada 5 s escreve uma linha "[Stats]" com RAM do processo/sistema, threads, memoria
//    de codigo JIT (recompilador), temperatura, frequencias de CPU/GPU e alertas (RAM baixa, calor).
//  - LogSnapshot(motivo): mesma coisa na hora de um erro (ex.: DEVICE_LOST).
//  - VkResultName(): nome legivel de um VkResult.
// Tudo e "best effort": arquivos /proc e /sys ilegiveis sao ignorados em silencio.
#if defined(__ANDROID__)
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <dirent.h>
#include <unistd.h>
#include <sys/system_properties.h>
#include "Cemu/Logging/CemuLogging.h"
#include <fmt/format.h>

namespace AndroidDiag
{
	inline std::string ReadAll(const char* path, size_t maxBytes = 65536)
	{
		std::ifstream f(path, std::ios::binary);
		if (!f.is_open()) return {};
		std::string s(maxBytes, '\0');
		f.read(s.data(), (std::streamsize)maxBytes);
		s.resize((size_t)f.gcount());
		return s;
	}

	inline std::string Trim(std::string s)
	{
		while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
		size_t i = 0;
		while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
		return s.substr(i);
	}

	inline std::string Prop(const char* name)
	{
		char buf[PROP_VALUE_MAX] = {};
		__system_property_get(name, buf);
		return buf;
	}

	// valor numerico (kB) de uma chave em /proc/meminfo, /proc/self/status, etc.
	inline long KeyKb(const std::string& text, const char* key)
	{
		size_t p = text.find(key);
		if (p == std::string::npos) return -1;
		p += strlen(key);
		while (p < text.size() && (text[p] == ' ' || text[p] == '\t')) p++;
		return atol(text.c_str() + p);
	}

	inline long ReadLong(const char* path)
	{
		std::string s = Trim(ReadAll(path, 64));
		return s.empty() ? -1 : atol(s.c_str());
	}

	// maior temperatura entre as thermal zones (graus C) e o nome da zona
	inline double MaxThermal(std::string* zoneName = nullptr)
	{
		double best = -1;
		for (int i = 0; i < 64; i++)
		{
			char p[96];
			snprintf(p, sizeof(p), "/sys/class/thermal/thermal_zone%d/temp", i);
			long v = ReadLong(p);
			if (v <= 0) continue;
			double c = v > 1000 ? v / 1000.0 : (double)v;
			if (c > 120 || c < 0) continue;
			if (c > best)
			{
				best = c;
				if (zoneName)
				{
					snprintf(p, sizeof(p), "/sys/class/thermal/thermal_zone%d/type", i);
					*zoneName = Trim(ReadAll(p, 64));
				}
			}
		}
		return best;
	}

	// memoria de codigo executavel anonima (cache do recompilador JIT): conta mapeamentos r-xp / rwxp sem arquivo
	inline void JitMappings(int& count, long& totalKb)
	{
		count = 0; totalKb = 0;
		std::ifstream f("/proc/self/maps");
		std::string line;
		while (std::getline(f, line))
		{
			// formato: inicio-fim perms offset dev inode [caminho]
			size_t sp = line.find(' ');
			if (sp == std::string::npos || line.size() < sp + 5) continue;
			if (line[sp + 3] != 'x') continue;
			if (line.find('/') != std::string::npos || line.find('[') != std::string::npos) continue; // so anonimos
			unsigned long long a = 0, b = 0;
			if (sscanf(line.c_str(), "%llx-%llx", &a, &b) != 2) continue;
			count++;
			totalKb += (long)((b - a) / 1024);
		}
	}

	inline int CountThreads()
	{
		int n = 0;
		if (DIR* d = opendir("/proc/self/task"))
		{
			while (dirent* e = readdir(d)) if (e->d_name[0] != '.') n++;
			closedir(d);
		}
		return n;
	}

	inline std::string Snapshot()
	{
		const std::string status = ReadAll("/proc/self/status");
		const std::string mem = ReadAll("/proc/meminfo");
		const std::string smaps = ReadAll("/proc/self/smaps_rollup", 4096);
		long rss = KeyKb(status, "VmRSS:"), vmsz = KeyKb(status, "VmSize:"), swapProc = KeyKb(status, "VmSwap:");
		long pss = KeyKb(smaps, "Pss:");
		long memAvail = KeyKb(mem, "MemAvailable:"), memFree = KeyKb(mem, "MemFree:"), swapFree = KeyKb(mem, "SwapFree:"), swapTotal = KeyKb(mem, "SwapTotal:");
		int jitN; long jitKb; JitMappings(jitN, jitKb);
		std::string zone; double temp = MaxThermal(&zone);
		long batt = ReadLong("/sys/class/power_supply/battery/temp"); // decimos de grau
		long gpuClk = ReadLong("/sys/class/kgsl/kgsl-3d0/gpuclk");   // Hz (Adreno)
		long gpuBusy = -1;
		{
			std::string s = Trim(ReadAll("/sys/class/kgsl/kgsl-3d0/gpubusy", 64)); // "busy total"
			long busy = 0, total = 0;
			if (sscanf(s.c_str(), "%ld %ld", &busy, &total) == 2 && total > 0) gpuBusy = busy * 100 / total;
		}
		std::string cpuFreq;
		for (int c = 0; c < 8; c++)
		{
			char p[96];
			snprintf(p, sizeof(p), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", c);
			long f = ReadLong(p);
			if (f > 0) cpuFreq += fmt::format("{}{}", cpuFreq.empty() ? "" : "/", f / 1000);
		}
		return fmt::format(
			"RSS={}MB PSS={}MB VmSize={}MB swapProc={}MB | RAMlivre(MemAvailable)={}MB MemFree={}MB swap={}/{}MB | threads={} | JIT(exec anonima)={} mapeamentos {}MB | temp max={:.1f}C({}) bateria={:.1f}C | GPU={}MHz uso={}% | CPU MHz[{}]",
			rss / 1024, pss / 1024, vmsz / 1024, swapProc / 1024, memAvail / 1024, memFree / 1024, (swapTotal - swapFree) / 1024, swapTotal / 1024,
			CountThreads(), jitN, jitKb / 1024, temp, zone, batt > 0 ? batt / 10.0 : -1.0, gpuClk > 0 ? gpuClk / 1000000 : -1, gpuBusy, cpuFreq);
	}

	inline void LogSnapshot(const char* reason)
	{
		cemuLog_log(LogType::Force, "[Diag] SNAPSHOT ({}): {}", reason, Snapshot());
	}

	inline void LogSystemInfo()
	{
		static std::atomic<bool> s_done{ false };
		if (s_done.exchange(true)) return;
		cemuLog_log(LogType::Force, "[Diag] Aparelho: {} {} | Android {} (API {}) | SoC/plataforma: {} / {} / {} | build {}",
			Prop("ro.product.manufacturer"), Prop("ro.product.model"), Prop("ro.build.version.release"), Prop("ro.build.version.sdk"),
			Prop("ro.soc.model"), Prop("ro.board.platform"), Prop("ro.hardware"), Prop("ro.build.fingerprint"));
		const std::string cpuinfo = ReadAll("/proc/cpuinfo", 16384);
		int cores = 0; for (size_t p = 0; (p = cpuinfo.find("processor", p)) != std::string::npos; p += 9) cores++;
		std::string features;
		size_t fp = cpuinfo.find("Features");
		if (fp != std::string::npos)
		{
			size_t e = cpuinfo.find('\n', fp);
			features = Trim(cpuinfo.substr(fp, e == std::string::npos ? std::string::npos : e - fp));
		}
		cemuLog_log(LogType::Force, "[Diag] CPU: {} nucleos | {}", cores, features);
		std::string freqs;
		for (int c = 0; c < 16; c++)
		{
			char p[96];
			snprintf(p, sizeof(p), "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", c);
			long f = ReadLong(p);
			if (f <= 0) continue;
			char g[96];
			snprintf(g, sizeof(g), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_governor", c);
			freqs += fmt::format("cpu{}={}MHz({}) ", c, f / 1000, Trim(ReadAll(g, 32)));
		}
		cemuLog_log(LogType::Force, "[Diag] CPU freq max/governor: {}", freqs);
		const std::string mem = ReadAll("/proc/meminfo");
		cemuLog_log(LogType::Force, "[Diag] RAM: total={}MB disponivel={}MB swap={}MB zram/swap em uso={}MB", KeyKb(mem, "MemTotal:") / 1024, KeyKb(mem, "MemAvailable:") / 1024, KeyKb(mem, "SwapTotal:") / 1024, (KeyKb(mem, "SwapTotal:") - KeyKb(mem, "SwapFree:")) / 1024);
		cemuLog_log(LogType::Force, "[Diag] Estado inicial: {}", Snapshot());
	}

	// linha periodica; frames = contador opcional de frames (para FPS), pode ser nullptr
	inline void StartPeriodic(const std::atomic<uint64_t>* frameCounter = nullptr)
	{
		static std::atomic<bool> s_started{ false };
		if (s_started.exchange(true)) return;
		std::thread([frameCounter]()
		{
			uint64_t lastFrames = frameCounter ? frameCounter->load() : 0;
			auto lastTime = std::chrono::steady_clock::now();
			int tick = 0;
			long lastRss = -1;
			while (true)
			{
				std::this_thread::sleep_for(std::chrono::seconds(5));
				tick++;
				const auto now = std::chrono::steady_clock::now();
				const double dt = std::chrono::duration<double>(now - lastTime).count();
				lastTime = now;
				std::string fps;
				if (frameCounter)
				{
					const uint64_t f = frameCounter->load();
					fps = fmt::format(" | frames/s={:.1f}", (double)(f - lastFrames) / dt);
					lastFrames = f;
				}
				cemuLog_log(LogType::Force, "[Stats] {}{}", Snapshot(), fps);
				const std::string mem = ReadAll("/proc/meminfo");
				const long avail = KeyKb(mem, "MemAvailable:") / 1024;
				const long rss = KeyKb(ReadAll("/proc/self/status"), "VmRSS:") / 1024;
				if (avail >= 0 && avail < 400)
					cemuLog_log(LogType::Force, "[Stats] ALERTA: pouca RAM livre no sistema ({}MB) - o Android pode matar o app (OOM) ou a GPU pode falhar por falta de memoria", avail);
				if (lastRss >= 0 && rss - lastRss > 300)
					cemuLog_log(LogType::Force, "[Stats] ALERTA: o app cresceu {}MB de RAM em 5 s (agora {}MB) - possivel vazamento/alocacao descontrolada", rss - lastRss, rss);
				lastRss = rss;
				const double t = MaxThermal();
				if (t >= 45.0 && tick % 6 == 0)
					cemuLog_log(LogType::Force, "[Stats] ALERTA: aparelho quente ({:.1f}C) - possivel throttling de CPU/GPU", t);
			}
		}).detach();
	}

	inline const char* VkResultName(int r)
	{
		switch (r)
		{
		case 0: return "VK_SUCCESS";
		case 1: return "VK_NOT_READY";
		case 2: return "VK_TIMEOUT";
		case 3: return "VK_EVENT_SET";
		case 4: return "VK_EVENT_RESET";
		case 5: return "VK_INCOMPLETE";
		case -1: return "VK_ERROR_OUT_OF_HOST_MEMORY";
		case -2: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
		case -3: return "VK_ERROR_INITIALIZATION_FAILED";
		case -4: return "VK_ERROR_DEVICE_LOST";
		case -5: return "VK_ERROR_MEMORY_MAP_FAILED";
		case -6: return "VK_ERROR_LAYER_NOT_PRESENT";
		case -7: return "VK_ERROR_EXTENSION_NOT_PRESENT";
		case -8: return "VK_ERROR_FEATURE_NOT_PRESENT";
		case -9: return "VK_ERROR_INCOMPATIBLE_DRIVER";
		case -10: return "VK_ERROR_TOO_MANY_OBJECTS";
		case -11: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
		case -12: return "VK_ERROR_FRAGMENTED_POOL";
		case -1000069000: return "VK_ERROR_OUT_OF_POOL_MEMORY";
		case -1000000000: return "VK_ERROR_SURFACE_LOST_KHR";
		case -1000000001: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
		case 1000001003: return "VK_SUBOPTIMAL_KHR";
		case -1000001004: return "VK_ERROR_OUT_OF_DATE_KHR";
		default: return "VkResult desconhecido";
		}
	}
}
#else
namespace AndroidDiag
{
	inline void LogSnapshot(const char*) {}
	inline void LogSystemInfo() {}
	inline const char* VkResultName(int) { return ""; }
}
#endif
