#pragma once
#include "config/CemuConfig.h"

struct gameProfileIntegerOption_t
{
	bool isPresent = false;
	sint32 value;
};

class GameProfile
{
	friend class GameProfileWindow;

public:
	static const uint32 kThreadQuantumDefault = 45000;

	bool Load(uint64_t title_id);
	void Save(uint64_t title_id);
	void ResetOptional();
	void Reset();

	[[nodiscard]] uint64 GetTitleId() const { return m_title_id; }
	[[nodiscard]] bool IsLoaded() const { return m_is_loaded; }
	[[nodiscard]] bool IsDefaultProfile() const { return m_is_default; }
	[[nodiscard]] const std::optional<std::string>& GetGameName() const { return m_gameName; }

	[[nodiscard]] const std::optional<bool>& ShouldLoadSharedLibraries() const { return m_loadSharedLibraries; }
	void SetShouldLoadSharedLibraries(bool shouldLoadSharedLibraries) { m_loadSharedLibraries = shouldLoadSharedLibraries; }
	[[nodiscard]] bool StartWithGamepadView() const { return m_startWithPadView; }

	[[nodiscard]] const std::optional<GraphicAPI>& GetGraphicsAPI() const { return m_graphics_api; }
	[[nodiscard]] const AccurateShaderMulOption& GetAccurateShaderMul() const { return m_accurateShaderMul; }
	void SetAccurateShaderMul(AccurateShaderMulOption accurateShaderMulOption) { m_accurateShaderMul = accurateShaderMulOption; }
#if ENABLE_METAL
	[[nodiscard]] bool GetShaderFastMath() const { return m_shaderFastMath; }
	[[nodiscard]] MetalBufferCacheMode GetBufferCacheMode() const { return m_metalBufferCacheMode; }
	[[nodiscard]] PositionInvariance GetPositionInvariance() const { return m_positionInvariance; }
#endif
	[[nodiscard]] const std::optional<PrecompiledShaderOption>& GetPrecompiledShadersState() const { return m_precompiledShaders; }

	[[nodiscard]] uint32 GetThreadQuantum() const { return m_threadQuantum; }
	void SetThreadQuantum(uint32 threadQuantum){ m_threadQuantum = threadQuantum; }
	[[nodiscard]] const std::optional<CPUMode>& GetCPUMode() const { return m_cpuMode; }
	void SetCPUMode(CPUMode cpuMode) { m_cpuMode = cpuMode; }

	[[nodiscard]] bool IsAudioDisabled() const { return m_disableAudio; }

	[[nodiscard]] const std::array< std::optional<std::string>, 8>& GetControllerProfile() const { return m_controllerProfile; }

	// Android/Mandacaru — graphic packs selected specifically for this title.
	// Stored as normalized relative paths (e.g. "Bayonetta2/Graphics"), matching
	// GraphicPack2::GetNormalizedPathString(). These are activated in addition to
	// (not instead of) globally-enabled packs, purely for this title's session —
	// see GraphicPack2::ActivateForCurrentTitle(). They never touch the global
	// graphic_pack_entries config, so other games are never affected.
	[[nodiscard]] const std::vector<std::string>& GetSelectedGraphicPacks() const { return m_selectedGraphicPacks; }
	void SetSelectedGraphicPacks(std::vector<std::string> selectedGraphicPacks) { m_selectedGraphicPacks = std::move(selectedGraphicPacks); }

	// Android/Mandacaru — per-title override for "Limit to 30 FPS" / "Emulation speed".
	// nullopt (the default) means "use the global setting" — setting either of these only
	// affects THIS title, never the global config or any other game's profile. See the
	// priority/reset logic in CafeSystem's title launch flow (cemu_initForGame).
	[[nodiscard]] const std::optional<bool>& GetLimitToHalfFps() const { return m_limitToHalfFps; }
	void SetLimitToHalfFps(std::optional<bool> value) { m_limitToHalfFps = value; }

	[[nodiscard]] const std::optional<uint32>& GetEmulationSpeedPercent() const { return m_emulationSpeedPercent; }
	void SetEmulationSpeedPercent(std::optional<uint32> value) { m_emulationSpeedPercent = value; }

#if BOOST_PLAT_ANDROID
  public:
	struct DriverSetting
	{
		DriverSettingMode mode = DriverSettingMode::Global;
		std::optional<std::string> customPath;
	};

	[[nodiscard]] DriverSetting GetDriverSetting() const
	{
		return m_driverSetting;
	}

	void SetDriverSetting(DriverSetting driverSetting)
	{
		m_driverSetting = driverSetting;
	}

  private:
	DriverSetting m_driverSetting;

#endif

  private:
	uint64_t m_title_id = 0;
	bool m_is_loaded = false;
	bool m_is_default = true;

	std::optional<std::string> m_gameName{};

	// general settings
	std::optional<bool> m_loadSharedLibraries{}; // = true;
	bool m_startWithPadView = false;

	// graphic settings
	std::optional<GraphicAPI> m_graphics_api{};
	AccurateShaderMulOption m_accurateShaderMul = AccurateShaderMulOption::True;
#if ENABLE_METAL
	bool m_shaderFastMath = true;
	MetalBufferCacheMode m_metalBufferCacheMode = MetalBufferCacheMode::Auto;
	PositionInvariance m_positionInvariance = PositionInvariance::Auto;
#endif
	std::optional<PrecompiledShaderOption> m_precompiledShaders{};
	// cpu settings
	uint32 m_threadQuantum = kThreadQuantumDefault; // values: 20000 45000 60000 80000 100000
	std::optional<CPUMode> m_cpuMode{}; // = CPUModeOption::kSingleCoreRecompiler;
	// audio
	bool m_disableAudio = false;
	// controller settings
	std::array< std::optional<std::string>, 8> m_controllerProfile{};
	// graphic packs selected specifically for this title (Android/Mandacaru)
	std::vector<std::string> m_selectedGraphicPacks{};
	// per-title "Limit to 30 FPS" / "Emulation speed" override (Android/Mandacaru)
	std::optional<bool> m_limitToHalfFps{};
	std::optional<uint32> m_emulationSpeedPercent{};
};
extern std::unique_ptr<GameProfile> g_current_game_profile;

void gameProfile_load();
