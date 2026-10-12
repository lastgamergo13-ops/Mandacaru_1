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
#include <functional>
#include <mutex>
#include <vector>
#include <deque>
#include <map>
#include <unordered_map>
#include <algorithm>
#include <malloc.h>
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

	// provedores extras (ex.: metricas do recompilador) - cada um devolve uma string que vira parte do snapshot
	inline std::vector<std::function<std::string()>>& Providers() { static std::vector<std::function<std::string()>> v; return v; }
	inline std::mutex& ProvidersMutex() { static std::mutex m; return m; }
	inline void AddProvider(std::function<std::string()> f) { std::lock_guard<std::mutex> l(ProvidersMutex()); Providers().push_back(std::move(f)); }
	inline std::string ExtraInfo()
	{
		std::string out;
		std::lock_guard<std::mutex> l(ProvidersMutex());
		for (auto& f : Providers()) out += " | " + f();
		return out;
	}


	// ---------------------------------------------------------------------------------------------
	// Rastreio de memoria Vulkan (vkAllocateMemory/vkFreeMemory feitos pelo VKRMemoryManager)
	// ---------------------------------------------------------------------------------------------
	struct VkMemTracker
	{
		std::mutex m;
		std::unordered_map<uint64_t, std::pair<uint64_t, uint32_t>> live; // handle -> (bytes, tipo de memoria)
		std::map<uint32_t, uint64_t> bytesPerType;
		uint64_t liveBytes = 0, peakBytes = 0, allocCount = 0, freeCount = 0, totalAllocBytes = 0;
		uint64_t bufferBytes = 0, textureBytes = 0; // kind 1 = buffers, 0 = texturas (heap de imagens)
		std::unordered_map<uint64_t, int> kindOf;
	};
	inline VkMemTracker& VkMem() { static VkMemTracker t; return t; }
	inline void VkMemAlloc(uint64_t handle, uint64_t bytes, uint32_t type, int kind)
	{
		auto& t = VkMem(); std::lock_guard<std::mutex> l(t.m);
		t.live[handle] = { bytes, type }; t.kindOf[handle] = kind;
		t.bytesPerType[type] += bytes; t.liveBytes += bytes; t.allocCount++; t.totalAllocBytes += bytes;
		(kind ? t.bufferBytes : t.textureBytes) += bytes;
		if (t.liveBytes > t.peakBytes) t.peakBytes = t.liveBytes;
	}
	inline void VkMemFree(uint64_t handle)
	{
		auto& t = VkMem(); std::lock_guard<std::mutex> l(t.m);
		auto it = t.live.find(handle);
		if (it == t.live.end()) return;
		t.bytesPerType[it->second.second] -= it->second.first; t.liveBytes -= it->second.first; t.freeCount++;
		(t.kindOf[handle] ? t.bufferBytes : t.textureBytes) -= it->second.first;
		t.kindOf.erase(handle); t.live.erase(it);
	}
	inline std::string VkMemInfo()
	{
		auto& t = VkMem(); std::lock_guard<std::mutex> l(t.m);
		std::string types;
		for (auto& kv : t.bytesPerType) if (kv.second) types += fmt::format("{}tipo{}={}MB", types.empty() ? "" : " ", kv.first, kv.second / 1048576);
		return fmt::format("VKMEM vivo={}MB (texturas={}MB buffers={}MB) pico={}MB blocos vivos={} alocs={} liberacoes={} [{}]",
			t.liveBytes / 1048576, t.textureBytes / 1048576, t.bufferBytes / 1048576, t.peakBytes / 1048576, t.live.size(), t.allocCount, t.freeCount, types);
	}

	// ---------------------------------------------------------------------------------------------
	// Detalhe de memoria do processo
	// ---------------------------------------------------------------------------------------------
	inline int CountDir(const char* path)
	{
		int n = 0;
		if (DIR* d = opendir(path)) { while (dirent* e = readdir(d)) if (e->d_name[0] != '.') n++; closedir(d); }
		return n;
	}

	inline long CountLines(const char* path)
	{
		std::ifstream f(path); std::string l; long n = 0;
		while (std::getline(f, l)) n++;
		return n;
	}

	// RAM do processo discriminada: anonima (heap/GPU/emulada) x arquivos x shmem, swap, pico, malloc
	inline std::string ProcessMemDetail()
	{
		const std::string st = ReadAll("/proc/self/status");
		const std::string sr = ReadAll("/proc/self/smaps_rollup", 4096);
		std::string mallocInfo;
#if defined(__BIONIC__) || defined(__ANDROID__)
		struct mallinfo mi = mallinfo();
		mallocInfo = fmt::format(" | malloc: arena={}MB emUso={}MB livre(no heap)={}MB mmap={}MB", (long)mi.arena / 1048576, (long)mi.uordblks / 1048576, (long)mi.fordblks / 1048576, (long)mi.hblkhd / 1048576);
#endif
		return fmt::format("RssAnon={}MB RssFile={}MB RssShmem={}MB pico(VmHWM)={}MB VmData={}MB swapProc={}MB PrivDirty={}MB | fds={} mapeamentos={}{}",
			KeyKb(st, "RssAnon:") / 1024, KeyKb(st, "RssFile:") / 1024, KeyKb(st, "RssShmem:") / 1024, KeyKb(st, "VmHWM:") / 1024,
			KeyKb(st, "VmData:") / 1024, KeyKb(st, "VmSwap:") / 1024, KeyKb(sr, "Private_Dirty:") / 1024,
			CountDir("/proc/self/fd"), CountLines("/proc/self/maps"), mallocInfo);
	}

	// Soma de Rss por categoria de mapeamento (le /proc/self/smaps; ~alguns ms) + os maiores mapeamentos
	struct SmapsCats { std::map<std::string, long> kb; std::vector<std::pair<long, std::string>> top; };
	inline SmapsCats ReadSmaps()
	{
		SmapsCats r;
		std::ifstream f("/proc/self/smaps");
		std::string line, cur; bool isHeader;
		unsigned long long a = 0, b = 0; std::string perms;
		while (std::getline(f, line))
		{
			isHeader = false;
			if (!line.empty() && ((line[0] >= '0' && line[0] <= '9') || (line[0] >= 'a' && line[0] <= 'f')))
			{
				char pr[8] = {}; unsigned long long x, y;
				if (sscanf(line.c_str(), "%llx-%llx %7s", &x, &y, pr) == 3 && line.find('-') < 17) { isHeader = true; a = x; b = y; perms = pr; }
			}
			if (isHeader)
			{
				// nome = ultimo campo (depois do inode) se existir
				std::string name;
				int fields = 0; size_t i = 0;
				while (i < line.size() && fields < 5) { while (i < line.size() && line[i] == ' ') i++; while (i < line.size() && line[i] != ' ') i++; fields++; }
				while (i < line.size() && line[i] == ' ') i++;
				if (i < line.size()) name = line.substr(i);
				std::string cat;
				if (name.find("kgsl") != std::string::npos || name.find("/dev/dri") != std::string::npos) cat = "GPU(kgsl)";
				else if (name.find("scudo") != std::string::npos || name.find("libc_malloc") != std::string::npos || name.find("[heap]") != std::string::npos) cat = "heap(malloc)";
				else if (name.find("dalvik") != std::string::npos || name.find(".art") != std::string::npos || name.find(".oat") != std::string::npos || name.find(".vdex") != std::string::npos || name.find(".odex") != std::string::npos) cat = "Java/ART";
				else if (name.find("memfd") != std::string::npos || name.find("ashmem") != std::string::npos || name.find("/dev/shm") != std::string::npos) cat = "memfd/shmem";
				else if (name.find(".so") != std::string::npos) cat = "bibliotecas(.so)";
				else if (name.find('/') != std::string::npos) cat = "outros arquivos";
				else if (name.find("[stack") != std::string::npos) cat = "pilhas";
				else if (perms.size() > 2 && perms[2] == 'x') cat = "codigo JIT(anonimo exec)";
				else cat = "anonima(outra)";
				cur = cat;
				// guarda o nome para o top
				r.top.push_back({ 0, fmt::format("{:x}-{:x} {} {} {}MB virtual", a, b, perms, name.empty() ? "[anonimo]" : name, (long)((b - a) / 1048576)) });
			}
			else if (line.compare(0, 4, "Rss:") == 0 && !cur.empty())
			{
				long kb = atol(line.c_str() + 4);
				r.kb[cur] += kb;
				if (!r.top.empty()) r.top.back().first = kb;
			}
		}
		std::sort(r.top.begin(), r.top.end(), [](auto& x, auto& y) { return x.first > y.first; });
		if (r.top.size() > 4) r.top.resize(4);
		return r;
	}

	// Tendencia de RSS (regressao linear sobre as ultimas amostras) - detector de vazamento
	struct LeakTrend
	{
		std::deque<std::pair<double, double>> samples; // (segundos, MB)
		double Slope(double windowSec, double nowSec) const // MB por minuto
		{
			double sx = 0, sy = 0, sxx = 0, sxy = 0; int n = 0;
			for (auto& s : samples) if (nowSec - s.first <= windowSec) { sx += s.first; sy += s.second; sxx += s.first * s.first; sxy += s.first * s.second; n++; }
			if (n < 6) return 0;
			double den = n * sxx - sx * sx;
			if (den == 0) return 0;
			return (n * sxy - sx * sy) / den * 60.0;
		}
	};

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
			CountThreads(), jitN, jitKb / 1024, temp, zone, batt > 0 ? batt / 10.0 : -1.0, gpuClk > 0 ? gpuClk / 1000000 : -1, gpuBusy, cpuFreq) + ExtraInfo();
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
			LeakTrend trend;
			const auto t0 = std::chrono::steady_clock::now();
			SmapsCats baseCats; bool haveBase = false;
			double lastPurge = -1000;
			int leakStrikes = 0;
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
				const double nowSec = std::chrono::duration<double>(now - t0).count();
				if (tick % 2 == 0) // a cada 10 s: detalhe de RAM + memoria Vulkan
					cemuLog_log(LogType::Force, "[Mem] {} | {}", ProcessMemDetail(), VkMemInfo());
				const std::string mem = ReadAll("/proc/meminfo");
				const long avail = KeyKb(mem, "MemAvailable:") / 1024;
				const long rss = KeyKb(ReadAll("/proc/self/status"), "VmRSS:") / 1024;
				if (avail >= 0 && avail < 400)
					cemuLog_log(LogType::Force, "[Stats] ALERTA: pouca RAM livre no sistema ({}MB) - o Android pode matar o app (OOM) ou a GPU pode falhar por falta de memoria", avail);
				if (lastRss >= 0 && rss - lastRss > 300)
					cemuLog_log(LogType::Force, "[Stats] ALERTA: o app cresceu {}MB de RAM em 5 s (agora {}MB) - possivel vazamento/alocacao descontrolada", rss - lastRss, rss);
				lastRss = rss;
				// ---- detector de vazamento: tendencia do RSS ----
				trend.samples.push_back({ nowSec, (double)rss });
				while (!trend.samples.empty() && nowSec - trend.samples.front().first > 600) trend.samples.pop_front();
				if (tick % 6 == 0) // a cada 30 s
				{
					const double s2 = trend.Slope(120, nowSec), s5 = trend.Slope(300, nowSec);
					cemuLog_log(LogType::Force, "[Mem] tendencia do RSS: {:+.1f}MB/min (ultimos 2 min) {:+.1f}MB/min (ultimos 5 min)", s2, s5);
					if (nowSec > 90 && s2 > 15.0 && s5 > 10.0) leakStrikes++; else leakStrikes = 0;
					if (leakStrikes >= 2)
						cemuLog_log(LogType::Force, "[Mem] ALERTA: POSSIVEL VAZAMENTO - RSS cresce de forma continua ({:+.1f}MB/min por mais de 1 min). Veja a quebra por categoria abaixo e o VKMEM/RECOMPILADOR(orfao) para achar a origem", s2);
					SmapsCats c = ReadSmaps();
					if (!haveBase && nowSec > 60) { baseCats = c; haveBase = true; }
					std::string cats;
					for (auto& kv : c.kb)
					{
						long delta = haveBase && baseCats.kb.count(kv.first) ? kv.second - baseCats.kb[kv.first] : 0;
						cats += fmt::format("{}{}={}MB({:+}MB)", cats.empty() ? "" : " | ", kv.first, kv.second / 1024, delta / 1024);
					}
					cemuLog_log(LogType::Force, "[Mem] por categoria (Rss, variacao desde ~60s): {}", cats);
					for (auto& t : c.top)
						cemuLog_log(LogType::Force, "[Mem]   maior mapeamento: {}MB Rss | {}", t.first / 1024, t.second);
				}
				// ---- devolver memoria livre do alocador ao sistema quando a RAM esta apertada (seguro) ----
				if (avail >= 0 && avail < 900 && nowSec - lastPurge > 30)
				{
#if defined(M_PURGE)
					lastPurge = nowSec;
					mallopt(M_PURGE, 0);
					const long rssAfter = KeyKb(ReadAll("/proc/self/status"), "VmRSS:") / 1024;
					cemuLog_log(LogType::Force, "[Mem] RAM do sistema baixa ({}MB livres): mallopt(M_PURGE) devolveu {}MB ao sistema (RSS {} -> {}MB)", avail, rss - rssAfter, rss, rssAfter);
#endif
				}
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
#if defined(__ANDROID__)
#define VKMEM_TRACK_ALLOC(h, size, type, kind) AndroidDiag::VkMemAlloc((uint64_t)(h), (uint64_t)(size), (uint32_t)(type), (int)(kind))
#define VKMEM_TRACK_FREE(h) AndroidDiag::VkMemFree((uint64_t)(h))
#else
#define VKMEM_TRACK_ALLOC(h, size, type, kind) ((void)0)
#define VKMEM_TRACK_FREE(h) ((void)0)
#endif
