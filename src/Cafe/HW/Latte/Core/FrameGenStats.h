#pragma once
// Estatisticas do Frame Generation, lidas pelo overlay (LatteOverlay.cpp) e escritas por
// dependencies/ffx-frame-generation/src/swapchain_vk.cpp. So atomics - sem dependencias.
#include <atomic>

namespace FrameGenStats
{
	inline std::atomic<bool> active{ false };      // frame gen ligado e funcionando
	inline std::atomic<float> realFps{ 0.0f };      // frames reais do jogo por segundo
	inline std::atomic<float> generatedFps{ 0.0f }; // frames gerados (interpolados) por segundo
}
