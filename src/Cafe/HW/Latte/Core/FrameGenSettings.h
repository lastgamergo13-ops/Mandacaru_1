#pragma once
// Configuracoes EFETIVAS do FSR3 Frame Gen / FSR Upscale para o jogo em execucao:
// valor do perfil do jogo (se definido) senao o valor global da config.
// Barato (so leituras) - pode ser chamado todo frame.
#include "config/CemuConfig.h"
#include "Cafe/GameProfile/GameProfile.h"
#include <algorithm>

namespace FrameGenSettings
{
	struct Values
	{
		bool enabled = false;
		int multiplier = 2;        // 2..4
		int flowScalePercent = 100; // 25..100 (usado 50/75/100)
		bool performanceMode = false;
		int baseFpsLimit = 0;      // 0 = sem limite
		bool fsrUpscale = false;
		bool pyramid = false;      // Optical Flow multi-nivel (so global)
	};

	inline Values Get()
	{
		const auto& c = GetConfig();
		Values v;
		v.enabled = c.fsr3_frame_gen.GetValue();
		v.multiplier = c.fsr3_fg_multiplier.GetValue();
		v.flowScalePercent = c.fsr3_fg_flow_scale.GetValue();
		v.performanceMode = c.fsr3_fg_performance_mode.GetValue();
		v.baseFpsLimit = c.fsr3_fg_base_fps_limit.GetValue();
		v.fsrUpscale = c.fsr3_upscale.GetValue();
		v.pyramid = c.fsr3_fg_pyramid.GetValue();
		if (g_current_game_profile)
		{
			const auto& o = g_current_game_profile->GetFrameGenOverrides();
			if (o.enabled) v.enabled = *o.enabled;
			if (o.multiplier) v.multiplier = (int)*o.multiplier;
			if (o.flowScalePercent) v.flowScalePercent = (int)*o.flowScalePercent;
			if (o.performanceMode) v.performanceMode = *o.performanceMode;
			if (o.baseFpsLimit) v.baseFpsLimit = (int)*o.baseFpsLimit;
			if (o.fsrUpscale) v.fsrUpscale = *o.fsrUpscale;
		}
		v.multiplier = std::clamp(v.multiplier, 2, 4);
		v.flowScalePercent = std::clamp(v.flowScalePercent, 25, 100);
		if (v.performanceMode)
			v.flowScalePercent = std::min(v.flowScalePercent, 50); // modo performance: Optical Flow mais leve
		v.baseFpsLimit = std::clamp(v.baseFpsLimit, 0, 240);
		return v;
	}
}
