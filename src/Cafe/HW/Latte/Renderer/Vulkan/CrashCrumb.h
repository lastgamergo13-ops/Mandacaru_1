#pragma once
// "Migalha de pao" para diagnostico de crash: cada thread guarda um ponteiro para um texto fixo que diz
// qual chamada Vulkan/etapa estava rodando. O tratador de sinais imprime isso no crash, o que identifica
// QUAL chamada derrubou o driver mesmo quando o backtrace so mostra enderecos dentro do .so do driver.
// Custo: uma escrita de ponteiro em thread_local (sem alocacao, seguro dentro de signal handler).
namespace CrashCrumb
{
	inline thread_local const char* g_text = nullptr;
	inline thread_local unsigned long long g_count = 0;
}
#define CRUMB(s) do { CrashCrumb::g_text = (s); CrashCrumb::g_count++; } while (0)
