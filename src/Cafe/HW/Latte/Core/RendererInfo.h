#pragma once
// Info do renderer mostrada no overlay de FPS: API em uso e driver (do sistema ou customizado).
#include <mutex>
#include <string>

namespace RendererInfo
{
	inline std::mutex g_mutex;
	inline std::string g_api;          // "Vulkan" / "OpenGL ES"
	inline std::string g_driver;       // nome do driver reportado pela GPU/driver
	inline std::string g_customName;   // nome (meta.json) do driver customizado carregado; vazio = driver do sistema

	inline void Set(const std::string& api, const std::string& driver)
	{
		std::lock_guard<std::mutex> lk(g_mutex);
		g_api = api;
		g_driver = driver;
	}
	inline void SetCustomName(const std::string& n)
	{
		std::lock_guard<std::mutex> lk(g_mutex);
		g_customName = n;
	}
	inline void Get(std::string& api, std::string& driver, std::string& custom)
	{
		std::lock_guard<std::mutex> lk(g_mutex);
		api = g_api;
		driver = g_driver;
		custom = g_customName;
	}
}
