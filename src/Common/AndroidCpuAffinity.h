#pragma once
#if defined(__ANDROID__)
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <sched.h>
#include <unistd.h>
#include <vector>
#include "Common/DiagnosticsFlags.h"
#include "Cemu/Logging/CemuLogging.h"

// Preferir os nucleos RAPIDOS da CPU para as threads principais do emulador (nucleos PPC e thread Latte/GPU).
// Motivo: o Android costuma comecar threads novas nos nucleos LENTOS (ex.: Cortex-A55 a ~1,1 GHz) e so migra para os rapidos depois de
// alguns segundos. Num emulador isso aparece como o primeiro video do jogo rodando a 2-3 FPS e subindo devagar. As threads criadas por
// uma thread ja restrita herdam a mascara, entao basta chamar isto no inicio das threads principais.
// Controlado por Diag::Flag::PreferBigCores (botao em Experimental > Logs e Diagnostico). So age se houver >= 4 nucleos rapidos
// DISTINTOS dos demais (aparelhos com poucos nucleos fortes nao sao restringidos).
namespace AndroidCpu
{
	// mascara de bits dos nucleos rapidos, ou 0 se nao ha o que escolher / nao foi possivel ler a frequencia
	inline uint64_t FastCoreMask()
	{
		static const uint64_t s_mask = []() -> uint64_t
		{
			const long n = sysconf(_SC_NPROCESSORS_CONF);
			if (n <= 1 || n > 64)
				return 0;
			std::vector<unsigned> maxFreq((size_t)n, 0);
			unsigned best = 0;
			for (long i = 0; i < n; i++)
			{
				char path[96];
				snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%ld/cpufreq/cpuinfo_max_freq", i);
				FILE* f = fopen(path, "r");
				if (!f)
					return 0;
				unsigned v = 0;
				if (fscanf(f, "%u", &v) != 1)
					v = 0;
				fclose(f);
				maxFreq[(size_t)i] = v;
				best = std::max(best, v);
			}
			if (best == 0)
				return 0;
			uint64_t mask = 0;
			long count = 0;
			for (long i = 0; i < n; i++)
			{
				if ((uint64_t)maxFreq[(size_t)i] * 100 >= (uint64_t)best * 75)
				{
					mask |= (1ull << i);
					count++;
				}
			}
			if (count < 4 || count == n)
			{
				cemuLog_log(LogType::Force, "[CPU] preferir nucleos rapidos: nada a fazer ({} de {} nucleos passam do limite de 75% da frequencia maxima; precisa de pelo menos 4 e de uma diferenca entre eles)", (sint32)count, (sint32)n);
				return 0;
			}
			cemuLog_log(LogType::Force, "[CPU] nucleos rapidos detectados: mascara 0x{:x} ({} de {} nucleos, frequencia maxima {} MHz)", mask, (sint32)count, (sint32)n, best / 1000);
			return mask;
		}();
		return s_mask;
	}

	// Threads AUXILIARES (compilacao do JIT, de shaders e de pipelines, caches em disco) devem poder usar TODOS os nucleos. Como a mascara e herdada
	// pelas threads filhas, quem e criado a partir de uma thread restrita nasce restrito; estas threads chamam isto para voltar a usar todos.
	// So tem efeito se a opcao "Preferir nucleos rapidos" estiver ligada e houver restricao a desfazer.
	inline void AllowAllCores()
	{
		if (!Diag::IsEnabled(Diag::Flag::PreferBigCores))
			return;
		if (FastCoreMask() == 0)
			return;
		const long n = sysconf(_SC_NPROCESSORS_CONF);
		if (n <= 0)
			return;
		cpu_set_t set;
		CPU_ZERO(&set);
		for (long i = 0; i < n && i < CPU_SETSIZE; i++)
			CPU_SET(i, &set);
		sched_setaffinity(0, sizeof(set), &set);
	}

	inline void PreferFastCores(const char* who)
	{
		if (!Diag::IsEnabled(Diag::Flag::PreferBigCores))
			return;
		const uint64_t mask = FastCoreMask();
		if (!mask)
			return;
		cpu_set_t set;
		CPU_ZERO(&set);
		for (int i = 0; i < 64; i++)
			if (mask & (1ull << i))
				CPU_SET(i, &set);
		static std::atomic<int> s_logged{0};
		const bool ok = sched_setaffinity(0, sizeof(set), &set) == 0; // pid 0 = a thread que chama
		if (s_logged.fetch_add(1) < 8)
			cemuLog_log(LogType::Force, "[CPU] thread \"{}\" fixada nos nucleos rapidos: {}", who, ok ? "OK" : "FALHOU");
	}
}
#endif
