#include "util/SystemInfo/SystemInfo.h"

#include <unistd.h>
#include <dirent.h>
#include <time.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>

uint64 QueryRamUsage()
{
	static long page_size = sysconf(_SC_PAGESIZE);
	if (page_size == -1)
	{
		return 0;
	}

	std::ifstream file("/proc/self/statm");
	if (file)
	{
		file.ignore(std::numeric_limits<std::streamsize>::max(), ' ');
		uint64 pages;
		file >> pages;

		return pages * page_size;
	}
	return 0;
}

namespace
{
	// Le /proc/stat (uso do SISTEMA por nucleo). Devolve false se nao deu para ler ou se a leitura veio vazia.
	bool QueryCoreTimesFromProcStat(uint32 count, std::vector<ProcessorTime>& out)
	{
		std::ifstream file("/proc/stat");
		if (!file)
			return false;
		std::string line;
		std::getline(file, line); // linha agregada "cpu ..."
		bool anyData = false;
		for (uint32 i = 0; i < count && i < out.size(); ++i)
		{
			if (!std::getline(file, line))
				break;
			unsigned long long user = 0, nice = 0, kernel = 0, idle = 0;
			if (line.compare(0, 3, "cpu") != 0 || sscanf(line.c_str(), "%*s %llu %llu %llu %llu", &user, &nice, &kernel, &idle) != 4)
				continue;
			out[i].idle = idle;
			out[i].kernel = kernel;
			out[i].user = user + nice;
			if (user + nice + kernel + idle > 0)
				anyData = true;
		}
		return anyData;
	}

	// Android 8+ BLOQUEIA o /proc/stat para apps (SELinux): a leitura falha ou vem vazia, e o overlay mostrava "nan%"
	// (0/0). Alternativa que sempre funciona: o tempo de CPU das PROPRIAS threads do Cemu (/proc/self/task/<tid>/stat), atribuido
	// ao ultimo nucleo em que cada thread rodou (campo 39 "processor"). O resultado e o uso do Cemu por nucleo, nao do sistema.
	struct OwnThreadCoreSampler
	{
		std::mutex mutex;
		std::unordered_map<int, uint64> lastThreadNs; // tid -> tempo de CPU acumulado (ns) na ultima leitura
		std::vector<uint64> perCoreNs;                // tempo de CPU do Cemu acumulado por nucleo (ns)
		uint64 startNs = 0;
	};

	uint64 NowNs()
	{
		timespec ts{};
		clock_gettime(CLOCK_MONOTONIC, &ts);
		return (uint64)ts.tv_sec * 1000000000ull + (uint64)ts.tv_nsec;
	}

	void QueryCoreTimesFromOwnThreads(uint32 count, std::vector<ProcessorTime>& out)
	{
		static OwnThreadCoreSampler s;
		static const double nsPerTick = 1e9 / (double)std::max<long>(1, sysconf(_SC_CLK_TCK));
		std::lock_guard<std::mutex> lock(s.mutex);
		const uint64 now = NowNs();
		if (s.startNs == 0)
			s.startNs = now;
		if (s.perCoreNs.size() < count)
			s.perCoreNs.resize(count, 0);

		if (DIR* dir = opendir("/proc/self/task"))
		{
			std::unordered_map<int, uint64> seen;
			while (dirent* entry = readdir(dir))
			{
				const int tid = atoi(entry->d_name);
				if (tid <= 0)
					continue;
				char path[64];
				snprintf(path, sizeof(path), "/proc/self/task/%d/stat", tid);
				std::ifstream f(path);
				std::string stat;
				if (!f || !std::getline(f, stat))
					continue;
				const size_t close = stat.rfind(')'); // o nome da thread pode ter espacos e parenteses
				if (close == std::string::npos)
					continue;
				// apos ')': campo 3 (state) e o 1o token; utime = campo 14, stime = campo 15, processor = campo 39
				std::istringstream rest(stat.substr(close + 1));
				std::string tok;
				unsigned long long utime = 0, stime = 0;
				int processor = -1;
				for (int field = 3; rest >> tok; ++field)
				{
					if (field == 14) utime = strtoull(tok.c_str(), nullptr, 10);
					else if (field == 15) stime = strtoull(tok.c_str(), nullptr, 10);
					else if (field == 39) { processor = atoi(tok.c_str()); break; }
				}
				const uint64 threadNs = (uint64)((double)(utime + stime) * nsPerTick);
				auto it = s.lastThreadNs.find(tid);
				if (it != s.lastThreadNs.end() && threadNs >= it->second && processor >= 0 && (uint32)processor < s.perCoreNs.size())
					s.perCoreNs[processor] += threadNs - it->second;
				seen[tid] = threadNs; // thread nova: so serve de base (evita um pico falso na primeira leitura)
			}
			closedir(dir);
			s.lastThreadNs.swap(seen); // threads que terminaram saem do mapa
		}

		const uint64 elapsed = now - s.startNs;
		for (uint32 i = 0; i < count && i < out.size(); ++i)
		{
			const uint64 busy = std::min<uint64>(s.perCoreNs[i], elapsed);
			out[i].user = busy;
			out[i].kernel = 0;
			out[i].idle = elapsed - busy;
		}
	}
}

void QueryCoreTimes(uint32 count, std::vector<ProcessorTime>& out)
{
	if (QueryCoreTimesFromProcStat(count, out))
		return;
	QueryCoreTimesFromOwnThreads(count, out);
}
