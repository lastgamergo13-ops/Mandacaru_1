#pragma once
// Rastreio de pipelines por submit (diagnostico de VK_ERROR_DEVICE_LOST em drivers de fabrica).
// Cada troca de pipeline registra (submit, hashes de VS/PS/GS, primitiva). Quando a GPU trava o log
// mostra quais shaders estavam nos ultimos submits - e, no modo de sincronizacao (arquivo
// "vk_sync_debug" na pasta de dados do usuario), exatamente os do submit que travou.
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanRenderer.h"
#include "Cafe/HW/Latte/LegacyShaderDecompiler/LatteDecompiler.h"
#include "Cemu/Logging/CemuLogging.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/DrawTraceFlag.h"
#include <fmt/format.h>
#include <cstdint>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>
#include <sstream>
#include <atomic>
#include <vector>

namespace DrawTraceVk
{
	struct Rec
	{
		uint64_t submit;
		uint64_t vs, ps, gs;
		uint32_t prim;
		uint8_t rect;
	};
	constexpr uint32_t kRing = 4096;
	inline Rec g_ring[kRing];
	inline uint32_t g_pos = 0;

	inline void Add(uint64_t submit, const PipelineInfo* p)
	{
		if (!p)
			return;
		Rec& r = g_ring[g_pos++ % kRing];
		r.submit = submit;
		r.vs = p->vertexShader ? p->vertexShader->baseHash : 0;
		r.ps = p->pixelShader ? p->pixelShader->baseHash : 0;
		r.gs = p->geometryShader ? p->geometryShader->baseHash : 0;
		r.prim = (uint32_t)p->primitiveMode;
		r.rect = p->rectEmulationGS ? 1 : 0;
	}

	template<typename... Args>
	inline void Log(fmt::format_string<Args...> f, Args&&... a)
	{
		const std::string m = fmt::format(f, std::forward<Args>(a)...);
		cemuLog_log(LogType::Force, "{}", m);
		if (IsSyncEnabled())
			AppendFile(m);
	}

	// lista os pipelines unicos dos submits [fromSubmit, toSubmit]
	inline void Dump(uint64_t fromSubmit, uint64_t toSubmit)
	{
		struct Key { uint64_t s, vs, ps, gs; uint32_t prim; uint8_t rect; };
		std::vector<Key> keys;
		std::vector<int> counts;
		const uint32_t total = g_pos < kRing ? g_pos : kRing;
		for (uint32_t i = 0; i < total; i++)
		{
			const Rec& r = g_ring[(g_pos - total + i) % kRing];
			if (r.submit < fromSubmit || r.submit > toSubmit)
				continue;
			bool found = false;
			for (size_t k = 0; k < keys.size(); k++)
			{
				if (keys[k].s == r.submit && keys[k].vs == r.vs && keys[k].ps == r.ps && keys[k].gs == r.gs && keys[k].prim == r.prim && keys[k].rect == r.rect)
				{
					counts[k]++;
					found = true;
					break;
				}
			}
			if (!found)
			{
				keys.push_back({ r.submit, r.vs, r.ps, r.gs, r.prim, r.rect });
				counts.push_back(1);
			}
		}
		Log("[VkTrace] pipelines dos submits {}..{} ({} unicos):", fromSubmit, toSubmit, (uint32_t)keys.size());
		int lines = 0;
		for (size_t k = 0; k < keys.size() && lines < 120; k++, lines++)
			Log("[VkTrace] submit={} VS={:016x} PS={:016x} GS={:016x} prim={} rects={} x{}", keys[k].s, keys[k].vs, keys[k].ps, keys[k].gs, keys[k].prim, (int)keys[k].rect, counts[k]);
	}

	// ---- Quarentena automatica (V11.32) ----
	// Quando a GPU trava (DEVICE_LOST), os pipelines que apareceram pela PRIMEIRA vez nos ultimos submits sao os
	// suspeitos. Eles sao gravados em "vk_quarantine.txt" (pasta de dados do usuario). Na proxima abertura os draws
	// desses pipelines sao PULADOS (o jogo pode perder um efeito, mas nao trava). Apague o arquivo para desfazer.
	inline std::unordered_set<uint64_t> g_quarantine;
	inline std::atomic<bool> g_quarantineActive{false};
	inline bool g_quarantineLoaded = false;

	inline uint64_t PipelineKey(uint64_t vs, uint64_t ps, uint64_t gs)
	{
		uint64_t h = vs * 0x9E3779B97F4A7C15ull;
		h ^= (ps + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2));
		h ^= (gs + 0x7F4A7C159E3779B9ull + (h << 6) + (h >> 2));
		return h;
	}

	inline void LoadQuarantine()
	{
		if (g_quarantineLoaded)
			return;
		g_quarantineLoaded = true;
		std::error_code ec;
		std::ifstream f(ActiveSettings::GetUserDataPath("vk_quarantine.txt"));
		if (!f)
			return;
		std::string line;
		while (std::getline(f, line))
		{
			unsigned long long vs = 0, ps = 0, gs = 0;
			if (sscanf(line.c_str(), "%llx %llx %llx", &vs, &ps, &gs) == 3)
				g_quarantine.insert(PipelineKey(vs, ps, gs));
		}
		g_quarantineActive = !g_quarantine.empty();
		if (g_quarantineActive)
			cemuLog_log(LogType::Force, "[Quarentena] {} pipeline(s) suspeito(s) de travar a GPU serao PULADOS (vk_quarantine.txt). Apague o arquivo para desativar.", (uint32_t)g_quarantine.size());
	}

	inline bool ShouldSkip(const PipelineInfo* p)
	{
		if (!g_quarantineActive.load(std::memory_order_relaxed) || !p)
			return false;
		const uint64_t vs = p->vertexShader ? p->vertexShader->baseHash : 0;
		const uint64_t ps = p->pixelShader ? p->pixelShader->baseHash : 0;
		const uint64_t gs = p->geometryShader ? p->geometryShader->baseHash : 0;
		return g_quarantine.count(PipelineKey(vs, ps, gs)) != 0;
	}

	// chamado no DEVICE_LOST: grava como suspeitos os pipelines vistos pela 1a vez nos ultimos 12 submits
	inline void RecordSuspects(uint64_t cur)
	{
		const uint64_t from = cur > 12 ? cur - 12 : 0; // V11.43: era 3; a GPU trava varios submits depois do pipeline culpado (log SM3DW: 1o uso no submit 1075, travou no 1081)
		const uint32_t total = g_pos < kRing ? g_pos : kRing;
		std::unordered_set<uint64_t> old, suspects;
		std::vector<Rec> sus;
		for (uint32_t i = 0; i < total; i++)
		{
			const Rec& r = g_ring[(g_pos - total + i) % kRing];
			const uint64_t k = PipelineKey(r.vs, r.ps, r.gs);
			if (r.submit < from)
				old.insert(k);
		}
		for (uint32_t i = 0; i < total; i++)
		{
			const Rec& r = g_ring[(g_pos - total + i) % kRing];
			if (r.submit < from || r.submit > cur)
				continue;
			const uint64_t k = PipelineKey(r.vs, r.ps, r.gs);
			if (old.count(k) || suspects.count(k))
				continue;
			suspects.insert(k);
			sus.push_back(r);
		}
		if (sus.empty())
		{
			// V11.47: nenhum pipeline era novo (ex.: Mario Kart 8 na Adreno, travou no submit 133 com todos os pipelines ja
			// vistos). Em vez de nao gravar nada e travar de novo igual, poe em quarentena os pipelines dos 2 ultimos submits.
			// V11.47b: os ultimos submits podem nao ter draws (MK8: draws no submit 119, travou no 124). Usa o ultimo
			// submit que TEM draws (e o anterior).
			uint64_t lastDrawSubmit = 0;
			for (uint32_t i = 0; i < total; i++)
			{
				const Rec& r = g_ring[(g_pos - total + i) % kRing];
				if (r.submit <= cur && r.submit > lastDrawSubmit)
					lastDrawSubmit = r.submit;
			}
			const uint64_t from2 = lastDrawSubmit > 1 ? lastDrawSubmit - 1 : 0;
			std::unordered_set<uint64_t> seen;
			for (uint32_t i = 0; i < total; i++)
			{
				const Rec& r = g_ring[(g_pos - total + i) % kRing];
				if (r.submit < from2 || r.submit > lastDrawSubmit)
					continue;
				if (!seen.insert(PipelineKey(r.vs, r.ps, r.gs)).second)
					continue;
				sus.push_back(r);
			}
			cemuLog_log(LogType::Force, "[Quarentena] nenhum pipeline novo nos ultimos submits; usando os {} pipeline(s) dos 2 ultimos submits", (uint32_t)sus.size());
		}
		if (sus.empty())
		{
			cemuLog_log(LogType::Force, "[Quarentena] nenhum pipeline registrado nos ultimos submits; nada gravado");
			return;
		}
		std::lock_guard<std::mutex> lk(g_fileMutex);
		std::ofstream f(ActiveSettings::GetUserDataPath("vk_quarantine.txt"), std::ios::app);
		for (const Rec& r : sus)
		{
			f << fmt::format("{:016x} {:016x} {:016x}\n", r.vs, r.ps, r.gs);
			cemuLog_log(LogType::Force, "[Quarentena] suspeito gravado: VS={:016x} PS={:016x} GS={:016x} prim={} (sera pulado na proxima abertura)", r.vs, r.ps, r.gs, r.prim);
		}
	}
}
