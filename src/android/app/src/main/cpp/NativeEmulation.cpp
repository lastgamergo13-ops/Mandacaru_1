#include "AndroidFilesystemCallbacks.h"
#include "AndroidInputHelpers.h"
#include "AndroidMemoryManager.h"
#include "AndroidOpenGLCanvas.h"
#include "Cafe/CafeSystem.h"
#include "Cafe/GameProfile/GameProfile.h"
#include "Cafe/HW/Latte/Core/LatteOverlay.h"
#include "Cafe/HW/Latte/Renderer/OpenGL/OpenGLRenderer.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanRenderer.h"
#include "Cafe/OS/libs/snd_core/ax.h"
#include "Cafe/TitleList/TitleId.h"
#include "Cafe/TitleList/TitleList.h"
#include "Cemu/ncrypto/ncrypto.h"
#include "config/ActiveSettings.h"
#include "config/CemuConfig.h"
#include "input/InputManager.h"
#include "JNIUtils.h"
#include "WindowSystem.h"

#if HAS_CUBEB
#include "audio/CubebAPI.h"
#endif // HAS_CUBEB

#include <algorithm>
#include <vector>
#include <stdexcept>

#include <android/native_window_jni.h>

// forward declaration from main.cpp
void CemuCommonInit();

namespace NativeEmulation
{

	// adrenotools only supports hooking Qualcomm's Adreno Vulkan driver, so custom driver
	// loading is only offered on devices that report an Adreno GPU
	bool SupportsLoadingCustomDriver()
	{
		// mostra a opção para qualquer aparelho, independente do fabricante da
		// GPU (Adreno/Mali/Exynos/etc) - quem decide se tenta usar um driver
		// customizado é o usuário
		return true;
	}

	void CreateAudioDevice(IAudioAPI::AudioAPI audioApi, AudioChannels channels, sint32 volume, bool isTV)
	{
		constexpr int AX_FRAMES_PER_GROUP = 4;
		std::unique_lock lock(g_audioMutex);
		auto& audioDevice = isTV ? g_tvAudio : g_padAudio;

		switch (audioApi)
		{
#if HAS_CUBEB
		case IAudioAPI::AudioAPI::Cubeb:
		{
			audioDevice.reset();
			auto deviceDescriptionPtr = std::make_shared<CubebAPI::CubebDeviceDescription>(nullptr, std::string(), std::wstring());
			audioDevice = IAudioAPI::CreateDevice(
				IAudioAPI::AudioAPI::Cubeb,
				deviceDescriptionPtr,
				48000,
				CemuConfig::AudioChannelsToNChannels(channels),
				snd_core::AX_SAMPLES_PER_3MS_48KHZ * AX_FRAMES_PER_GROUP,
				16);
			audioDevice->SetVolume(volume);
			break;
		}
#endif // HAS_CUBEB
		default:
			cemuLog_log(LogType::Force, "Invalid audio api: {}", audioApi);
			break;
		}
	}

	void InitializeAudioDevices()
	{
		auto& config = GetConfig();
		if (!config.tv_device.empty())
			CreateAudioDevice(IAudioAPI::AudioAPI::Cubeb, config.tv_channels, config.tv_volume, true);

		if (!config.pad_device.empty())
			CreateAudioDevice(IAudioAPI::AudioAPI::Cubeb, config.pad_channels, config.pad_volume, false);
	}

	void SetDefaultDeviceController()
	{
		for (size_t i = 0; i < InputManager::kMaxController; ++i)
		{
			auto emulatedController = InputManager::instance().get_controller(i);

			if (!emulatedController)
			{
				continue;
			}

			for (const auto& controller : emulatedController->get_controllers())
			{
				if (controller->api() == InputAPI::Device)
				{
					return;
				}
			}
		}

		if (auto emulatedController = InputManager::instance().get_controller(0))
		{
			auto controller = CreateDefaultDeviceController();
			emulatedController->add_controller(controller);
		}
	}

	void CreateCemuDirectories()
	{
		const auto mlc = ActiveSettings::GetMlcPath();

		// create sys/usr folder in mlc01
		const auto sysFolder = mlc / "sys";
		fs::create_directories(sysFolder);

		const auto usrFolder = mlc / "usr";
		fs::create_directories(usrFolder);
		fs::create_directories(usrFolder / "title/00050000"); // base
		fs::create_directories(usrFolder / "title/0005000c"); // dlc
		fs::create_directories(usrFolder / "title/0005000e"); // update

		// Mii Maker save folders {0x500101004A000, 0x500101004A100, 0x500101004A200},
		fs::create_directories(mlc / "usr/save/00050010/1004a100/user/common/db");
		fs::create_directories(mlc / "usr/save/00050010/1004a000/user/common/db");
		fs::create_directories(mlc / "usr/save/00050010/1004a200/user/common/db");

		// lang files
		auto langDir = mlc / "sys/title/0005001b/1005c000/content";
		fs::create_directories(langDir);

		auto langFile = langDir / "language.txt";

		if (!fs::exists(langFile))
		{
			if (std::ofstream file{langFile})
			{
				const auto langStrings = {"ja", "en", "fr", "de", "it", "es", "ko", "nl", "pt", "ru", "zh"};
				for (const char* lang : langStrings)
					fmt::println(file, R"("{}",)", lang);
			}
		}

		auto countryFile = langDir / "country.txt";
		if (!fs::exists(countryFile))
		{
			if (std::ofstream file{countryFile})
			{
				for (sint32 i = 0; i < 201; i++)
				{
					const char* countryCode = NCrypto::GetCountryAsString(i);
					if (boost::iequals(countryCode, "NN"))
						fmt::println(file, "NULL,");
					else
						fmt::println(file, R"("{}",)", countryCode);
				}
			}
		}

		// cemu directories
		fs::create_directories(ActiveSettings::GetConfigPath("controllerProfiles"));
		fs::create_directories(ActiveSettings::GetUserDataPath("memorySearcher"));
	}

	enum PrepareTitleResult : sint32
	{
		SUCCESSFUL = 0,
		ERROR_GAME_BASE_FILES_NOT_FOUND = 1,
		ERROR_NO_DISC_KEY = 2,
		ERROR_NO_TITLE_TIK = 3,
		ERROR_UNKNOWN = 4,
	};

	class TestSurface
	{
	  public:
		TestSurface()
		{
			JNIEnv* env = JNIUtils::GetEnv();

			jclass surfaceTextureClass = env->FindClass("android/graphics/SurfaceTexture");
			jmethodID ctorSurfaceTexture = env->GetMethodID(surfaceTextureClass, "<init>", "(I)V");
			jobject localSurfaceTexture = env->NewObject(surfaceTextureClass, ctorSurfaceTexture, 0);

			jclass surfaceClass = env->FindClass("android/view/Surface");
			jmethodID ctorSurface = env->GetMethodID(surfaceClass, "<init>", "(Landroid/graphics/SurfaceTexture;)V");
			jobject localSurface = env->NewObject(surfaceClass, ctorSurface, localSurfaceTexture);

			m_surfaceTexture = env->NewGlobalRef(localSurfaceTexture);
			m_surface = env->NewGlobalRef(localSurface);

			m_window = ANativeWindow_fromSurface(env, m_surface);
			ANativeWindow_acquire(m_window);

			env->DeleteLocalRef(localSurfaceTexture);
			env->DeleteLocalRef(localSurface);
			env->DeleteLocalRef(surfaceTextureClass);
			env->DeleteLocalRef(surfaceClass);
		}

		~TestSurface()
		{
			JNIEnv* env = JNIUtils::GetEnv();

			ANativeWindow_release(m_window);

			jclass surfaceClass = env->FindClass("android/view/Surface");
			jmethodID releaseSurface = env->GetMethodID(surfaceClass, "release", "()V");
			env->CallVoidMethod(m_surface, releaseSurface);
			env->DeleteGlobalRef(m_surface);
			m_surface = nullptr;
			env->DeleteLocalRef(surfaceClass);

			jclass surfaceTextureClass = env->FindClass("android/graphics/SurfaceTexture");
			jmethodID releaseSurfaceTexture = env->GetMethodID(surfaceTextureClass, "release", "()V");
			env->CallVoidMethod(m_surfaceTexture, releaseSurfaceTexture);
			env->DeleteGlobalRef(m_surfaceTexture);
			m_surfaceTexture = nullptr;
			env->DeleteLocalRef(surfaceTextureClass);
		}

		ANativeWindow* getWindow()
		{
			return m_window;
		}

	  private:
		ANativeWindow* m_window;
		jobject m_surface = nullptr;
		jobject m_surfaceTexture = nullptr;
	};
} // namespace NativeEmulation

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_setReplaceTVWithPadView([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean swapped)
{
	// Emulate pressing the TAB key for showing DRC instead of TV
	WindowSystem::GetWindowInfo().set_keystate(static_cast<uint32>(WindowSystem::PlatformKeyCodes::TAB), swapped);
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_initializeEmulation([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	FilesystemAndroid::SetFilesystemCallbacks(std::make_shared<AndroidFilesystemCallbacks>());
	GetConfigHandle().SetFilename(ActiveSettings::GetConfigPath("settings.xml").generic_wstring());
	NativeEmulation::CreateCemuDirectories();
	NetworkConfig::LoadOnce();
	ActiveSettings::Init();
	LatteOverlay_init();
	CemuCommonInit(); // loads settings.xml into GetConfig() (see CemuCommonInit() in main.cpp)

	// AndroidMemoryManager's monitor thread is already running by this point (started from
	// JNI_OnLoad, before settings.xml exists to be read) with the "Memory Usage Brake" ceiling
	// off. Now that the persisted config has actually been loaded, push the user's saved choice
	// into it once. After this, the Experimental settings screen keeps it in sync live via its
	// own JNI setter whenever the user changes it.
	AndroidMemoryManager::SetMemoryCeilingMB(
		GetConfig().memory_usage_brake.GetValue() ? (uint64_t)GetConfig().memory_ceiling_mb.GetValue() : 0);
}

namespace NativeEmulation
{
	enum class RenderBackend
	{
		Vulkan,
		OpenGL,
	};

	// ============================================================================================
	// COMO ESCOLHER A API GRAFICA DE UM JOGO NO MODO "AUTO"
	//
	// Quando voce descobrir que um jogo roda melhor em OpenGL ou em Vulkan, ha 2 formas de
	// dizer isso ao emulador (as duas so valem com "Render API" = Auto nas configuracoes; nos
	// modos Vulkan e OpenGL fixos a API escolhida la sempre vence):
	//
	//  1) PERFIL DO JOGO (funciona para OpenGL e para Vulkan, tem PRIORIDADE sobre a lista abaixo).
	//     Crie/edite o arquivo  <pasta do app>/gameProfiles/<TitleID com 16 digitos>.ini
	//     (no aparelho: Android/data/info.mandacarucemu.cemu/files/gameProfiles/) com:
	//         [Graphics]
	//         graphics_api = 0      <- 0 = OpenGL ES | 1 = Vulkan
	//     ATENCAO: se ja existir um perfil padrao desse jogo em data/gameProfiles/default/,
	//     copie-o para gameProfiles/ e so acrescente a linha - o perfil do usuario SUBSTITUI o
	//     padrao, nao e somado a ele.
	//
	//  2) LISTA ABAIXO (somente para preferir OpenGL; o padrao do Auto ja e Vulkan, entao para
	//     Vulkan nao precisa de entrada). Acrescente o Title ID como numero hexadecimal de 64 bits,
	//     por exemplo:   0x0005000010101a00ull,
	//     Para descobrir o Title ID: o log do emulador mostra "TitleId: 00050000-10101a00"
	//     (junte as duas metades: 0x0005000010101a00).
	//
	// A lista comeca VAZIA de proposito: nao ha (ainda) dados medidos de compatibilidade por
	// jogo, e inventar entradas aqui seria chutar. Guia completo: docs/RENDER_API_AUTO.md
	// ============================================================================================
	inline const std::vector<TitleId> kAutoPreferOpenGLTitleIds = {
		// 0x0005000010101a00ull, // exemplo de formato (nome do jogo)
	};

	// Modo Automatico: escolhe a API preferida para o jogo que esta sendo iniciado. O titulo
	// ja foi preparado (PrepareForegroundTitle) antes do renderer ser criado, entao o Title ID e
	// o game profile dele ja estao disponiveis aqui.
	//   1) game profile do jogo com graphics_api definido (OpenGL/Vulkan);
	//   2) tabela kAutoPreferOpenGLTitleIds;
	//   3) sem loader Vulkan no aparelho -> OpenGL;
	//   4) padrao: Vulkan (backend mais maduro).
	// Se a API escolhida falhar ao inicializar, o chamador tenta a outra.
	RenderBackend ChooseAutoRenderBackend()
	{
		if (g_current_game_profile)
		{
			const auto& profileApi = g_current_game_profile->GetGraphicsAPI();
			if (profileApi.has_value())
			{
				if (*profileApi == kOpenGL)
				{
					cemuLog_log(LogType::Force, "Render API (Auto): OpenGL ES (game profile)");
					return RenderBackend::OpenGL;
				}
				if (*profileApi == kVulkan)
				{
					cemuLog_log(LogType::Force, "Render API (Auto): Vulkan (game profile)");
					return RenderBackend::Vulkan;
				}
			}
		}

		const TitleId titleId = CafeSystem::GetForegroundTitleId();
		if (std::find(kAutoPreferOpenGLTitleIds.begin(), kAutoPreferOpenGLTitleIds.end(), titleId) != kAutoPreferOpenGLTitleIds.end())
		{
			cemuLog_log(LogType::Force, "Render API (Auto): OpenGL ES (title {:016x} na lista de preferencia)", titleId);
			return RenderBackend::OpenGL;
		}

		if (!InitializeGlobalVulkan())
		{
			cemuLog_log(LogType::Force, "Render API (Auto): OpenGL ES (Vulkan indisponivel neste aparelho)");
			return RenderBackend::OpenGL;
		}

		cemuLog_log(LogType::Force, "Render API (Auto): Vulkan");
		return RenderBackend::Vulkan;
	}
} // namespace NativeEmulation

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_initializeRenderer(JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	static std::unique_ptr<NativeEmulation::TestSurface> testSurface;

	// Render API (Experimental/Hackers -> "Render API"), 3 modos:
	//   Automatico - o emulador escolhe Vulkan ou OpenGL ES para cada jogo na hora de entrar
	//                (ver ChooseAutoRenderBackend) e, se a escolhida falhar ao inicializar,
	//                tenta a outra;
	//   Vulkan     - so usa Vulkan; se nao inicializar, da erro (sem cair para OpenGL);
	//   OpenGL     - so usa OpenGL ES; se nao inicializar, da erro (sem cair para Vulkan).
	using NativeEmulation::RenderBackend;
	const auto rendererMode = (CemuConfig::AndroidRendererMode)GetConfig().android_renderer_mode.GetValue();

	JNIUtils::HandleNativeException(env, [&]() {
		auto createTestSurface = [&]() {
			// recria a surface de teste: se uma tentativa anterior (ex.: Vulkan) chegou a
			// conectar a janela a uma API, a proxima API nao consegue usar a mesma janela
			testSurface.reset();
			testSurface = std::make_unique<NativeEmulation::TestSurface>();
			WindowSystem::GetWindowInfo().window_main.surface = testSurface->getWindow();
		};

		auto tryVulkan = [&](std::string& error) -> bool {
			try
			{
				if (!InitializeGlobalVulkan())
				{
					error = "Vulkan nao esta disponivel neste aparelho (loader Vulkan nao encontrado)";
					return false;
				}
				g_renderer = std::make_unique<VulkanRenderer>();
				return true;
			}
			catch (const std::exception& e)
			{
				g_renderer.reset();
				error = std::string("Falha ao inicializar o Vulkan: ") + e.what();
				return false;
			}
		};

		auto tryOpenGL = [&](std::string& error) -> bool {
			try
			{
				g_androidOpenGLCanvas = std::make_unique<AndroidOpenGLCanvas>();
				if (!g_androidOpenGLCanvas->Initialize(testSurface->getWindow()))
				{
					SetOpenGLCanvasCallbacks(nullptr);
					g_androidOpenGLCanvas.reset();
					error = "Falha ao inicializar o OpenGL ES (driver GLES 3 / EGL indisponivel ou incompativel)";
					return false;
				}
				SetOpenGLCanvasCallbacks(g_androidOpenGLCanvas.get());
				g_androidOpenGLCanvas->MakeCurrent(false);
				g_renderer = std::make_unique<OpenGLRenderer>();
				return true;
			}
			catch (const std::exception& e)
			{
				g_renderer.reset();
				SetOpenGLCanvasCallbacks(nullptr);
				g_androidOpenGLCanvas.reset();
				error = std::string("Falha ao inicializar o OpenGL ES: ") + e.what();
				return false;
			}
		};

		createTestSurface();

		std::string error;
		switch (rendererMode)
		{
		case CemuConfig::kAndroidRendererVulkan:
			if (!tryVulkan(error))
				throw std::runtime_error(error);
			return;

		case CemuConfig::kAndroidRendererOpenGL:
			if (!tryOpenGL(error))
				throw std::runtime_error(error);
			return;

		case CemuConfig::kAndroidRendererAuto:
		default:
		{
			const RenderBackend preferred = NativeEmulation::ChooseAutoRenderBackend();
			const bool vulkanFirst = (preferred == RenderBackend::Vulkan);

			if (vulkanFirst ? tryVulkan(error) : tryOpenGL(error))
				return;
			cemuLog_log(LogType::Force, "Render API (Auto): {} - tentando a outra API", error);

			createTestSurface();
			std::string fallbackError;
			if (vulkanFirst ? tryOpenGL(fallbackError) : tryVulkan(fallbackError))
				return;

			throw std::runtime_error(error + "; " + fallbackError);
		}
		}
	});
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_setDPI([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jfloat dpi)
{
	auto& windowInfo = WindowSystem::GetWindowInfo();
	windowInfo.dpi_scale = windowInfo.pad_dpi_scale = dpi;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_clearPadSurface([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	if (g_androidOpenGLCanvas)
		g_androidOpenGLCanvas->ClearPadSurface();
	else
		VulkanRenderer::GetInstance()->StopUsingPadAndWait();
	WindowSystem::GetWindowInfo().pad_open = false;
}

extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_supportsLoadingCustomDriver([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return NativeEmulation::SupportsLoadingCustomDriver();
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_setSurface(JNIEnv* env, [[maybe_unused]] jclass clazz, jobject surface, jboolean isMainCanvas)
{
	JNIUtils::HandleNativeException(env, [&]() {
		auto& windowHandleInfo = isMainCanvas ? WindowSystem::GetWindowInfo().canvas_main : WindowSystem::GetWindowInfo().canvas_pad;
		auto oldWindow = windowHandleInfo.surface.load();
		if (oldWindow != nullptr)
			ANativeWindow_release(static_cast<ANativeWindow*>(oldWindow));
		auto newSurface = ANativeWindow_fromSurface(env, surface);
		ANativeWindow_acquire(newSurface);
		windowHandleInfo.surface = newSurface;
		windowHandleInfo.surface.notify_all();
	});
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_initializeSurface(JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean isMainCanvas)
{
	JNIUtils::HandleNativeException(env, [&]() {
		int width, height;
		if (isMainCanvas)
		{
			WindowSystem::GetWindowPhysSize(width, height);
		}
		else
		{
			WindowSystem::GetPadWindowPhysSize(width, height);
			WindowSystem::GetWindowInfo().pad_open = true;
		}

		if (g_androidOpenGLCanvas)
		{
			// setSurface() (above) already stored the real ANativeWindow* in
			// canvas_main/canvas_pad just before Kotlin calls this - hand it to the EGL
			// context the same way VulkanRenderer::InitializeSurface consumes it below for
			// Vulkan (see WindowSystem::GetWindowInfo() usage throughout this file).
			auto& windowHandleInfo = isMainCanvas ? WindowSystem::GetWindowInfo().canvas_main : WindowSystem::GetWindowInfo().canvas_pad;
			auto* window = static_cast<ANativeWindow*>(windowHandleInfo.surface.load());
			if (isMainCanvas)
				g_androidOpenGLCanvas->SetMainSurface(window);
			else
				g_androidOpenGLCanvas->SetPadSurface(window);
			return;
		}

		VulkanRenderer::GetInstance()->InitializeSurface({width, height}, isMainCanvas);
	});
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_setSurfaceSize([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jint width, jint height, jboolean isMainCanvas)
{
	auto& windowInfo = WindowSystem::GetWindowInfo();
	if (isMainCanvas)
	{
		windowInfo.width = windowInfo.phys_width = width;
		windowInfo.height = windowInfo.phys_height = height;
	}
	else
	{
		windowInfo.pad_width = windowInfo.phys_pad_width = width;
		windowInfo.pad_height = windowInfo.phys_pad_height = height;
	}
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_initializeSystems([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	NativeEmulation::SetDefaultDeviceController();
	WindowSystem::GetWindowInfo().set_keystatesup();
	NativeEmulation::InitializeAudioDevices();
}

extern "C" [[maybe_unused]] JNIEXPORT jint JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_prepareTitle([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jstring launchPathJava)
{
	fs::path launchPath = JNIUtils::FromJString(env, launchPathJava);

	TitleInfo launchTitle{launchPath};

	using enum NativeEmulation::PrepareTitleResult;

	if (launchTitle.IsValid())
	{
		// the title might not be in the TitleList, so we add it as a temporary entry
		CafeTitleList::AddTitleFromPath(launchPath);
		// title is valid, launch from TitleId
		TitleId baseTitleId;
		if (!CafeTitleList::FindBaseTitleId(launchTitle.GetAppTitleId(), baseTitleId))
		{
			return ERROR_GAME_BASE_FILES_NOT_FOUND;
		}
		CafeSystem::PREPARE_STATUS_CODE r = CafeSystem::PrepareForegroundTitle(baseTitleId);
		if (r != CafeSystem::PREPARE_STATUS_CODE::SUCCESS)
		{
			return ERROR_UNKNOWN;
		}
	}
	else // if (launchTitle.GetFormat() == TitleInfo::TitleDataFormat::INVALID_STRUCTURE )
	{
		// title is invalid, if it's an RPX/ELF we can launch it directly
		// otherwise it's an error
		CafeTitleFileType fileType = DetermineCafeSystemFileType(launchPath);
		if (fileType == CafeTitleFileType::RPX || fileType == CafeTitleFileType::ELF)
		{
			CafeSystem::PREPARE_STATUS_CODE r = CafeSystem::PrepareForegroundTitleFromStandaloneRPX(launchPath);
			if (r != CafeSystem::PREPARE_STATUS_CODE::SUCCESS)
			{
				return ERROR_UNKNOWN;
			}
		}
		else if (launchTitle.GetInvalidReason() == TitleInfo::InvalidReason::NO_DISC_KEY)
		{
			return ERROR_NO_DISC_KEY;
		}
		else if (launchTitle.GetInvalidReason() == TitleInfo::InvalidReason::NO_TITLE_TIK)
		{
			return ERROR_NO_TITLE_TIK;
		}
		else
		{
			return ERROR_UNKNOWN;
		}
	}

	return SUCCESSFUL;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_launchTitle([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	CafeSystem::LaunchForegroundTitle();
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_pauseTitle([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	CafeSystem::PauseTitle();
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_resumeTitle([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	CafeSystem::ResumeTitle();
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_updateThermalStatus([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jint status)
{
	LatteOverlay_updateThermalStatus(static_cast<sint32>(status));
}
