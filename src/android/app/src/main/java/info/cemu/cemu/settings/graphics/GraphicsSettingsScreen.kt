package info.cemu.cemu.settings.graphics

import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import info.cemu.cemu.common.ui.components.Button
import info.cemu.cemu.common.ui.components.ScreenContent
import info.cemu.cemu.common.ui.components.SingleSelection
import info.cemu.cemu.common.ui.components.Slider
import info.cemu.cemu.common.ui.components.Toggle
import info.cemu.cemu.common.ui.localization.tr
import info.cemu.cemu.nativeinterface.NativeEmulation
import info.cemu.cemu.nativeinterface.NativeSettings

private val ScalingFilterChoices = listOf(
    NativeSettings.ScalingFilter.BILINEAR_FILTER,
    NativeSettings.ScalingFilter.BICUBIC_FILTER,
    NativeSettings.ScalingFilter.BICUBIC_HERMITE_FILTER,
    NativeSettings.ScalingFilter.NEAREST_NEIGHBOR_FILTER
)

// FSR (AMD FidelityFX Super Resolution 1.0) is an upscaling algorithm specifically - it
// doesn't make sense as a downscale filter, so it's only offered here, not in
// ScalingFilterChoices (used for both upscale and downscale below).
private val UpscalingFilterChoices = ScalingFilterChoices + NativeSettings.ScalingFilter.FSR_FILTER

@Composable
fun GraphicsSettingsScreen(navigateBack: () -> Unit, goToCustomDriversSettings: () -> Unit) {
    val supportsLoadingCustomDrivers = remember { NativeEmulation.supportsLoadingCustomDriver() }
    var upscaleFilter by rememberSaveable { mutableIntStateOf(NativeSettings.getUpscalingFilter()) }

    ScreenContent(
        appBarText = tr("Graphics settings"),
        navigateBack = navigateBack,
    ) {
        if (supportsLoadingCustomDrivers) {
            Button(
                label = tr("Custom drivers"),
                onClick = goToCustomDriversSettings
            )
        }
        Toggle(
            label = tr("Async shader compile"),
            description = tr("Enables async shader and pipeline compilation. Reduces stutter at the cost of objects not rendering for a short time.\nVulkan only"),
            initialCheckedState = NativeSettings::getAsyncShaderCompile,
            onCheckedChanged = NativeSettings::setAsyncShaderCompile,
        )
        SingleSelection(
            label = tr("VSync"),
            initialChoice = NativeSettings::getVsyncMode,
            onChoiceChanged = NativeSettings::setVsyncMode,
            choiceToString = { vsyncModeToString(it) },
            choices = listOf(
                NativeSettings.VSyncMode.OFF,
                NativeSettings.VSyncMode.DOUBLE_BUFFERING,
                NativeSettings.VSyncMode.TRIPLE_BUFFERING
            ),
        )
        Toggle(
            label = tr("Accurate barriers"),
            description = tr("Disabling the accurate barriers option will lead to flickering graphics but may improve performance. It is highly recommended to leave it turned on"),
            initialCheckedState = NativeSettings::getAccurateBarriers,
            onCheckedChanged = NativeSettings::setAccurateBarriers,
        )
        SingleSelection(
            label = tr("Fullscreen scaling"),
            initialChoice = NativeSettings::getFullscreenScaling,
            onChoiceChanged = NativeSettings::setFullscreenScaling,
            choiceToString = { fullscreenScalingModeToString(it) },
            choices = listOf(
                NativeSettings.FullscreenScaling.KEEP_ASPECT_RATIO,
                NativeSettings.FullscreenScaling.STRETCH
            ),
        )
        SingleSelection(
            label = tr("Upscale filter"),
            initialChoice = NativeSettings::getUpscalingFilter,
            onChoiceChanged = {
                NativeSettings.setUpscalingFilter(it)
                upscaleFilter = it
            },
            choiceToString = { scalingFilterToString(it) },
            choices = UpscalingFilterChoices,
        )
        Slider(
            label = tr("FSR sharpness"),
            initialValue = { (NativeSettings.getFsrSharpness() * 100f).toInt() },
            valueFrom = 0,
            valueTo = 200,
            enabled = upscaleFilter == NativeSettings.ScalingFilter.FSR_FILTER,
            labelFormatter = { "%.2f".format(it / 100f) },
            onValueChange = { NativeSettings.setFsrSharpness(it / 100f) },
        )
        SingleSelection(
            label = tr("Downscale filter"),
            initialChoice = NativeSettings::getDownscalingFilter,
            onChoiceChanged = NativeSettings::setDownscalingFilter,
            choiceToString = { scalingFilterToString(it) },
            choices = ScalingFilterChoices,
        )
    }
}

private fun scalingFilterToString(scalingFilter: Int) = when (scalingFilter) {
    NativeSettings.ScalingFilter.BILINEAR_FILTER -> tr("Bilinear")
    NativeSettings.ScalingFilter.BICUBIC_FILTER -> tr("Bicubic")
    NativeSettings.ScalingFilter.BICUBIC_HERMITE_FILTER -> tr("Hermite")
    NativeSettings.ScalingFilter.NEAREST_NEIGHBOR_FILTER -> tr("Nearest neighbor")
    NativeSettings.ScalingFilter.FSR_FILTER -> tr("FSR")
    else -> throw IllegalArgumentException("Invalid scaling filter:  $scalingFilter")
}

private fun vsyncModeToString(vsyncMode: Int) = when (vsyncMode) {
    NativeSettings.VSyncMode.OFF -> tr("Off")
    NativeSettings.VSyncMode.DOUBLE_BUFFERING -> tr("Double buffering")
    NativeSettings.VSyncMode.TRIPLE_BUFFERING -> tr("Triple buffering")
    else -> throw IllegalArgumentException("Invalid vsync mode: $vsyncMode")
}

private fun fullscreenScalingModeToString(fullscreenScaling: Int) = when (fullscreenScaling) {
    NativeSettings.FullscreenScaling.KEEP_ASPECT_RATIO -> tr("Keep aspect ratio")
    NativeSettings.FullscreenScaling.STRETCH -> tr("Stretch")
    else -> throw IllegalArgumentException("Invalid fullscreen scaling mode:  $fullscreenScaling")
}