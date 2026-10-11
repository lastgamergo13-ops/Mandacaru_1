package info.cemu.cemu.games.profile

import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import androidx.lifecycle.viewmodel.CreationExtras
import androidx.lifecycle.viewmodel.initializer
import androidx.lifecycle.viewmodel.viewModelFactory
import info.cemu.cemu.common.customdrivers.parseInstalledDrivers
import info.cemu.cemu.nativeinterface.NativeGameTitles
import info.cemu.cemu.nativeinterface.NativeGameTitles.DriverSettingMode
import info.cemu.cemu.nativeinterface.NativeGameTitles.Game
import info.cemu.cemu.nativeinterface.NativeGraphicPacks
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

data class CustomDriverData(
    val customPath: String,
    val name: String,
)

data class DriverSettingChoice(
    val mode: Int,
    val customDriverData: CustomDriverData? = null,
)

/**
 * One graphic pack applicable to this specific game (its titleId is listed in the
 * pack's rules.txt), plus whether the user has selected it for this game only.
 * Selecting a pack here does NOT enable it globally — see
 * [NativeGameTitles.setSelectedGraphicPacksForTitle].
 */
data class GameGraphicPackChoice(
    val basicInfo: NativeGraphicPacks.GraphicPackBasicInfo,
    val selectedForThisGame: Boolean,
)

class GameProfileEditViewModel(val game: Game) : ViewModel() {
    private val _selectedDriverSetting =
        MutableStateFlow(DriverSettingChoice(DriverSettingMode.GLOBAL))
    val selectedDriverSetting = _selectedDriverSetting.asStateFlow()

    // ─── Graphic packs available for this game ─────────────────────────
    private val _graphicPackChoices = MutableStateFlow<List<GameGraphicPackChoice>>(listOf())
    val graphicPackChoices = _graphicPackChoices.asStateFlow()

    private suspend fun getGraphicPackChoices(): List<GameGraphicPackChoice> {
        val selectedPaths =
            NativeGameTitles.getSelectedGraphicPacksForTitle(game.titleId).toSet()
        return NativeGraphicPacks.getGraphicPackBasicInfos()
            .filter { it.titleIds.contains(game.titleId) }
            .sortedBy { it.virtualPath }
            .map { info -> GameGraphicPackChoice(info, info.normalizedPath in selectedPaths) }
    }

    /** Toggles a single graphic pack for this game only and persists immediately. */
    fun setGraphicPackSelectedForGame(normalizedPath: String, selected: Boolean) {
        val updated = _graphicPackChoices.value.map {
            if (it.basicInfo.normalizedPath == normalizedPath)
                it.copy(selectedForThisGame = selected)
            else
                it
        }
        _graphicPackChoices.value = updated
        val selectedPaths = updated
            .filter { it.selectedForThisGame }
            .map { it.basicInfo.normalizedPath }
            .toTypedArray()
        NativeGameTitles.setSelectedGraphicPacksForTitle(game.titleId, selectedPaths)
    }

    // ─── Limit to 30 FPS / Emulation speed — per-game override ────────────────
    // Tri-state: -1 = "use the global setting" (Settings → Experimental/Hackers),
    // 0/1 = off/on just for this game. Never touches the global setting or any
    // other game.
    private val _limitToHalfFpsOverride = MutableStateFlow(-1)
    val limitToHalfFpsOverride = _limitToHalfFpsOverride.asStateFlow()

    fun setLimitToHalfFpsOverride(triState: Int) {
        NativeGameTitles.setLimitToHalfFpsForTitle(game.titleId, triState)
        _limitToHalfFpsOverride.value = triState
    }

    // -1 = "use the global setting", otherwise 100-200 just for this game.
    private val _emulationSpeedPercentOverride = MutableStateFlow(-1)
    val emulationSpeedPercentOverride = _emulationSpeedPercentOverride.asStateFlow()

    fun setEmulationSpeedPercentOverride(value: Int) {
        NativeGameTitles.setEmulationSpeedPercentForTitle(game.titleId, value)
        _emulationSpeedPercentOverride.value = value
    }

    // ─── FSR3 Frame Gen / FSR Upscale — per-game override (-1 = usar a opcao global) ───
    private val _frameGenOptions = MutableStateFlow(List(6) { -1 })
    val frameGenOptions = _frameGenOptions.asStateFlow()

    fun setFrameGenOption(key: Int, value: Int) {
        NativeGameTitles.setFrameGenOptionForTitle(game.titleId, key, value)
        _frameGenOptions.value = _frameGenOptions.value.toMutableList().also { it[key] = value }
    }

    fun setSelectedDriverSetting(driverSetting: DriverSettingChoice) {
        if (driverSetting.mode != DriverSettingMode.GLOBAL
            && driverSetting.mode != DriverSettingMode.SYSTEM
            && !driverSettingChoices.value.any { it.customDriverData != null && it.customDriverData.customPath == driverSetting.customDriverData?.customPath }
        ) {
            return
        }

        val nativeDriverSetting = NativeGameTitles.DriverSetting(
            driverSetting.mode,
            driverSetting.customDriverData?.customPath
        )

        NativeGameTitles.setDriverSettingForTitle(
            game.titleId,
            nativeDriverSetting
        )

        _selectedDriverSetting.value = driverSetting
    }

    private suspend fun getDriverSettingsChoices(): List<DriverSettingChoice> {
        val globalDriverSetting = DriverSettingChoice(DriverSettingMode.GLOBAL)

        val systemDriverSetting = DriverSettingChoice(DriverSettingMode.SYSTEM)

        val installedDrivers = parseInstalledDrivers().map {
            val data = CustomDriverData(it.path, it.metadata.name)
            DriverSettingChoice(
                DriverSettingMode.CUSTOM,
                data,
            )
        }

        val titleDriverSetting = NativeGameTitles.getDriverSettingForTitle(game.titleId)
        val selectedDriverName =
            if (titleDriverSetting.mode == DriverSettingMode.CUSTOM && titleDriverSetting.customPath != null) {
                installedDrivers.firstOrNull { it.customDriverData?.customPath == titleDriverSetting.customPath }
                    ?.customDriverData?.name
            } else {
                null
            }

        if (titleDriverSetting.mode == DriverSettingMode.CUSTOM && selectedDriverName != null) {
            _selectedDriverSetting.value = DriverSettingChoice(
                mode = DriverSettingMode.CUSTOM,
                customDriverData = CustomDriverData(
                    customPath = titleDriverSetting.customPath!!,
                    name = selectedDriverName,
                )
            )
        } else {
            val mode = if (titleDriverSetting.mode == DriverSettingMode.CUSTOM)
                DriverSettingMode.GLOBAL
            else
                titleDriverSetting.mode
            _selectedDriverSetting.value = DriverSettingChoice(mode = mode)
        }

        return listOf(globalDriverSetting) + listOf(systemDriverSetting) + installedDrivers
    }

    private val _driverSettingChoices = MutableStateFlow<List<DriverSettingChoice>>(listOf())
    val driverSettingChoices = _driverSettingChoices.asStateFlow()

    init {
        viewModelScope.launch {
            _driverSettingChoices.value = getDriverSettingsChoices()
            _graphicPackChoices.value = getGraphicPackChoices()
            _limitToHalfFpsOverride.value = NativeGameTitles.getLimitToHalfFpsForTitle(game.titleId)
            _emulationSpeedPercentOverride.value = NativeGameTitles.getEmulationSpeedPercentForTitle(game.titleId)
            _frameGenOptions.value = List(6) { NativeGameTitles.getFrameGenOptionForTitle(game.titleId, it) }
        }
    }

    companion object {
        val GAME_KEY = object : CreationExtras.Key<Game> {}
        val Factory: ViewModelProvider.Factory = viewModelFactory {
            initializer {
                GameProfileEditViewModel(
                    this[GAME_KEY] as Game
                )
            }
        }
    }
}