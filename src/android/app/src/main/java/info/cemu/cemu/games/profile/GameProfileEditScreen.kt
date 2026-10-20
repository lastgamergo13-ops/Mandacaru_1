package info.cemu.cemu.games.profile

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.MutableCreationExtras
import androidx.lifecycle.viewmodel.compose.viewModel
import info.cemu.cemu.common.ui.components.Header
import info.cemu.cemu.common.ui.components.ScreenContentWithGamePanel
import info.cemu.cemu.common.ui.components.SingleSelection
import info.cemu.cemu.common.ui.components.Slider
import info.cemu.cemu.common.ui.components.Toggle
import info.cemu.cemu.common.ui.localization.tr
import info.cemu.cemu.nativeinterface.NativeEmulation
import info.cemu.cemu.nativeinterface.NativeGameTitles
import info.cemu.cemu.nativeinterface.NativeSettings
import info.cemu.cemu.nativeinterface.NativeGameTitles.DriverSettingMode
import kotlinx.coroutines.launch

@Composable
fun GameProfileEditScreen(
    game: NativeGameTitles.Game,
    navigateBack: () -> Unit,
    startGame: (NativeGameTitles.Game) -> Unit,
    viewModel: GameProfileEditViewModel = viewModel(
        factory = GameProfileEditViewModel.Factory,
        extras = MutableCreationExtras().apply {
            set(GameProfileEditViewModel.GAME_KEY, game)
        },
    ),
) {
    val supportsLoadingCustomDrivers = remember { NativeEmulation.supportsLoadingCustomDriver() }
    val scope = rememberCoroutineScope()
    val snackbarHostState = remember { SnackbarHostState() }

    val driverSettingChoices by viewModel.driverSettingChoices.collectAsState()
    val selectedDriverSetting by viewModel.selectedDriverSetting.collectAsState()
    val graphicPackChoices by viewModel.graphicPackChoices.collectAsState()
    val limitToHalfFpsOverride by viewModel.limitToHalfFpsOverride.collectAsState()
    val emulationSpeedPercentOverride by viewModel.emulationSpeedPercentOverride.collectAsState()
    val frameGenOptions by viewModel.frameGenOptions.collectAsState()

    val titleId = game.titleId

    ScreenContentWithGamePanel(
        appBarText = tr("Edit game profile"),
        navigateBack = navigateBack,
        contentModifier = Modifier.padding(16.dp),
        contentVerticalArrangement = Arrangement.spacedBy(16.dp),
        snackbarHost = { SnackbarHost(hostState = snackbarHostState) },
        game = game,
        onPlay = { startGame(game) },
        onEditProfile = {},
        onFavoriteChanged = { NativeGameTitles.setGameTitleFavorite(titleId, it) },
        onRemoveShaderCaches = {
            NativeGameTitles.removeShaderCacheFilesForTitle(titleId)
            scope.launch { snackbarHostState.showSnackbar(tr("Shader caches removed")) }
        },
    ) {
        Header(text = game.name)
        Toggle(
            label = tr("Load shared libraries"),
            description = tr("Load libraries from the cafeLibs directory"),
            initialCheckedState = {
                NativeGameTitles.isLoadingSharedLibrariesForTitleEnabled(
                    titleId
                )
            },
            onCheckedChanged = { enabled ->
                NativeGameTitles.setLoadingSharedLibrariesForTitleEnabled(
                    titleId, enabled
                )
            },
        )
        Toggle(
            label = tr("Shader multiplication accuracy"),
            description = tr("Controls the accuracy of floating point multiplication in shaders"),
            initialCheckedState = {
                NativeGameTitles.isShaderMultiplicationAccuracyForTitleEnabled(
                    titleId
                )
            },
            onCheckedChanged = { enabled ->
                NativeGameTitles.setShaderMultiplicationAccuracyForTitleEnabled(
                    titleId, enabled
                )
            },
        )
        SingleSelection(
            label = tr("CPU mode"),
            initialChoice = { NativeGameTitles.getCpuModeForTitle(titleId) },
            choices = listOf(
                NativeGameTitles.CPUMode.SINGLECOREINTERPRETER,
                NativeGameTitles.CPUMode.SINGLECORERECOMPILER,
                NativeGameTitles.CPUMode.MULTICORERECOMPILER,
                NativeGameTitles.CPUMode.AUTO
            ),
            choiceToString = { cpuMode -> cpuModeToString(cpuMode) },
            onChoiceChanged = { cpuMode -> NativeGameTitles.setCpuModeForTitle(titleId, cpuMode) })
        SingleSelection(
            label = tr("Thread quantum"),
            initialChoice = { NativeGameTitles.getThreadQuantumForTitle(titleId) },
            choices = NativeGameTitles.THREAD_QUANTUM_VALUES,
            choiceToString = { it.toString() },
            onChoiceChanged = { threadQuantum ->
                NativeGameTitles.setThreadQuantumForTitle(titleId, threadQuantum)
            })
        if (supportsLoadingCustomDrivers) {
            SingleSelection(
                label = tr("Custom driver"),
                choice = selectedDriverSetting,
                choices = driverSettingChoices,
                choiceToString = ::customDriverSettingChoiceToString,
                onChoiceChanged = viewModel::setSelectedDriverSetting,
            )
        }

        // ── Limit to 30 FPS / Emulation speed for this game only ──
        Header(text = tr("Timing"))
        SingleSelection(
            label = tr("Limit to 30 FPS"),
            choice = limitToHalfFpsOverride,
            choices = listOf(-1, 0, 1),
            choiceToString = { triState ->
                when (triState) {
                    0 -> tr("Off")
                    1 -> tr("On")
                    else -> tr("Use global setting")
                }
            },
            onChoiceChanged = viewModel::setLimitToHalfFpsOverride,
        )
        var overrideEmulationSpeed by remember(emulationSpeedPercentOverride) {
            mutableStateOf(emulationSpeedPercentOverride != -1)
        }
        Toggle(
            label = tr("Override emulation speed for this game"),
            description = tr("When off, this game follows the global Emulation speed setting instead."),
            checked = overrideEmulationSpeed,
            onCheckedChanged = { enabled ->
                overrideEmulationSpeed = enabled
                viewModel.setEmulationSpeedPercentOverride(if (enabled) 100 else -1)
            },
        )
        if (overrideEmulationSpeed) {
            Slider(
                label = tr("Emulation speed"),
                initialValue = { if (emulationSpeedPercentOverride == -1) 100 else emulationSpeedPercentOverride },
                valueFrom = 100,
                valueTo = 200,
                steps = 3,
                labelFormatter = { tr("{0}% - the game genuinely plays at {1}x speed, not just the display", it, "%.2f".format(it / 100f)) },
                onValueChange = viewModel::setEmulationSpeedPercentOverride,
            )
        }

        // ── FSR3 Frame Gen / FSR Upscale so para este jogo ──
        Header(text = tr("FSR3 Frame Gen / Upscale (this game)"))
        val globalLabel = tr("Use global setting")
        val triChoices = listOf(-1, 0, 1)
        val triToString: (Int) -> String = { v ->
            when (v) {
                0 -> tr("Off")
                1 -> tr("On")
                else -> globalLabel
            }
        }
        SingleSelection(
            label = tr("Frame Gen"),
            choice = frameGenOptions[NativeSettings.Fsr3Option.FRAME_GEN],
            choices = triChoices,
            choiceToString = triToString,
            onChoiceChanged = { viewModel.setFrameGenOption(NativeSettings.Fsr3Option.FRAME_GEN, it) },
        )
        SingleSelection(
            label = tr("Frame multiplier"),
            choice = frameGenOptions[NativeSettings.Fsr3Option.MULTIPLIER],
            choices = listOf(-1, 2, 3, 4),
            choiceToString = { if (it < 0) globalLabel else "${it}x" },
            onChoiceChanged = { viewModel.setFrameGenOption(NativeSettings.Fsr3Option.MULTIPLIER, it) },
        )
        SingleSelection(
            label = tr("Flow scale"),
            choice = frameGenOptions[NativeSettings.Fsr3Option.FLOW_SCALE],
            choices = listOf(-1, 25, 50, 75, 100),
            choiceToString = { if (it < 0) globalLabel else "$it%" },
            onChoiceChanged = { viewModel.setFrameGenOption(NativeSettings.Fsr3Option.FLOW_SCALE, it) },
        )
        SingleSelection(
            label = tr("Performance mode"),
            choice = frameGenOptions[NativeSettings.Fsr3Option.PERFORMANCE_MODE],
            choices = triChoices,
            choiceToString = triToString,
            onChoiceChanged = { viewModel.setFrameGenOption(NativeSettings.Fsr3Option.PERFORMANCE_MODE, it) },
        )
        SingleSelection(
            label = tr("Base FPS limit"),
            choice = frameGenOptions[NativeSettings.Fsr3Option.BASE_FPS_LIMIT],
            choices = listOf(-1, 0, 30, 40, 60),
            choiceToString = { if (it < 0) globalLabel else if (it == 0) tr("Off") else "$it FPS" },
            onChoiceChanged = { viewModel.setFrameGenOption(NativeSettings.Fsr3Option.BASE_FPS_LIMIT, it) },
        )
        SingleSelection(
            label = tr("FSR Upscale"),
            choice = frameGenOptions[NativeSettings.Fsr3Option.UPSCALE],
            choices = triChoices,
            choiceToString = triToString,
            onChoiceChanged = { viewModel.setFrameGenOption(NativeSettings.Fsr3Option.UPSCALE, it) },
        )

        // ── Graphic packs for this game (saved per-title, not globally) ──
        if (graphicPackChoices.any { !it.basicInfo.virtualPath.contains("cheat", ignoreCase = true) }) {
            Header(text = tr("Graphic packs"))
            // Packs de CHEATS nao aparecem aqui (a lista ficava bagunçada): eles sao controlados em jogo, pelo botao "Cheats" do menu lateral.
            graphicPackChoices.filterNot { it.basicInfo.virtualPath.contains("cheat", ignoreCase = true) }.forEach { choice ->
                Toggle(
                    label = choice.basicInfo.virtualPath,
                    checked = choice.selectedForThisGame,
                    onCheckedChanged = { selected ->
                        viewModel.setGraphicPackSelectedForGame(
                            choice.basicInfo.normalizedPath,
                            selected,
                        )
                    },
                )
            }
        }
    }
}

private fun customDriverSettingChoiceToString(driverSettingChoice: DriverSettingChoice): String =
    when (driverSettingChoice.mode) {
        DriverSettingMode.GLOBAL -> tr("Global")
        DriverSettingMode.SYSTEM -> tr("System driver")
        else -> driverSettingChoice.customDriverData!!.name
    }

private fun cpuModeToString(cpuMode: Int): String = when (cpuMode) {
    NativeGameTitles.CPUMode.SINGLECOREINTERPRETER -> tr("Single-core interpreter")
    NativeGameTitles.CPUMode.SINGLECORERECOMPILER -> tr("Single-core recompiler")
    NativeGameTitles.CPUMode.MULTICORERECOMPILER -> tr("Multi-core recompiler")
    else -> tr("Auto (recommended)")
}
