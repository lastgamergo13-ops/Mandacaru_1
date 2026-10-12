#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"
#define VKFUNC_DEFINE
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"
#include <numeric> // for std::iota

#if BOOST_OS_LINUX || BOOST_OS_MACOS || BOOST_OS_BSD
#include <dlfcn.h>
#endif

#define VULKAN_API_CPU_BENCHMARK 0	// if 1, Cemu will log the CPU time spent per Vulkan API function

bool g_vulkan_available = false;

#if VULKAN_API_CPU_BENCHMARK != 0
uint64 s_vulkanBenchmarkLastResultsTime = 0;

struct VulkanBenchmarkFuncInfo
{
	std::string funcName;
	uint64 cycles;
	uint32 numCalls;
};

std::vector<VulkanBenchmarkFuncInfo*> s_vulkanBenchmarkFuncs;

template<typename TRet, typename... Args>
auto VkWrapperFuncGenTest(TRet (*func)(Args...), const char* name)
{
	static VulkanBenchmarkFuncInfo _FuncInfo;
	static auto _FuncPtrCopy = func;
	TRet (*newFunc)(Args...);
	if constexpr(std::is_void_v<TRet>)
	{
		newFunc = +[](Args... args) { uint64 t = __rdtsc(); _mm_mfence(); _FuncPtrCopy(args...); _mm_mfence(); _FuncInfo.cycles += (__rdtsc() - t); _FuncInfo.numCalls++; };
	}
	else
		newFunc = +[](Args... args) -> TRet { uint64 t = __rdtsc(); _mm_mfence(); TRet r = _FuncPtrCopy(args...); _mm_mfence(); _FuncInfo.cycles += (__rdtsc() - t); _FuncInfo.numCalls++; return r; };
	if(func && func != newFunc)
		_FuncPtrCopy = func;
	if(_FuncInfo.funcName.empty())
	{
		_FuncInfo = {.funcName = name, .cycles = 0, .numCalls = 0};
		s_vulkanBenchmarkFuncs.emplace_back(&_FuncInfo);
	}
	return newFunc;
};
#endif

// called when a TV SwapBuffers is called
void VulkanBenchmarkPrintResults()
{
#if VULKAN_API_CPU_BENCHMARK != 0
	// note: This could be done by hooking vk present functions
	uint64 currentCycle = __rdtsc();
	uint64 elapsedCycles = currentCycle - s_vulkanBenchmarkLastResultsTime;
	s_vulkanBenchmarkLastResultsTime = currentCycle;
	double elapsedCyclesDbl = (double)elapsedCycles;
	cemuLog_log(LogType::Force, "--- Vulkan API CPU benchmark ---");
	cemuLog_log(LogType::Force, "Elapsed cycles this frame: {:} | Current cycle {:} | NumFunc {:}", elapsedCycles, currentCycle, s_vulkanBenchmarkFuncs.size());

	// sum up total time of Vulkan calls
	uint64 totalVkCycles = 0;
	for (auto& it : s_vulkanBenchmarkFuncs)
	{
		totalVkCycles += it->cycles;
	}
	cemuLog_log(LogType::Force, "Total Vulkan time: {:.4}%", ((double)totalVkCycles / elapsedCyclesDbl) * 100.0);

	std::vector<sint32> sortedIndices(s_vulkanBenchmarkFuncs.size());
	std::iota(sortedIndices.begin(), sortedIndices.end(), 0);
	std::sort(sortedIndices.begin(), sortedIndices.end(),
			  [](int32_t a, int32_t b) {
				  return s_vulkanBenchmarkFuncs[a]->cycles > s_vulkanBenchmarkFuncs[b]->cycles;
			  });
	for (sint32 idx : sortedIndices)
	{
		auto& func = s_vulkanBenchmarkFuncs[idx];
		if(func->cycles == 0)
			return;
		cemuLog_log(LogType::Force, "{}: {} cycles ({:.4}%) {} calls", func->funcName.c_str(), func->cycles, ((double)func->cycles / elapsedCyclesDbl) * 100.0, func->numCalls);
		func->cycles = 0;
		func->numCalls = 0;
	}
#endif
}

#if BOOST_OS_WINDOWS

bool InitializeGlobalVulkan()
{
	const auto hmodule = LoadLibraryA("vulkan-1.dll");

	if(g_vulkan_available)
		return true;

	if (hmodule == nullptr)
	{
		cemuLog_log(LogType::Force, "Vulkan loader not available. Outdated graphics driver or Vulkan runtime not installed?");
		return false;
	}

	#define VKFUNC_INIT
	#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"

	if(!vkEnumerateInstanceVersion)
	{
		cemuLog_log(LogType::Force, "vkEnumerateInstanceVersion not available. Outdated graphics driver or Vulkan runtime?");
		FreeLibrary(hmodule);
		return false;
	}
	
	g_vulkan_available = true;
	return true;
}

bool InitializeInstanceVulkan(VkInstance instance)
{
	const auto hmodule = GetModuleHandleA("vulkan-1.dll");
	if (hmodule == nullptr)
		return false;

	#define VKFUNC_INSTANCE_INIT
	#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"
	
	return true;
}

bool InitializeDeviceVulkan(VkDevice device)
{
	const auto hmodule = GetModuleHandleA("vulkan-1.dll");
	if (hmodule == nullptr)
		return false;

	#define VKFUNC_DEVICE_INIT
	#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"

#if VULKAN_API_CPU_BENCHMARK != 0
	#define VKFUNC_DEFINE_CUSTOM(__func) __func = VkWrapperFuncGenTest(__func, #__func)
	#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"
#endif

	return true;
}

#else

#if BOOST_PLAT_ANDROID
#include <adrenotools/driver.h>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "config/CemuConfig.h"
#include "config/ActiveSettings.h"
#include "Cafe/GameProfile/GameProfile.h"
#include "Cemu/Logging/CemuLogging.h"

namespace
{
	// meta.json do driver: {"libraryName":"vulkan.ad07XX.so", ...}
	std::string ReadDriverLibraryName(const std::string& driverDir)
	{
		std::ifstream f(fs::path(driverDir) / "meta.json");
		if (!f.is_open())
			return {};
		std::stringstream ss;
		ss << f.rdbuf();
		const std::string json = ss.str();
		size_t k = json.find("\"libraryName\"");
		if (k == std::string::npos) return {};
		k = json.find(':', k);
		if (k == std::string::npos) return {};
		size_t q1 = json.find('"', k);
		if (q1 == std::string::npos) return {};
		size_t q2 = json.find('"', q1 + 1);
		if (q2 == std::string::npos) return {};
		return json.substr(q1 + 1, q2 - q1 - 1);
	}

	// Qual driver usar: perfil do jogo (Global/Sistema/Custom) senao a config global.
	std::string ResolveCustomDriverDir()
	{
		std::string path = GetConfig().custom_driver_path.GetValue();
		if (g_current_game_profile)
		{
			const auto setting = g_current_game_profile->GetDriverSetting();
			if (setting.mode == DriverSettingMode::System)
				return {};
			if (setting.mode == DriverSettingMode::Custom && setting.customPath.has_value())
				path = setting.customPath.value();
		}
		return path;
	}

	// Abre o libvulkan.so UMA vez por processo. Com driver customizado (Turnip etc.) usa a libadrenotools;
	// se algo falhar, cai no driver do sistema (e registra o motivo).
	void* OpenAndroidVulkanLoader()
	{
		static void* s_handle = nullptr;
		static bool s_tried = false;
		static std::string s_loadedDir;
		const std::string dir = ResolveCustomDriverDir();
		if (s_tried)
		{
			if (dir != s_loadedDir)
				cemuLog_log(LogType::Force, "[Driver] o driver Vulkan ja foi carregado neste processo ('{}'); a troca para '{}' so vale apos reiniciar o app", s_loadedDir.empty() ? "sistema" : s_loadedDir, dir.empty() ? "sistema" : dir);
			return s_handle;
		}
		s_tried = true;
		s_loadedDir = dir;

		if (!dir.empty())
		{
			const std::string libName = ReadDriverLibraryName(dir);
			if (libName.empty())
				cemuLog_log(LogType::Force, "[Driver] meta.json sem libraryName em '{}' - usando o driver do sistema", dir);
			else
			{
				// A pasta externa (/storage/emulated/0/Android/data/...) e montada SEM permissao de execucao:
				// o linker nao consegue carregar .so de la. Copiamos o driver para o armazenamento INTERNO
				// do app (code_cache) e usamos ele tambem como pasta temporaria da libadrenotools.
				std::string customDir = dir;
				std::string tmpDir;
				{
					std::error_code ec;
					// dataDir do app (interno, com permissao de execucao) - definido por CemuApplication.kt
					fs::path internalRoot = ActiveSettings::GetInternalDir();
					if (internalRoot.empty())
					{
						std::string pkg;
						std::ifstream cmd("/proc/self/cmdline", std::ios::binary);
						std::getline(cmd, pkg, '\0');
						internalRoot = fs::path("/data/data") / pkg;
					}
					const fs::path internalBase = internalRoot / "code_cache";
					fs::path internalDriver = internalBase / "cemu_custom_driver" / fs::path(dir).filename();
					fs::create_directories(internalDriver, ec);
					bool copyOk = !ec;
					if (ec)
						cemuLog_log(LogType::Force, "[Driver] nao consegui criar {}: {}", internalDriver.string(), ec.message());
					if (copyOk)
					{
						ec.clear();
						for (const auto& entry : fs::directory_iterator(dir, ec))
						{
							std::error_code e1;
							if (!entry.is_regular_file(e1)) continue;
							const fs::path dst = internalDriver / entry.path().filename();
							std::error_code e2;
							if (fs::exists(dst, e2) && fs::file_size(dst, e2) == entry.file_size(e1))
								continue; // ja copiado
							fs::copy_file(entry.path(), dst, fs::copy_options::overwrite_existing, e2);
							if (e2) { cemuLog_log(LogType::Force, "[Driver] falha ao copiar {} para o armazenamento interno: {}", entry.path().string(), e2.message()); copyOk = false; }
						}
						if (ec)
						{
							cemuLog_log(LogType::Force, "[Driver] nao consegui listar {}: {}", dir, ec.message());
							copyOk = false;
						}
					}
					if (copyOk)
					{
						customDir = internalDriver.string();
						cemuLog_log(LogType::Force, "[Driver] driver copiado para o armazenamento interno: {}", customDir);
					}
					else
						cemuLog_log(LogType::Force, "[Driver] AVISO: nao consegui copiar o driver para o armazenamento interno; tentando a pasta original (pode falhar por falta de permissao de execucao)");
					fs::path tmp = internalBase / "adrenotools_tmp";
					fs::create_directories(tmp, ec);
					tmpDir = tmp.string() + "/";
				}
				if (customDir.back() != '/') customDir += '/';
				const std::string hookDir = ActiveSettings::GetNativeLibPath().string() + "/";
				cemuLog_log(LogType::Force, "[Driver] carregando driver customizado '{}' (lib {}) hook={} tmp={}", customDir, libName, hookDir, tmpDir);
				for (const char* hookLib : { "libmain_hook.so", "libhook_impl.so", "libfile_redirect_hook.so" })
				{
					std::error_code hookEc;
					cemuLog_log(LogType::Force, "[Driver] {}: {}", hookLib, fs::exists(fs::path(hookDir) / hookLib, hookEc) ? "presente" : "AUSENTE (a libadrenotools nao foi empacotada com este lib)");
				}
				void* h = adrenotools_open_libvulkan(RTLD_NOW, ADRENOTOOLS_DRIVER_CUSTOM, tmpDir.c_str(), hookDir.c_str(), customDir.c_str(), libName.c_str(), nullptr, nullptr);
				if (h)
				{
					cemuLog_log(LogType::Force, "[Driver] driver customizado carregado com sucesso");
					s_handle = h;
					return s_handle;
				}
				const int savedErrno = errno;
				const char* dlErr = dlerror();
				cemuLog_log(LogType::Force, "[Driver] FALHA ao carregar o driver customizado (adrenotools_open_libvulkan devolveu nulo; errno={} '{}'; dlerror='{}') - usando o driver do sistema", savedErrno, strerror(savedErrno), dlErr ? dlErr : "");
			}
		}
		s_handle = dlopen("libvulkan.so", RTLD_NOW);
		return s_handle;
	}
}
#endif

void* dlopen_vulkan_loader()
{
#if BOOST_PLAT_ANDROID
	void* vulkan_so = OpenAndroidVulkanLoader();
#elif BOOST_OS_LINUX || BOOST_OS_BSD
	void* vulkan_so = dlopen("libvulkan.so", RTLD_NOW);
	if(!vulkan_so)
		vulkan_so = dlopen("libvulkan.so.1", RTLD_NOW);
#elif BOOST_OS_MACOS
	void* vulkan_so = dlopen("libMoltenVK.dylib", RTLD_NOW);
#endif
	return vulkan_so;
}

bool InitializeGlobalVulkan()
{
	void* vulkan_so = dlopen_vulkan_loader();

	if(g_vulkan_available)
		return true;

	if (!vulkan_so)
	{
		cemuLog_log(LogType::Force, "Vulkan loader not available.");
		return false;
	}

	#define VKFUNC_INIT
	#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"

	if(!vkEnumerateInstanceVersion)
	{
		cemuLog_log(LogType::Force, "vkEnumerateInstanceVersion not available. Outdated graphics driver or Vulkan runtime?");
		return false;
	}
	
	g_vulkan_available = true;
	return true;
}

bool InitializeInstanceVulkan(VkInstance instance)
{
	void* vulkan_so = dlopen_vulkan_loader();
	if (!vulkan_so)
		return false;

	#define VKFUNC_INSTANCE_INIT
	#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"
	
	return true;
}

bool InitializeDeviceVulkan(VkDevice device)
{
	void* vulkan_so = dlopen_vulkan_loader();
	if (!vulkan_so)
		return false;

	#define VKFUNC_DEVICE_INIT
	#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"

#if VULKAN_API_CPU_BENCHMARK != 0
	#define VKFUNC_DEFINE_CUSTOM(__func) __func = VkWrapperFuncGenTest(__func, #__func)
	#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"
#endif

	return true;
}

#endif
