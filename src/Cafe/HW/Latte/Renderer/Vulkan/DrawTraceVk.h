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
#include <unordered_map>
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
}
