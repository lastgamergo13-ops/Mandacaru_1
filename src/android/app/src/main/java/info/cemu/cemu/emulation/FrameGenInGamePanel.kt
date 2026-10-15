package info.cemu.cemu.emulation

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import info.cemu.cemu.common.ui.localization.tr
import info.cemu.cemu.nativeinterface.NativeSettings

/**
 * Painel do FSR3 Frame Gen na lateral DIREITA, em jogo. Todas as opcoes sao aplicadas na hora
 * (o swapchain relê as configuracoes a cada frame). Abre pelo item "FSR3 Frame Gen (painel em jogo)"
 * do menu da ESQUERDA; o liga/desliga do Frame Gen fica sempre no menu da esquerda.
 */
@Composable
fun FrameGenInGamePanel(
    modifier: Modifier = Modifier,
    frameGenEnabled: Boolean,
    onFrameGenChange: (Boolean) -> Unit,
    onClose: () -> Unit,
) {
    var multiplier by remember { mutableStateOf(NativeSettings.getFsr3Option(NativeSettings.Fsr3Option.MULTIPLIER)) }
    var flowScale by remember { mutableStateOf(NativeSettings.getFsr3Option(NativeSettings.Fsr3Option.FLOW_SCALE)) }
    var pyramid by remember { mutableStateOf(NativeSettings.getFsr3Option(NativeSettings.Fsr3Option.PYRAMID) != 0) }
    var perfMode by remember { mutableStateOf(NativeSettings.getFsr3Option(NativeSettings.Fsr3Option.PERFORMANCE_MODE) != 0) }
    var fpsLimit by remember { mutableStateOf(NativeSettings.getFsr3Option(NativeSettings.Fsr3Option.BASE_FPS_LIMIT)) }
    var upscale by remember { mutableStateOf(NativeSettings.getFsr3Option(NativeSettings.Fsr3Option.UPSCALE) != 0) }
    var ovShow by remember { mutableStateOf(NativeSettings.getFsr3Option(NativeSettings.Fsr3Option.OVERLAY_SHOW) != 0) }
    var ovDecimals by remember { mutableStateOf(NativeSettings.getFsr3Option(NativeSettings.Fsr3Option.OVERLAY_DECIMALS)) }
    var ovStyle by remember { mutableStateOf(NativeSettings.getFsr3Option(NativeSettings.Fsr3Option.OVERLAY_STYLE)) }
    var ovGpu by remember { mutableStateOf(NativeSettings.getFsr3Option(NativeSettings.Fsr3Option.OVERLAY_GPU_MS) != 0) }

    Surface(
        modifier = modifier
            .fillMaxHeight()
            .width(300.dp),
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
                Text(tr("FSR3 Frame Gen"), fontSize = 18.sp, modifier = Modifier.weight(1f))
                TextButton(onClick = onClose) { Text(tr("Close")) }
            }

            SwitchRow(tr("Frame Gen"), frameGenEnabled) { onFrameGenChange(it) }

            Text(tr("Frame multiplier"), fontSize = 14.sp)
            ChipRow(listOf(2, 3, 4), multiplier, { "${it}x" }) {
                multiplier = it
                NativeSettings.setFsr3Option(NativeSettings.Fsr3Option.MULTIPLIER, it)
            }

            Text(tr("Flow scale"), fontSize = 14.sp)
            ChipRow(listOf(50, 75, 100), flowScale, { "$it%" }) {
                flowScale = it
                NativeSettings.setFsr3Option(NativeSettings.Fsr3Option.FLOW_SCALE, it)
            }

            SwitchRow(tr("Optical Flow pyramid"), pyramid) {
                pyramid = it
                NativeSettings.setFsr3Option(NativeSettings.Fsr3Option.PYRAMID, if (it) 1 else 0)
            }

            SwitchRow(tr("Frame Gen performance mode"), perfMode) {
                perfMode = it
                NativeSettings.setFsr3Option(NativeSettings.Fsr3Option.PERFORMANCE_MODE, if (it) 1 else 0)
            }

            Text(tr("Frame Gen base FPS limit"), fontSize = 14.sp)
            ChipRow(listOf(0, 30, 40, 60), fpsLimit, { if (it == 0) tr("Off") else "$it" }) {
                fpsLimit = it
                NativeSettings.setFsr3Option(NativeSettings.Fsr3Option.BASE_FPS_LIMIT, it)
            }

            SwitchRow(tr("FSR Upscale"), upscale) {
                upscale = it
                NativeSettings.setFsr3Option(NativeSettings.Fsr3Option.UPSCALE, if (it) 1 else 0)
            }

            Text(tr("Overlay"), fontSize = 16.sp)
            SwitchRow(tr("Show Frame Gen line"), ovShow) {
                ovShow = it
                NativeSettings.setFsr3Option(NativeSettings.Fsr3Option.OVERLAY_SHOW, if (it) 1 else 0)
            }
            Text(tr("Decimals"), fontSize = 14.sp)
            ChipRow(listOf(0, 1, 2), ovDecimals, { it.toString() }) {
                ovDecimals = it
                NativeSettings.setFsr3Option(NativeSettings.Fsr3Option.OVERLAY_DECIMALS, it)
            }
            Text(tr("Style"), fontSize = 14.sp)
            ChipRow(listOf(0, 1), ovStyle, { if (it == 0) tr("Detailed") else tr("Compact") }) {
                ovStyle = it
                NativeSettings.setFsr3Option(NativeSettings.Fsr3Option.OVERLAY_STYLE, it)
            }
            SwitchRow(tr("Show GPU cost (ms)"), ovGpu) {
                ovGpu = it
                NativeSettings.setFsr3Option(NativeSettings.Fsr3Option.OVERLAY_GPU_MS, if (it) 1 else 0)
            }
        }
    }
}

@Composable
private fun SwitchRow(label: String, checked: Boolean, onChange: (Boolean) -> Unit) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(label, fontSize = 14.sp, modifier = Modifier.weight(1f))
        Switch(checked = checked, onCheckedChange = onChange)
    }
}

@Composable
private fun ChipRow(
    choices: List<Int>,
    selected: Int,
    label: (Int) -> String,
    onSelect: (Int) -> Unit,
) {
    Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        choices.forEach {
            FilterChip(
                selected = it == selected,
                onClick = { onSelect(it) },
                label = { Text(label(it)) },
            )
        }
    }
}
