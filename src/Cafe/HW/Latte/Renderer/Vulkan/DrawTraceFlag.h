#pragma once
// Chave do diagnostico Vulkan (sync por submit + rastreio de pipelines), ligada pelo botao em
// Experimental/Hackers. O estado persiste no arquivo-marcador "vk_sync_debug" (criado/removido pelo botao).
// O resultado vai para "vk_trace.txt" (pasta de dados do usuario), compartilhavel pelo botao "Compartilhar".
#include "config/ActiveSettings.h"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace DrawTraceVk
{
	inline std::atomic<int> g_syncState{-1}; // -1 = ainda nao lido
	inline std::mutex g_fileMutex;

	inline bool IsSyncEnabled()
	{
		int s = g_syncState.load();
		if (s < 0)
		{
			std::error_code ec;
			s = std::filesystem::exists(ActiveSettings::GetUserDataPath("vk_sync_debug"), ec) ? 1 : 0;
			g_syncState = s;
		}
		return s == 1;
	}

	inline void AppendFile(const std::string& line)
	{
		std::lock_guard<std::mutex> lk(g_fileMutex);
		std::ofstream f(ActiveSettings::GetUserDataPath("vk_trace.txt"), std::ios::app);
		if (f)
			f << line << "\n";
	}

	inline void SetSyncEnabled(bool on)
	{
		std::error_code ec;
		const auto marker = ActiveSettings::GetUserDataPath("vk_sync_debug");
		if (on)
		{
			{
				std::lock_guard<std::mutex> lk(g_fileMutex);
				std::ofstream(ActiveSettings::GetUserDataPath("vk_trace.txt"), std::ios::trunc) << "[VkTrace] diagnostico ligado\n";
				std::ofstream(marker, std::ios::trunc) << "1";
			}
		}
		else
			std::filesystem::remove(marker, ec);
		g_syncState = on ? 1 : 0;
	}
}
