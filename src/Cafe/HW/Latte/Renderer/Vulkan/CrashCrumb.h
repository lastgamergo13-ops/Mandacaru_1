#pragma once
#include <atomic>
// "Migalha de pao" para diagnostico de crash: cada thread guarda um ponteiro para um texto fixo que diz
// qual chamada Vulkan/etapa estava rodando. O tratador de sinais imprime isso no crash, o que identifica
// QUAL chamada derrubou o driver mesmo quando o backtrace so mostra enderecos dentro do .so do driver.
// Custo: uma escrita de ponteiro em thread_local (sem alocacao, seguro dentro de signal handler).
namespace CrashCrumb
{
	inline thread_local const char* g_text = nullptr;
	inline thread_local unsigned long long g_count = 0;

	// Quarentena automatica de pipeline que derruba o DRIVER (SIGSEGV dentro de vkCreateGraphicsPipelines, ex.: PowerVR/MediaTek).
	// Cada thread guarda os hashes (VS/PS/GS) do pipeline que esta criando agora. Se o app tomar um sinal fatal nessa thread,
	// o tratador de sinais grava esses hashes direto em vk_quarantine.txt (so open/write/close, sem alocar) e, na proxima
	// abertura, esse pipeline e PULADO em vez de derrubar o app de novo.
	inline thread_local bool g_pipeActive = false;
	inline thread_local unsigned long long g_pipeVs = 0, g_pipePs = 0, g_pipeGs = 0;
	// caminho completo de vk_quarantine.txt (preenchido uma vez, em DrawTraceVk::LoadQuarantine)
	inline char g_quarantinePath[1024] = {0};
	// V11.54: caminho do cache de pipelines do driver; o tratador de sinais o apaga (unlink) se o crash foi DENTRO de vkCreateGraphicsPipelines
	inline char g_pipelineCachePath[1024] = {0};
	// Gancho chamado pelo tratador de sinais para gravar o vk_trace.txt (ultimos pipelines enviados a GPU) quando o app crasha por
	// SIGSEGV/SIGABRT dentro do driver (ex.: Turnip no vkQueueSubmit). Registrado por DrawTraceVk. Antes o vk_trace so era gravado no
	// DEVICE_LOST e so com o "Vulkan diagnostic log" ligado, entao muitos crashes ficavam sem o arquivo.
	inline void (*g_vkCrashDumpHook)() = nullptr;
	// identifica GPU+driver (vendorID/driverVersion), preenchido em VulkanRenderer ao detectar o dispositivo. A quarentena e
	// gravada POR DRIVER: um pipeline que travou o driver da Qualcomm nao deve ser pulado ao trocar para o Turnip (nos logs, com o
	// Turnip o jogo rodava com 4 draws/frame porque herdava a quarentena do driver do sistema).
	inline char g_driverKey[40] = {0};

	// Mesma ideia para o OpenGL ES: SIGSEGV dentro do compilador de shader do driver (ex.: libllvm-glnext.so da Adreno no Mario Kart 8).
	// Hashes (base, aux, tipo) do shader que esta sendo compilado/linkado agora; gravados em gl_shader_quarantine.txt no crash.
	inline thread_local bool g_glShaderActive = false;
	inline thread_local unsigned long long g_glBase = 0, g_glAux = 0, g_glType = 0;
	inline char g_glQuarantinePath[1024] = {0};

	// Visao GLOBAL do shader em compilacao (para o watchdog, que roda em OUTRA thread e nao enxerga os thread_local acima).
	// Se a thread da GPU ficar presa >=27 s dentro do compilador do driver (PowerVR libusc.so, Adreno libllvm-glnext.so), o
	// watchdog grava esse shader em gl_shader_quarantine.txt: ele passa a ser PULADO nas proximas aberturas.
	inline std::atomic<bool> g_glCompileActiveGlobal{false};
	inline std::atomic<unsigned long long> g_glCompileBaseGlobal{0}, g_glCompileAuxGlobal{0}, g_glCompileTypeGlobal{0};
}
#define CRUMB(s) do { CrashCrumb::g_text = (s); CrashCrumb::g_count++; } while (0)
#define PIPE_CREATE_BEGIN(vs, ps, gs) do { CrashCrumb::g_pipeVs = (vs); CrashCrumb::g_pipePs = (ps); CrashCrumb::g_pipeGs = (gs); CrashCrumb::g_pipeActive = true; } while (0)
#define PIPE_CREATE_END() do { CrashCrumb::g_pipeActive = false; } while (0)
