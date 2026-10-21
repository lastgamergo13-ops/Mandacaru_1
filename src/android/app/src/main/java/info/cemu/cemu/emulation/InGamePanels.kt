package info.cemu.cemu.emulation

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.FilterChip
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Slider
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.collectAsState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.viewmodel.compose.viewModel
import info.cemu.cemu.common.settings.OverlayInputConfig
import info.cemu.cemu.common.ui.localization.tr
import info.cemu.cemu.nativeinterface.NativeCheats
import info.cemu.cemu.nativeinterface.NativeInput
import info.cemu.cemu.nativeinterface.NativeSettings
import info.cemu.cemu.settings.inputoverlay.InputOverlaySettingsViewModel

/** Qual painel lateral (direita) esta aberto em jogo. So um por vez; o liga/desliga de cada um fica no menu da esquerda. */
object InGamePanel {
    const val NONE = 0
    const val FRAME_GEN = 1
    const val HUD = 2
    const val CHEATS = 3
    const val CONTROLLER = 4
}

/** Moldura comum dos paineis da direita: titulo, botao Fechar e conteudo rolavel (mesmo visual do painel do Frame Gen). */
@Composable
private fun PanelFrame(
    modifier: Modifier,
    title: String,
    onClose: () -> Unit,
    content: @Composable ColumnScope.() -> Unit,
) {
    Surface(
        modifier = modifier
            .fillMaxHeight()
            .width(320.dp),
        color = MaterialTheme.colorScheme.surface.copy(alpha = 0.92f),
        tonalElevation = 6.dp,
    ) {
        Column(
            modifier = Modifier
                .verticalScroll(rememberScrollState())
                .padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(title, fontSize = 18.sp, modifier = Modifier.weight(1f))
                TextButton(onClick = onClose) { Text(tr("Close")) }
            }
            content()
        }
    }
}

@Composable
private fun PanelSwitch(label: String, checked: Boolean, onChange: (Boolean) -> Unit) {
    Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
        Text(label, fontSize = 14.sp, modifier = Modifier.weight(1f))
        Switch(checked = checked, onCheckedChange = onChange)
    }
}

@Composable
private fun PanelSlider(label: String, value: Int, from: Int, to: Int, steps: Int, suffix: String, onChange: (Int) -> Unit) {
    Text("$label: $value$suffix", fontSize = 14.sp)
    Slider(
        value = value.toFloat(),
        onValueChange = { onChange(it.toInt()) },
        valueRange = from.toFloat()..to.toFloat(),
        steps = steps,
    )
}

// =====================================================================================================================
// HUD / FPS
// =====================================================================================================================

/**
 * Painel do HUD (Show FPS no menu da esquerda): todas as opcoes do overlay de estatisticas, aplicadas na hora, sem sair do jogo.
 * Sao as mesmas opcoes de Settings > Overlay settings.
 */
@Composable
fun HudInGamePanel(modifier: Modifier = Modifier, onClose: () -> Unit) {
    var position by remember { mutableIntStateOf(NativeSettings.getOverlayPosition()) }
    var scale by remember { mutableIntStateOf(NativeSettings.getOverlayTextScalePercentage()) }
    var opacity by remember { mutableIntStateOf(NativeSettings.getOverlayBackgroundOpacityPercentage()) }
    var fps by remember { mutableStateOf(NativeSettings.isOverlayFPSEnabled()) }
    var draws by remember { mutableStateOf(NativeSettings.isOverlayDrawCallsPerFrameEnabled()) }
    var cpu by remember { mutableStateOf(NativeSettings.isOverlayCPUUsageEnabled()) }
    var cpuCores by remember { mutableStateOf(NativeSettings.isOverlayCPUPerCoreUsageEnabled()) }
    var ram by remember { mutableStateOf(NativeSettings.isOverlayRAMUsageEnabled()) }
    var vram by remember { mutableStateOf(NativeSettings.isOverlayVRAMUsageEnabled()) }
    var frameTime by remember { mutableStateOf(NativeSettings.isOverlayFrameTimeEnabled()) }
    var thermal by remember { mutableStateOf(NativeSettings.isOverlayDeviceThermalStatusEnabled()) }
    var horizontal by remember { mutableStateOf(NativeSettings.isOverlayHorizontalLayoutEnabled()) }
    var debug by remember { mutableStateOf(NativeSettings.isOverlayDebugEnabled()) }

    val positions = listOf(
        NativeSettings.OverlayScreenPosition.DISABLED to tr("Off"),
        NativeSettings.OverlayScreenPosition.TOP_LEFT to tr("Top left"),
        NativeSettings.OverlayScreenPosition.TOP_CENTER to tr("Top center"),
        NativeSettings.OverlayScreenPosition.TOP_RIGHT to tr("Top right"),
        NativeSettings.OverlayScreenPosition.BOTTOM_LEFT to tr("Bottom left"),
        NativeSettings.OverlayScreenPosition.BOTTOM_CENTER to tr("Bottom center"),
        NativeSettings.OverlayScreenPosition.BOTTOM_RIGHT to tr("Bottom right"),
    )

    PanelFrame(modifier, tr("Show FPS / HUD"), onClose) {
        Text(tr("Position"), fontSize = 14.sp)
        positions.chunked(2).forEach { rowItems ->
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                rowItems.forEach { (value, label) ->
                    FilterChip(
                        selected = position == value,
                        onClick = {
                            position = value
                            NativeSettings.setOverlayPosition(value)
                        },
                        label = { Text(label, fontSize = 12.sp) },
                    )
                }
            }
        }
        // a posicao "Off" esconde todo o HUD; as chaves abaixo so valem com o HUD visivel
        PanelSwitch(tr("FPS"), fps) { fps = it; NativeSettings.setOverlayFPSEnabled(it) }
        PanelSwitch(tr("Frame time"), frameTime) { frameTime = it; NativeSettings.setOverlayFrameTimeEnabled(it) }
        PanelSwitch(tr("Draw calls per frame"), draws) { draws = it; NativeSettings.setOverlayDrawCallsPerFrameEnabled(it) }
        PanelSwitch(tr("CPU usage"), cpu) { cpu = it; NativeSettings.setOverlayCPUUsageEnabled(it) }
        PanelSwitch(tr("CPU usage per core"), cpuCores) { cpuCores = it; NativeSettings.setOverlayCPUPerCoreUsageEnabled(it) }
        PanelSwitch(tr("RAM usage"), ram) { ram = it; NativeSettings.setOverlayRAMUsageEnabled(it) }
        PanelSwitch(tr("VRAM usage"), vram) { vram = it; NativeSettings.setOverlayVRAMUsageEnabled(it) }
        PanelSwitch(tr("Device thermal status"), thermal) { thermal = it; NativeSettings.setOverlayDeviceThermalStatusEnabled(it) }
        PanelSwitch(tr("Horizontal layout"), horizontal) { horizontal = it; NativeSettings.setOverlayHorizontalLayoutEnabled(it) }
        PanelSwitch(tr("Debug (Vulkan only)"), debug) { debug = it; NativeSettings.setOverlayDebugEnabled(it) }

        HorizontalDivider()
        PanelSlider(
            tr("Scale"), scale, NativeSettings.OVERLAY_TEXT_SCALE_MIN, NativeSettings.OVERLAY_TEXT_SCALE_MAX, 9, "%",
        ) { scale = it; NativeSettings.setOverlayTextScalePercentage(it) }
        PanelSlider(tr("Background opacity"), opacity, 0, 100, 9, "%") {
            opacity = it
            NativeSettings.setOverlayBackgroundOpacityPercentage(it)
        }
    }
}

// =====================================================================================================================
// Cheats
// =====================================================================================================================

/**
 * Painel de Cheats: lista TODOS os cheats dos graphic packs ativos do jogo, agrupados por pack, tanto os ligados quanto os
 * desligados, e permite alterar qualquer um a qualquer momento (mesma API de NativeCheats que o antigo dialogo).
 */
@Composable
fun CheatsInGamePanel(modifier: Modifier = Modifier, onClose: () -> Unit) {
    var cheats by remember { mutableStateOf(NativeCheats.getCheatsForCurrentGame().toList()) }
    val activeCount = cheats.count { it.enabled }

    PanelFrame(modifier, tr("Cheats"), onClose) {
        if (cheats.isEmpty()) {
            Text(
                tr("No cheats available for this game. Cheats come from graphic packs enabled for it - check the Graphic Packs settings screen."),
                fontSize = 14.sp,
            )
        } else {
            Text(tr("{0} of {1} cheats on", activeCount, cheats.size), fontSize = 13.sp)
            cheats.groupBy { it.packName }.forEach { (packName, packCheats) ->
                HorizontalDivider()
                Text(packName, fontSize = 13.sp, fontWeight = FontWeight.Medium)
                packCheats.forEach { cheat ->
                    PanelSwitch(cheat.cheatName, cheat.enabled) { isChecked ->
                        val succeeded = NativeCheats.setCheatEnabled(cheat.packName, cheat.cheatName, isChecked)
                        // relê do nativo: um cheat pode falhar ao aplicar ou puxar/soltar cheats de dependencia
                        if (succeeded)
                            cheats = NativeCheats.getCheatsForCurrentGame().toList()
                    }
                }
            }
        }
    }
}

// =====================================================================================================================
// Controle
// =====================================================================================================================

/**
 * Painel de Controle: tudo que da para mudar sobre os controles durante o jogo - sensor de movimento, TV/PAD, overlay de
 * toques (liga/desliga, vibracao, opacidade, controle, botoes visiveis, editar/resetar layout) e troca de perfil de controle.
 */
@Composable
fun ControllerInGamePanel(
    modifier: Modifier = Modifier,
    sideMenuState: SideMenuState,
    onSideMenuStateChange: (SideMenuState) -> Unit,
    onEditInputOverlay: () -> Unit,
    onResetInputOverlay: () -> Unit,
    onClose: () -> Unit,
) {
    val overlayViewModel: InputOverlaySettingsViewModel = viewModel()
    val overlaySettings by overlayViewModel.overlaySettings.collectAsState()

    val activeControllerIndices = remember {
        (0 until NativeInput.MAX_CONTROLLERS).filter { !NativeInput.isControllerDisabled(it) }
    }
    val profiles = remember { NativeInput.getControllerProfiles().toList() }
    var selectedController by remember { mutableIntStateOf(activeControllerIndices.firstOrNull() ?: 0) }
    var newProfileName by remember { mutableStateOf("") }
    var message by remember { mutableStateOf<String?>(null) }
    var showVisibleInputs by remember { mutableStateOf(false) }

    PanelFrame(modifier, tr("Controller"), onClose) {
        // ---- Console / tela --------------------------------------------------------------------------------------
        PanelSwitch(tr("Enable motion"), sideMenuState.isMotionEnabled) {
            onSideMenuStateChange(sideMenuState.copy(isMotionEnabled = it))
        }
        PanelSwitch(tr("Replace TV with PAD"), sideMenuState.isTVReplacedWithPad) {
            onSideMenuStateChange(sideMenuState.copy(isTVReplacedWithPad = it))
        }
        PanelSwitch(tr("Show PAD"), sideMenuState.isPadVisible) {
            onSideMenuStateChange(sideMenuState.copy(isPadVisible = it))
        }

        // ---- Overlay de toques ----------------------------------------------------------------------------------
        HorizontalDivider()
        Text(tr("Input overlay"), fontSize = 16.sp)
        PanelSwitch(tr("Show input overlay"), sideMenuState.isInputOverlayVisible) {
            onSideMenuStateChange(sideMenuState.copy(isInputOverlayVisible = it))
            overlayViewModel.setOverlayEnabled(it)
        }

        // ---- Controle emulado (o que o jogo "enxerga"): GamePad, Pro, Classic ou Wiimote ------------------------------
        val emulatedIndex = overlaySettings.controllerIndex
        var emulatedType by remember(emulatedIndex) { mutableIntStateOf(NativeInput.getControllerType(emulatedIndex)) }
        val typeOptions = listOf(
            NativeInput.EmulatedControllerType.VPAD to tr("Wii U GamePad"),
            NativeInput.EmulatedControllerType.PRO to tr("Wii U Pro Controller"),
            NativeInput.EmulatedControllerType.CLASSIC to tr("Wii U Classic Controller"),
            NativeInput.EmulatedControllerType.WIIMOTE to tr("Wiimote"),
        )
        Text(tr("Emulated controller") + " (${emulatedIndex + 1})", fontSize = 14.sp)
        typeOptions.forEach { (type, label) ->
            // mesmas regras da tela de entrada: ha um maximo de GamePads e de controles Wii (Pro/Classic/Wiimote) ao mesmo tempo
            val types = (0 until NativeInput.MAX_CONTROLLERS).map { NativeInput.getControllerType(it) }
            val vpadCount = types.count { it == NativeInput.EmulatedControllerType.VPAD }
            val wpadCount = types.count { it != NativeInput.EmulatedControllerType.VPAD && it != NativeInput.EmulatedControllerType.DISABLED }
            val current = emulatedType
            val allowed = if (type == NativeInput.EmulatedControllerType.VPAD)
                current == NativeInput.EmulatedControllerType.VPAD || vpadCount < NativeInput.MAX_VPAD_CONTROLLERS
            else
                (current != NativeInput.EmulatedControllerType.VPAD && current != NativeInput.EmulatedControllerType.DISABLED) || wpadCount < NativeInput.MAX_WPAD_CONTROLLERS
            FilterChip(
                selected = emulatedType == type,
                enabled = allowed,
                onClick = {
                    NativeInput.setControllerType(emulatedIndex, type)
                    emulatedType = type
                },
                label = { Text(label, fontSize = 13.sp) },
            )
        }

        PanelSwitch(tr("Vibrate"), overlaySettings.isVibrateOnTouchEnabled) { overlayViewModel.setVibrateOnTouch(it) }
        PanelSlider(tr("Inputs opacity"), overlaySettings.alpha, 0, 255, 16, "") { overlayViewModel.setAlpha(it) }
        if (activeControllerIndices.size > 1) {
            Text(tr("Overlay controller"), fontSize = 14.sp)
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                activeControllerIndices.forEach { index ->
                    FilterChip(
                        selected = overlaySettings.controllerIndex == index,
                        onClick = { overlayViewModel.setControllerIndex(index) },
                        label = { Text("${index + 1}") },
                    )
                }
            }
        }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            OutlinedButton(enabled = sideMenuState.isInputOverlayVisible, onClick = onEditInputOverlay) { Text(tr("Edit inputs")) }
            OutlinedButton(enabled = sideMenuState.isInputOverlayVisible, onClick = onResetInputOverlay) { Text(tr("Reset input overlay")) }
        }
        TextButton(onClick = { showVisibleInputs = !showVisibleInputs }) {
            Text(if (showVisibleInputs) tr("Hide visible inputs") else tr("Visible inputs"))
        }
        if (showVisibleInputs) {
            val inputs = listOf(
                "A" to OverlayInputConfig.BUTTON_A, "B" to OverlayInputConfig.BUTTON_B,
                "X" to OverlayInputConfig.BUTTON_X, "Y" to OverlayInputConfig.BUTTON_Y,
                "1" to OverlayInputConfig.BUTTON_ONE, "2" to OverlayInputConfig.BUTTON_TWO,
                "C" to OverlayInputConfig.BUTTON_C, "Z" to OverlayInputConfig.BUTTON_Z,
                tr("Home") to OverlayInputConfig.BUTTON_HOME,
                "L" to OverlayInputConfig.BUTTON_L, "R" to OverlayInputConfig.BUTTON_R,
                "ZL" to OverlayInputConfig.BUTTON_ZL, "ZR" to OverlayInputConfig.BUTTON_ZR,
                tr("Left stick click") to OverlayInputConfig.BUTTON_L_STICK_CLICK,
                tr("Right stick click") to OverlayInputConfig.BUTTON_R_STICK_CLICK,
                tr("Plus") to OverlayInputConfig.BUTTON_PLUS, tr("Minus") to OverlayInputConfig.BUTTON_MINUS,
                tr("Blow mic") to OverlayInputConfig.BUTTON_BLOW_MIC,
                tr("Left joystick") to OverlayInputConfig.JOYSTICK_LEFT, tr("Right joystick") to OverlayInputConfig.JOYSTICK_RIGHT,
                tr("D-pad") to OverlayInputConfig.DPAD,
            )
            inputs.forEach { (label, input) ->
                PanelSwitch(label, overlaySettings.inputVisibilityMap[input] ?: true) {
                    overlayViewModel.setInputVisibility(input, it)
                }
            }
        }

        // ---- Perfis de controle ---------------------------------------------------------------------------------
        HorizontalDivider()
        Text(tr("Controller profile"), fontSize = 16.sp)
        if (activeControllerIndices.isEmpty()) {
            Text(tr("No controller is currently active."), fontSize = 14.sp)
        } else {
            if (activeControllerIndices.size > 1) {
                Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    activeControllerIndices.forEach { index ->
                        FilterChip(
                            selected = selectedController == index,
                            onClick = { selectedController = index },
                            label = { Text(tr("Controller {0}", index + 1)) },
                        )
                    }
                }
            }
            if (profiles.isEmpty()) {
                Text(tr("No saved profiles yet. Save the current mapping as a new profile below to get started."), fontSize = 14.sp)
            } else {
                profiles.forEach { profileName ->
                    TextButton(
                        modifier = Modifier.fillMaxWidth(),
                        onClick = {
                            val applied = NativeInput.loadControllerProfile(selectedController, profileName)
                            message = if (applied) tr("Applied \"{0}\"", profileName) else tr("Failed to apply \"{0}\"", profileName)
                        },
                    ) { Text(profileName, modifier = Modifier.fillMaxWidth()) }
                }
            }
            OutlinedTextField(
                modifier = Modifier.fillMaxWidth(),
                value = newProfileName,
                onValueChange = { newProfileName = it },
                singleLine = true,
                label = { Text(tr("Profile name")) },
            )
            Button(
                enabled = NativeInput.isValidControllerProfileName(newProfileName),
                onClick = {
                    val saved = NativeInput.saveControllerProfile(selectedController, newProfileName)
                    message = if (saved) tr("Saved \"{0}\"", newProfileName) else tr("Failed to save \"{0}\"", newProfileName)
                },
            ) { Text(tr("Save current mapping as new profile")) }
            message?.let { Text(it, fontSize = 13.sp) }
        }
    }
}
