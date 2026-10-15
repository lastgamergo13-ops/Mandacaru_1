#pragma once
// Log periodico "[Perf]" para diagnosticar lentidao (ex.: personagem em camera lenta mesmo com FPS "estavel").
// Mostra: FPS real, velocidade emulada (ciclos do PPC/s vs 1243 MHz do Espresso), uso de CPU por thread
// (qual thread esta saturada: GPU/Latte ou nucleos PPC), tempos da thread de GPU e configuracoes que limitam o FPS.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>
#if defined(__ANDROID__) || defined(__linux__)
#include <dirent.h>
#include <unistd.h>
#endif

namespace PerfLog
{
	struct ThreadSample { std::string name; uint64_t ticks; };

	inline std::string ThreadTop(double wallSeconds)
	{
#if defined(__ANDROID__) || defined(__linux__)
		static std::unordered_map<int, uint64_t> s_prev;
		const long hz = sysconf(_SC_CLK_TCK);
		std::vector<std::pair<double, std::string>> rows;
		DIR* d = opendir("/proc/self/task");
		if (!d)
			return {};
		while (dirent* e = readdir(d))
		{
			const int tid = atoi(e->d_name);
			if (tid <= 0)
				continue;
			char path[64];
			snprintf(path, sizeof(path), "/proc/self/task/%d/stat", tid);
			FILE* f = fopen(path, "r");
			if (!f)
				continue;
			char buf[512] = {};
			size_t n = fread(buf, 1, sizeof(buf) - 1, f);
			fclose(f);
			buf[n] = 0;
			char* lp = strchr(buf, '(');
			char* rp = strrchr(buf, ')');
			if (!lp || !rp)
				continue;
			std::string name(lp + 1, rp - lp - 1);
			unsigned long long ut = 0, st = 0;
			// campos apos ")": state ppid pgrp session tty tpgid flags minflt cminflt majflt cmajflt utime stime
			if (sscanf(rp + 2, "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %llu %llu", &ut, &st) != 2)
				continue;
			const uint64_t total = ut + st;
			auto it = s_prev.find(tid);
			if (it != s_prev.end() && wallSeconds > 0.0)
			{
				const double pct = (double)(total - it->second) * 100.0 / ((double)hz * wallSeconds);
				if (pct >= 5.0)
					rows.push_back({ pct, name + "#" + std::to_string(tid) });
			}
			s_prev[tid] = total;
		}
		closedir(d);
		std::sort(rows.begin(), rows.end(), [](auto& a, auto& b) { return a.first > b.first; });
		std::string out;
		for (size_t i = 0; i < rows.size() && i < 8; i++)
		{
			char tmp[96];
			snprintf(tmp, sizeof(tmp), "%s=%.0f%% ", rows[i].second.c_str(), rows[i].first);
			out += tmp;
		}
		return out;
#else
		return {};
#endif
	}
}
