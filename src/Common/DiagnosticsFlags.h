#pragma once
// Logs & Diagnostico (Experimental/Hackers > Logs e Diagnostico).
// Concentra num unico lugar as chaves de diagnostico do emulador, para nao ficarem espalhadas:
//   - GLDebug : log profundo do OpenGL ES (KHR_debug sincrono, todas as severidades, contexto EGL de depuracao)
//   - TexLog  : registra as ultimas texturas carregadas (formato/dimensao/endereco) e despeja no log em congelamento/crash
//   - AutoCleanOff : desliga a limpeza automatica da quarentena ao trocar de jogo (padrao = limpeza LIGADA)
// O estado persiste em arquivos-marcador na pasta de dados do usuario (mesmo mecanismo do "vk_sync_debug").
#include "config/ActiveSettings.h"
#include "Cemu/Logging/CemuLogging.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace Diag
{
	enum class Flag : int
	{
		GLDebug = 0,
		TexLog = 1,
		AutoCleanOff = 2,
		SustainedPerformance = 3,
		DumpShaders = 4,
		JitStrictOrdering = 5, // teste do JIT: dmb ish antes de cada load e depois de cada store (ver BackendAArch64.cpp)
		ThreadStartDelay = 6,  // teste de corrida: atrasa o inicio das threads secundarias do jogo (ver coreinit_Thread.cpp)
		GuestWatch = 7,        // vigia de memoria do jogo + sonda de tela/audio (ver coreinit_Thread.cpp, OpenGLRenderer.cpp, CubebAPI.cpp)
		AudioBoost = 8,        // ganho de audio x2 com limitador suave (ver audio/CubebAPI.cpp)
		PreferBigCores = 9,    // prende as threads principais do emulador nos nucleos rapidos da CPU (ver Common/AndroidCpuAffinity.h)
		ReduceGameMem = 10,    // informa ao jogo 128 MB a menos de MEM2 (ver coreinit_Memory.cpp: OSGetMemBound)
		ReduceMem64 = 11,      // idem, 64 MB a menos
		ReduceMem256 = 12,     // idem, 256 MB a menos
		ReduceMem384 = 13,     // idem, 384 MB a menos
		ReduceMem512 = 14,     // idem, 512 MB a menos
		// Bissecao do JIT (Logs e Diagnostico > "Teste do JIT"): so UMA delas fica ligada por vez; a faixa de codigo do jogo correspondente
		// roda no INTERPRETADOR em vez do JIT (ver JitBisectRange e PPCRecompiler.cpp).
		JitBisect1 = 15, // 02000000-07000000 (todo o codigo do jogo)
		JitBisect2 = 16, // 02030000-020A0000 (XML, strings e alocador)
		JitBisect3 = 17, // 02030000-02070000
		JitBisect4 = 18, // 02070000-020A0000
		JitBisect5 = 19, // 02030000-02050000
		JitBisect6 = 20, // 02050000-02070000
		JitBisect7 = 21, // 02070000-02080000
		JitBisect8 = 22, // 02080000-020A0000
		// REGRA: ao adicionar uma opcao, use o proximo numero e mantenha "Count" SEMPRE por ultimo.
		Count
	};

	inline const char* FlagMarkerName(Flag f)
	{
		switch (f)
		{
		case Flag::GLDebug: return "diag_gl_debug";
		case Flag::TexLog: return "diag_tex_log";
		case Flag::AutoCleanOff: return "diag_autoclean_off";
		case Flag::SustainedPerformance: return "diag_sustained_perf";
		case Flag::DumpShaders: return "diag_dump_shaders";
		case Flag::JitStrictOrdering: return "diag_jit_strict_order";
		case Flag::ThreadStartDelay: return "diag_thread_start_delay";
		case Flag::GuestWatch: return "diag_guest_watch";
		case Flag::AudioBoost: return "diag_audio_boost";
		case Flag::PreferBigCores: return "diag_prefer_big_cores";
		case Flag::ReduceGameMem: return "diag_reduce_game_mem";
		case Flag::ReduceMem64: return "diag_reduce_mem_64";
		case Flag::ReduceMem256: return "diag_reduce_mem_256";
		case Flag::ReduceMem384: return "diag_reduce_mem_384";
		case Flag::ReduceMem512: return "diag_reduce_mem_512";
		case Flag::JitBisect1: return "diag_jit_bisect_1";
		case Flag::JitBisect2: return "diag_jit_bisect_2";
		case Flag::JitBisect3: return "diag_jit_bisect_3";
		case Flag::JitBisect4: return "diag_jit_bisect_4";
		case Flag::JitBisect5: return "diag_jit_bisect_5";
		case Flag::JitBisect6: return "diag_jit_bisect_6";
		case Flag::JitBisect7: return "diag_jit_bisect_7";
		case Flag::JitBisect8: return "diag_jit_bisect_8";
		default: return "diag_unknown";
		}
	}

	// Faixa de codigo do jogo (inicio inclusivo, fim exclusivo) que a chave JitBisectN manda rodar no interpretador. false = nao e uma chave de bissecao.
	inline bool JitBisectRange(Flag f, uint32_t& lo, uint32_t& hi)
	{
		switch (f)
		{
		case Flag::JitBisect1: lo = 0x02000000; hi = 0x07000000; return true;
		case Flag::JitBisect2: lo = 0x02030000; hi = 0x020A0000; return true;
		case Flag::JitBisect3: lo = 0x02030000; hi = 0x02070000; return true;
		case Flag::JitBisect4: lo = 0x02070000; hi = 0x020A0000; return true;
		case Flag::JitBisect5: lo = 0x02030000; hi = 0x02050000; return true;
		case Flag::JitBisect6: lo = 0x02050000; hi = 0x02070000; return true;
		case Flag::JitBisect7: lo = 0x02070000; hi = 0x02080000; return true;
		case Flag::JitBisect8: lo = 0x02080000; hi = 0x020A0000; return true;
		default: return false;
		}
	}

	// Zerado por padrao (nao precisa contar valores iniciais): 0 = ainda nao lido, 1 = desligado, 2 = ligado
	inline std::atomic<int> g_flagState[(int)Flag::Count] = {};

	inline bool IsEnabled(Flag f)
	{
		int s = g_flagState[(int)f].load(std::memory_order_relaxed);
		if (s == 0)
		{
			std::error_code ec;
			s = std::filesystem::exists(ActiveSettings::GetUserDataPath(FlagMarkerName(f)), ec) ? 2 : 1;
			g_flagState[(int)f] = s;
		}
		return s == 2;
	}

	inline void SetEnabled(Flag f, bool on)
	{
		std::error_code ec;
		const auto marker = ActiveSettings::GetUserDataPath(FlagMarkerName(f));
		if (on)
			std::ofstream(marker, std::ios::trunc) << "1";
		else
			std::filesystem::remove(marker, ec);
		g_flagState[(int)f] = on ? 2 : 1;
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Quarentenas (vk_quarantine*.txt, gl_shader_quarantine.txt)
	// ---------------------------------------------------------------------------------------------------------------
	inline bool IsQuarantineFileName(const std::string& n)
	{
		return n.rfind("vk_quarantine", 0) == 0 || n == "gl_shader_quarantine.txt";
	}

	// apaga todos os arquivos de quarentena; devolve quantos foram apagados
	inline int DeleteQuarantineFiles()
	{
		int removed = 0;
		std::error_code ec;
		const auto dir = ActiveSettings::GetUserDataPath();
		for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
		{
			if (ec)
				break;
			if (!entry.is_regular_file(ec))
				continue;
			if (IsQuarantineFileName(entry.path().filename().string()))
			{
				std::error_code rec;
				if (std::filesystem::remove(entry.path(), rec))
					removed++;
			}
		}
		return removed;
	}

	// Chamado ao iniciar um jogo. A quarentena e protecao para o MESMO jogo na proxima abertura (um shader que travou a GPU e
	// pulado), entao NAO e apagada ao abrir o mesmo jogo de novo - isso desfaria a protecao. Ela so e limpa quando o jogo e
	// OUTRO (os hashes de shader sao especificos de cada jogo), a menos que o usuario tenha desligado a limpeza automatica.
	inline void OnTitleLaunch(uint64_t titleId)
	{
		// Qual variante nativa esta rodando? Uma build Debug (-O0) deixa o emulador MUITO mais lento (a decodificacao de texturas dos
		// logs levava ~9 us por bloco, 30-50x mais que o esperado). Aparece no log a cada abertura de jogo.
#if defined(__OPTIMIZE__)
		const char* optimized = "SIM (-O1 ou maior)";
#else
		const char* optimized = "NAO (-O0, build Debug: o emulador fica muito mais lento - instale a variante RELEASE)";
#endif
#if defined(NDEBUG)
		const char* ndebug = "NDEBUG definido (release)";
#else
		const char* ndebug = "NDEBUG NAO definido (debug: asserts ligados)";
#endif
		cemuLog_log(LogType::Force, "[Diag] build nativa otimizada: {} | {}", optimized, ndebug);
		const auto lastFile = ActiveSettings::GetUserDataPath("diag_last_title");
		std::string last;
		{
			std::ifstream f(lastFile);
			if (f)
				std::getline(f, last);
		}
		const std::string cur = fmt::format("{:016x}", titleId);
		if (last != cur)
		{
			if (!IsEnabled(Flag::AutoCleanOff))
			{
				int n = DeleteQuarantineFiles();
				if (n > 0)
					cemuLog_log(LogType::Force, "[Diag] jogo diferente do anterior ({} -> {}): {} arquivo(s) de quarentena apagado(s) automaticamente", last.empty() ? "nenhum" : last, cur, n);
			}
			std::ofstream(lastFile, std::ios::trunc) << cur;
		}
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Diagnostico de texturas: anel com as ultimas texturas carregadas na thread da GPU.
	// ---------------------------------------------------------------------------------------------------------------
	struct TexRec
	{
		uint64_t seq = 0;
		uint32_t physAddr = 0, physMipAddr = 0;
		uint32_t format = 0, tileMode = 0, dim = 0;
		uint32_t width = 0, height = 0, depth = 0, pitch = 0;
		uint32_t mip = 0, slice = 0, imageSize = 0;
		int64_t startUs = 0;
		std::atomic<int64_t> durUs{-1}; // -1 = ainda em andamento (o suspeito de um congelamento)
		std::atomic<int64_t> decodeUs{-1}; // tempo so da decodificacao/untile na CPU (-1 = ainda nao terminou)
	};

	constexpr int kTexRingSize = 64;
	inline TexRec g_texRing[kTexRingSize];
	inline std::atomic<uint64_t> g_texSeq{0};

	inline int64_t NowUs()
	{
		return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	// devolve o indice do registro (ou -1 se o diagnostico esta desligado)
	inline int TexBegin(uint32_t physAddr, uint32_t physMipAddr, uint32_t format, uint32_t dim, uint32_t w, uint32_t h, uint32_t depth,
						uint32_t pitch, uint32_t tileMode, uint32_t mip, uint32_t slice, uint32_t imageSize)
	{
		if (!IsEnabled(Flag::TexLog))
			return -1;
		const uint64_t seq = g_texSeq.fetch_add(1) + 1;
		const int idx = (int)(seq % kTexRingSize);
		TexRec& r = g_texRing[idx];
		r.durUs = -1;
		r.decodeUs = -1;
		r.seq = seq;
		r.physAddr = physAddr; r.physMipAddr = physMipAddr; r.format = format; r.dim = dim;
		r.width = w; r.height = h; r.depth = depth; r.pitch = pitch; r.tileMode = tileMode;
		r.mip = mip; r.slice = slice; r.imageSize = imageSize;
		r.startUs = NowUs();
		return idx;
	}

	inline void TexDecoded(int idx)
	{
		if (idx < 0)
			return;
		TexRec& r = g_texRing[idx];
		r.decodeUs = NowUs() - r.startUs;
	}

	inline void TexEnd(int idx)
	{
		if (idx < 0)
			return;
		TexRec& r = g_texRing[idx];
		const int64_t dur = NowUs() - r.startUs;
		r.durUs = dur;
		if (dur > 100000) // carga lenta (>100 ms): vira linha no log na hora
		{
			const int64_t dec = r.decodeUs.load();
			cemuLog_log(LogType::Force, "[TexDiag] carga LENTA {} ms (decodificar/untile na CPU: {} ms, envio p/ GPU: {} ms): addr={:08x} fmt=0x{:04x} dim={} {}x{}x{} mip={} slice={} tile={} pitch={} bytes={}",
						(long long)(dur / 1000), (long long)(dec / 1000), (long long)((dur - std::max<int64_t>(dec, 0)) / 1000),
						r.physAddr, r.format, r.dim, r.width, r.height, r.depth, r.mip, r.slice, r.tileMode, r.pitch, r.imageSize);
		}
	}

	// Despeja as ultimas texturas no log (mais antiga -> mais recente). Linhas "EM ANDAMENTO" = a textura em que a thread estava
	// quando o congelamento/crash foi detectado.
	inline void DumpRecentTextures(const char* reason)
	{
		if (!IsEnabled(Flag::TexLog))
			return;
		const uint64_t last = g_texSeq.load();
		if (last == 0)
		{
			cemuLog_log(LogType::Force, "[TexDiag] ({}) nenhuma textura registrada ainda", reason);
			return;
		}
		const uint64_t count = std::min<uint64_t>(last, 24);
		cemuLog_log(LogType::Force, "[TexDiag] ({}) ultimas {} texturas carregadas (total ate agora: {}):", reason, (unsigned long long)count, (unsigned long long)last);
		for (uint64_t s = last - count + 1; s <= last; s++)
		{
			const TexRec& r = g_texRing[s % kTexRingSize];
			if (r.seq != s)
				continue;
			const int64_t d = r.durUs.load();
			cemuLog_log(LogType::Force, "[TexDiag]  #{} addr={:08x} mipAddr={:08x} fmt=0x{:04x} dim={} {}x{}x{} mip={} slice={} tile={} pitch={} bytes={} {}",
						(unsigned long long)r.seq, r.physAddr, r.physMipAddr, r.format, r.dim, r.width, r.height, r.depth, r.mip, r.slice, r.tileMode,
						r.pitch, r.imageSize, d < 0 ? "<< EM ANDAMENTO" : fmt::format("{}us", (long long)d));
		}
	}
}
