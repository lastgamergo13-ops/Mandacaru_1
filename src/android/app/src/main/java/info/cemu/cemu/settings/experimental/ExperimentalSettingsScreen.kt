package info.cemu.cemu.settings.experimental

import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import android.widget.Toast
import info.cemu.cemu.common.cemufiles.tryShareVkTraceFile
import info.cemu.cemu.common.cemufiles.vkTraceFileExists
import info.cemu.cemu.common.ui.components.Button
import info.cemu.cemu.common.ui.components.ScreenContent
import info.cemu.cemu.common.ui.components.ScrollableDialog
import info.cemu.cemu.common.ui.components.SingleSelection
import info.cemu.cemu.common.ui.components.Slider
import info.cemu.cemu.common.ui.components.Toggle
import info.cemu.cemu.common.ui.localization.tr
import info.cemu.cemu.nativeinterface.NativeSettings

private fun memoryCeilingToString(ceilingMB: Int) = when (ceilingMB) {
    NativeSettings.MemoryCeiling.GB_2 -> tr("2 GB")
    NativeSettings.MemoryCeiling.GB_4 -> tr("4 GB")
    NativeSettings.MemoryCeiling.GB_6 -> tr("6 GB")
    NativeSettings.MemoryCeiling.GB_8 -> tr("8 GB")
    NativeSettings.MemoryCeiling.GB_12 -> tr("12 GB")
    else -> throw IllegalArgumentException("Invalid memory ceiling: $ceilingMB")
}

private fun rendererModeToString(mode: Int) = when (mode) {
    NativeSettings.RendererMode.AUTOMATIC -> tr("Auto")
    NativeSettings.RendererMode.VULKAN -> tr("Vulkan")
    NativeSettings.RendererMode.OPENGL -> tr("OpenGL")
    else -> throw IllegalArgumentException("Invalid renderer mode: $mode")
}

/**
 * "What does this do?" button that opens a scrollable dialog with a longer explanation -
 * built from this app's existing Button + ScrollableDialog components.
 */
@Composable
private fun InfoDialogButton(title: String, explanation: String) {
    var showDialog by remember { mutableStateOf(false) }
    Button(label = title, description = tr("Tap for details"), onClick = { showDialog = true })
    if (showDialog) {
        ScrollableDialog(title = title, onDismissRequest = { showDialog = false }) {
            Text(text = explanation)
        }
    }
}

/**
 * "Experimental / Hackers" tab: new/in-testing features that don't belong in the main
 * settings screens yet. Currently holds the per-format BC1-BC5 -> ETC2/EAC texture
 * recompression options.
 */
@Composable
fun ExperimentalSettingsScreen(navigateBack: () -> Unit, goToQualityOfLifeSettings: () -> Unit) {
    val context = LocalContext.current
    var vkSyncDebug by rememberSaveable { mutableStateOf(NativeSettings.getVkSyncDebug()) }
    var memoryBrakeEnabled by rememberSaveable { mutableStateOf(NativeSettings.isMemoryUsageBrakeEnabled()) }
    ScreenContent(
        appBarText = tr("Experimental/Hackers"),
        navigateBack = navigateBack,
    ) {
        InfoDialogButton(
            title = tr("Experimental/Hackers"),
            explanation = tr(
                "This tab holds new features that are still being finished, meant for advanced " +
                        "users. Defaults elsewhere in the app are chosen to be safe; anything you change " +
                        "here is opt-in and may be unstable. If something here misbehaves, it's always " +
                        "safe to switch it back off."
            ),
        )
        Toggle(
            label = tr("Vulkan diagnostic log"),
            description = tr(
                "Liga o diagnostico do Vulkan: espera cada submit da GPU terminar (MAIS LENTO) e, se a GPU " +
                        "travar (VK_ERROR_DEVICE_LOST), grava os shaders do submit que travou em vk_trace.txt. " +
                        "Use so para gerar o log e depois desligue. Vale na hora."
            ),
            initialCheckedState = { vkSyncDebug },
            onCheckedChanged = {
                vkSyncDebug = it
                NativeSettings.setVkSyncDebug(it)
            },
        )
        Button(
            label = tr("Share Vulkan diagnostic log"),
            description = tr("Escolha onde salvar/enviar o vk_trace.txt (igual ao Share log file)."),
            onClick = {
                if (!vkTraceFileExists() || !tryShareVkTraceFile(context))
                    Toast.makeText(context, tr("Nenhum log de diagnostico ainda. Ligue o diagnostico e reproduza o problema."), Toast.LENGTH_LONG).show()
            },
        )
        InfoDialogButton(
            title = tr("Render API"),
            explanation = tr(
                "This picks which graphics API the emulator uses to talk to your GPU driver.\n\n" +
                        "Auto: the emulator chooses between Vulkan and OpenGL for each game when you " +
                        "start it. It uses the game's own profile or the built-in preference list if " +
                        "the game has one, otherwise Vulkan (the most tested backend), and OpenGL if " +
                        "Vulkan isn't available on your device. If the chosen API fails to start, it " +
                        "automatically tries the other one.\n\n" +
                        "Vulkan: only ever uses Vulkan. If Vulkan can't start, the game won't start " +
                        "(it will not fall back to OpenGL).\n\n" +
                        "OpenGL: only ever uses OpenGL ES, talking directly to your GPU's own GLES " +
                        "driver (libEGL/libGLESv3). If it can't start, the game won't start (it will " +
                        "not fall back to Vulkan).\n\n" +
                        "Honest limitations: the OpenGL path is newer and less tested than Vulkan. " +
                        "Some older Mali GPUs (Midgard-generation) don't expose geometry/tessellation " +
                        "shaders in GLES at all, so games that depend on those stages won't render " +
                        "correctly there - that's a hardware gap, not a bug. A few texture-view and " +
                        "readback edge cases use best-effort approximations on GLES. If a game looks " +
                        "wrong or crashes on OpenGL, switch to Vulkan or Auto. Takes effect the next " +
                        "time you launch a game."
            ),
        )
        SingleSelection(
            label = tr("Render API"),
            initialChoice = NativeSettings::getRendererMode,
            onChoiceChanged = NativeSettings::setRendererMode,
            choiceToString = { rendererModeToString(it) },
            choices = listOf(
                NativeSettings.RendererMode.AUTOMATIC,
                NativeSettings.RendererMode.VULKAN,
                NativeSettings.RendererMode.OPENGL,
            ),
        )
        InfoDialogButton(
            title = tr("FSR Upscale"),
            explanation = tr(
                "Usa o filtro AMD FSR 1.0 (EASU + RCAS) do Cemu para ampliar a imagem do jogo " +
                        "(renderizada na resolucao nativa do Wii U) ate a resolucao da sua tela, " +
                        "com bordas mais nitidas que o bilinear/bicubic.\n\n" +
                        "Honestidade tecnica: o FSR3 'de verdade' (temporal) precisa de depth e " +
                        "motion vectors do jogo, que o Cemu nao tem. Por isso este botao liga o FSR 1.0 " +
                        "(espacial), que e um upscale real e ja funciona. Pode ser definido por jogo no " +
                        "perfil do jogo. Equivale a escolher \"FSR\" em Graficos > Filtro de upscale, " +
                        "sem alterar aquela escolha."
            ),
        )
        var fsr3Upscale by rememberSaveable { mutableStateOf(NativeSettings.getFsr3Option(NativeSettings.Fsr3Option.UPSCALE) != 0) }
        Toggle(
            label = tr("FSR Upscale"),
            description = tr("Liga o filtro FSR 1.0 (EASU+RCAS) no lugar do filtro de upscale normal. Vale na hora."),
            initialCheckedState = { fsr3Upscale },
            onCheckedChanged = {
                fsr3Upscale = it
                NativeSettings.setFsr3Option(NativeSettings.Fsr3Option.UPSCALE, if (it) 1 else 0)
            },
        )
        InfoDialogButton(
            title = tr("FSR3 Frame Gen"),
            explanation = tr(
                "AMD FSR3 Frame Generation inserts an extra, AI-generated frame between two frames " +
                        "the game actually rendered, estimating motion between them (Optical Flow) to " +
                        "synthesize what the in-between frame should look like. This raises the number of " +
                        "frames shown per second without the GPU having to fully render each one, making " +
                        "motion look smoother.\n\n" +
                        "Status: EXPERIMENTAL (Vulkan; OpenGL ES em teste). Uses AMD's real Optical Flow plus a custom " +
                        "interpolation shader (the game's depth/motion vectors are not available in " +
                        "Cemu). Can be toggled while a game is running; costs extra GPU time and may " +
                        "show artifacts."
            ),
        )
        Toggle(
            label = tr("FSR3 Frame Gen"),
            description = tr(
                "Experimental (Vulkan; OpenGL ES em teste). Liga/desliga na hora, ate com o jogo rodando.\n" +
                        "Resumo: gera um frame extra entre dois frames reais, estimando o movimento " +
                        "entre eles, pra deixar o movimento na tela mais suave sem a GPU ter que " +
                        "renderizar cada frame por completo."
            ),
            initialCheckedState = NativeSettings::getFsr3FrameGen,
            onCheckedChanged = NativeSettings::setFsr3FrameGen,
        )
        SingleSelection(
            label = tr("Frame multiplier"),
            initialChoice = { NativeSettings.getFsr3Option(NativeSettings.Fsr3Option.MULTIPLIER) },
            onChoiceChanged = { NativeSettings.setFsr3Option(NativeSettings.Fsr3Option.MULTIPLIER, it) },
            choiceToString = { "${it}x" },
            choices = listOf(2, 3, 4),
        )
        SingleSelection(
            label = tr("Flow scale"),
            initialChoice = { NativeSettings.getFsr3Option(NativeSettings.Fsr3Option.FLOW_SCALE) },
            onChoiceChanged = { NativeSettings.setFsr3Option(NativeSettings.Fsr3Option.FLOW_SCALE, it) },
            choiceToString = { "$it%" },
            choices = listOf(50, 75, 100),
        )
        Toggle(
            label = tr("Frame Gen Optical Flow pyramid"),
            description = tr("Optical Flow multi-nivel (ate 7 niveis, como o FSR3 original): movimento grande mais preciso, custa mais GPU. Se o shader do downsample faltar, volta sozinho ao modo simples."),
            initialCheckedState = { NativeSettings.getFsr3Option(NativeSettings.Fsr3Option.PYRAMID) != 0 },
            onCheckedChanged = { NativeSettings.setFsr3Option(NativeSettings.Fsr3Option.PYRAMID, if (it) 1 else 0) },
        )
        Toggle(
            label = tr("Frame Gen performance mode"),
            description = tr("Optical Flow mais leve (forca Flow scale ate 50%). Menos custo de GPU, movimento menos preciso."),
            initialCheckedState = { NativeSettings.getFsr3Option(NativeSettings.Fsr3Option.PERFORMANCE_MODE) != 0 },
            onCheckedChanged = { NativeSettings.setFsr3Option(NativeSettings.Fsr3Option.PERFORMANCE_MODE, if (it) 1 else 0) },
        )
        SingleSelection(
            label = tr("Frame Gen base FPS limit"),
            initialChoice = { NativeSettings.getFsr3Option(NativeSettings.Fsr3Option.BASE_FPS_LIMIT) },
            onChoiceChanged = { NativeSettings.setFsr3Option(NativeSettings.Fsr3Option.BASE_FPS_LIMIT, it) },
            choiceToString = { if (it == 0) tr("Off") else "$it FPS" },
            choices = listOf(0, 30, 40, 60),
        )
        Toggle(
            label = tr("Frame Gen overlay"),
            description = tr("Mostra a linha 'FrameGen: real + gerado = total' no overlay de stats (precisa do FPS ligado). A posicao, cor e tamanho seguem o overlay normal."),
            initialCheckedState = { NativeSettings.getFsr3Option(NativeSettings.Fsr3Option.OVERLAY_SHOW) != 0 },
            onCheckedChanged = { NativeSettings.setFsr3Option(NativeSettings.Fsr3Option.OVERLAY_SHOW, if (it) 1 else 0) },
        )
        SingleSelection(
            label = tr("Frame Gen overlay decimals"),
            initialChoice = { NativeSettings.getFsr3Option(NativeSettings.Fsr3Option.OVERLAY_DECIMALS) },
            onChoiceChanged = { NativeSettings.setFsr3Option(NativeSettings.Fsr3Option.OVERLAY_DECIMALS, it) },
            choiceToString = { it.toString() },
            choices = listOf(0, 1, 2),
        )
        SingleSelection(
            label = tr("Frame Gen overlay style"),
            initialChoice = { NativeSettings.getFsr3Option(NativeSettings.Fsr3Option.OVERLAY_STYLE) },
            onChoiceChanged = { NativeSettings.setFsr3Option(NativeSettings.Fsr3Option.OVERLAY_STYLE, it) },
            choiceToString = { if (it == 0) tr("Detailed") else tr("Compact") },
            choices = listOf(0, 1),
        )
        Toggle(
            label = tr("Frame Gen overlay: GPU cost"),
            description = tr("Mostra o custo de GPU (ms) do frame gerado (Optical Flow + interpolacao)."),
            initialCheckedState = { NativeSettings.getFsr3Option(NativeSettings.Fsr3Option.OVERLAY_GPU_MS) != 0 },
            onCheckedChanged = { NativeSettings.setFsr3Option(NativeSettings.Fsr3Option.OVERLAY_GPU_MS, if (it) 1 else 0) },
        )
        Button(
            label = tr("Quality of Life"),
            description = tr("Convenience features ported from an older branch, kept separate from everything else here. Off by default."),
            onClick = goToQualityOfLifeSettings,
        )
        InfoDialogButton(
            title = tr("Texture compression (BC1-BC5 -> ETC2/EAC)"),
            explanation = tr(
                "What this actually does, honestly: Wii U games store many textures in compressed " +
                        "formats called BC1 through BC5 (diffuse/color in BC1, textures needing " +
                        "transparency in BC2/BC3, single/dual-channel data including normal maps in " +
                        "BC4/BC5). Most Android GPUs can't read any of these directly - they support a " +
                        "different compressed format, ETC2/EAC, instead - so today Mandacaru decodes them " +
                        "back into fully uncompressed pixels before handing them to the GPU. Each toggle " +
                        "below re-compresses that one format into real ETC2/EAC data instead, but only on " +
                        "a GPU that actually supports ETC2/EAC and does NOT support that BC format " +
                        "natively - checked for real on your device, both on Vulkan and OpenGL " +
                        "independently, so results can differ slightly between the two render APIs.\n\n" +
                        "Honest limitations: the ETC2/EAC encoders were written from the official Khronos " +
                        "specification and cross-checked against independent reference implementations, " +
                        "but have not been verified against a real ETC2 GPU decoder or run on real " +
                        "hardware. They also use fast, average-value-per-block strategies, so quality is " +
                        "a little below the original BC compression even when everything works exactly as " +
                        "intended. All five are off by default for this reason - if textures look blocky, " +
                        "noisy, or wrong after turning one on, turn it back off; that's this option, not " +
                        "your game."
            ),
        )
        InfoDialogButton(
            title = tr("BC1 texture compression"),
            explanation = tr(
                "BC1 is the most common Wii U texture format: opaque or 1-bit-alpha diffuse/color " +
                        "textures. Re-compressing to ETC2 RGB8 keeps roughly the same VRAM savings as " +
                        "native BC1 would (about 4x smaller than uncompressed) instead of falling back to " +
                        "full-size RGBA."
            ),
        )
        Toggle(
            label = tr("BC1 texture compression"),
            description = tr("Diffuse/color textures. See \"What does this do?\" above."),
            initialCheckedState = NativeSettings::getTextureCompressionBC1,
            onCheckedChanged = NativeSettings::setTextureCompressionBC1,
        )
        InfoDialogButton(
            title = tr("BC2 texture compression"),
            explanation = tr(
                "BC2 adds an explicit 4-bit alpha channel to BC1-style color data (common in older " +
                        "titles' transparency effects). The ETC2 target for this one (RGBA8_ETC2_EAC) " +
                        "uses the same amount of memory as BC2 itself, so this option's real benefit is " +
                        "avoiding today's always-uncompressed fallback for this format, not shrinking it " +
                        "further."
            ),
        )
        Toggle(
            label = tr("BC2 texture compression"),
            description = tr("Color + explicit alpha textures. See \"What does this do?\" above."),
            initialCheckedState = NativeSettings::getTextureCompressionBC2,
            onCheckedChanged = NativeSettings::setTextureCompressionBC2,
        )
        InfoDialogButton(
            title = tr("BC3 texture compression"),
            explanation = tr(
                "BC3 is like BC2 but with smoother, interpolated 8-bit alpha (foliage, particle " +
                        "effects, UI elements) - it's the more common of the two in practice. Same ETC2 " +
                        "target and same tradeoffs as BC2 above."
            ),
        )
        Toggle(
            label = tr("BC3 texture compression"),
            description = tr("Color + interpolated alpha textures. See \"What does this do?\" above."),
            initialCheckedState = NativeSettings::getTextureCompressionBC3,
            onCheckedChanged = NativeSettings::setTextureCompressionBC3,
        )
        InfoDialogButton(
            title = tr("BC4 texture compression"),
            explanation = tr(
                "BC4 stores a single data channel per pixel - height maps, masks, or one channel of " +
                        "a normal map. Re-compresses to R11_EAC, keeping the same roughly 2x VRAM saving " +
                        "over uncompressed that native BC4 would have given. Covers both the UNORM and " +
                        "SNORM (signed - the common case for actual normal map data) variants."
            ),
        )
        Toggle(
            label = tr("BC4 texture compression"),
            description = tr("Single-channel textures (masks, one normal map channel). See \"What does this do?\" above."),
            initialCheckedState = NativeSettings::getTextureCompressionBC4,
            onCheckedChanged = NativeSettings::setTextureCompressionBC4,
        )
        InfoDialogButton(
            title = tr("BC5 texture compression"),
            explanation = tr(
                "BC5 stores two data channels - almost always a full normal map's X/Y pair. " +
                        "Re-compresses to RG11_EAC, keeping the same VRAM saving native BC5 would have " +
                        "given. Covers both UNORM and SNORM (signed - the normal case for real normal " +
                        "maps) variants."
            ),
        )
        Toggle(
            label = tr("BC5 texture compression"),
            description = tr("Two-channel textures (normal maps). See \"What does this do?\" above."),
            initialCheckedState = NativeSettings::getTextureCompressionBC5,
            onCheckedChanged = NativeSettings::setTextureCompressionBC5,
        )
        InfoDialogButton(
            title = tr("Recompiler AOT cache"),
            explanation = tr(
                "What this does: Cemu translates each Wii U game's PowerPC code into native " +
                        "machine code on the fly the first time it runs (\"recompiling\"). That happens " +
                        "again from scratch every single time you (re)launch a game, which is one of the " +
                        "bigger causes of stutter and slow boot times, especially on a phone's CPU. This " +
                        "option saves already-translated code to a per-game cache file on disk (the same " +
                        "idea as the shader cache Cemu already uses for graphics shaders), so a later " +
                        "launch of the same game can reuse it instead of translating everything again.\n\n" +
                        "Honest limitations: this is new and more delicate than the shader cache - the " +
                        "cached data is raw machine code, not a shader, and a bug in it means running " +
                        "miscompiled code instead of just a visual glitch. Every cache entry is checksummed " +
                        "against the game's own code and safely ignored (falls back to a normal recompile) " +
                        "if anything doesn't match, but this hasn't been tested nearly as widely as the " +
                        "rest of the emulator. Off by default for that reason - if a game becomes unstable " +
                        "after turning this on, turn it back off."
            ),
        )
        Toggle(
            label = tr("Recompiler AOT cache"),
            description = tr("Caches translated game code to disk for faster reloads. See \"What does this do?\" above."),
            initialCheckedState = NativeSettings::getRecompilerAOTCache,
            onCheckedChanged = NativeSettings::setRecompilerAOTCache,
        )
        InfoDialogButton(
            title = tr("Limit Shader Compiler Threads"),
            explanation = tr(
                "What this does: while a game runs, Cemu compiles new shaders/pipelines in the " +
                        "background as it encounters them, using several threads at once - and, on " +
                        "boot, it also reloads the entire previously-seen pipeline cache for that game " +
                        "in one burst, using just as many threads all at the same time. Each thread " +
                        "uses a meaningful amount of temporary RAM while it compiles something.\n\n" +
                        "Why this matters more on Android specifically: many phones have several CPU " +
                        "cores but comparatively little RAM actually available to the app, so \"one " +
                        "thread per core\" can be too aggressive there even though it would be fine on " +
                        "a PC with the same core count.\n\n" +
                        "What this option actually does: caps both of those thread pools at 2 threads " +
                        "each instead of up to 8. It only ever reduces the thread count the app would " +
                        "already use - it never increases it.\n\n" +
                        "Honest tradeoff: compilation gets slower, so new-shader stutter lasts longer, " +
                        "and the loading screen that reloads the pipeline cache also takes longer. " +
                        "Turn this on if the app is actually being killed (closing itself, not just " +
                        "stuttering) during gameplay or on that loading screen; leave it off otherwise, " +
                        "since it would just make things slower with no benefit if that isn't your " +
                        "problem. Off by default - takes effect on the next time you (re)launch the " +
                        "game, since both thread pools are only created then."
            ),
        )
        Toggle(
            label = tr("Limit Shader Compiler Threads"),
            description = tr("Caps shader/pipeline compilation at 2 threads to avoid RAM-spike crashes. See \"What does this do?\" above."),
            initialCheckedState = NativeSettings::getLimitShaderCompilerThreads,
            onCheckedChanged = NativeSettings::setLimitShaderCompilerThreads,
        )
        InfoDialogButton(
            title = tr("Deep IR Optimizations"),
            explanation = tr(
                "What this does: after translating a game's PowerPC code, the recompiler runs a " +
                        "couple of cleanup passes on it before turning it into native machine code. This " +
                        "option makes two of those passes more thorough: dead code elimination (removing " +
                        "values that are calculated but never actually used) now keeps re-checking until " +
                        "nothing more can be removed, instead of a single pass, and a new pass merges short " +
                        "internal code blocks that only ever flow into each other into one, removing the " +
                        "jump between them. Both only ever remove work that is provably useless - neither " +
                        "changes what a game actually computes, and this is not the same thing as inlining " +
                        "a game's own function calls (not safely possible here, since PowerPC calls jump to " +
                        "an address only known at runtime).\n\n" +
                        "Honest limitations: this is new, and while both passes were written conservatively " +
                        "(only merging blocks that are provably safe to merge), any bug in the recompiler's " +
                        "cleanup logic can affect any game. Off by default for that reason, matching the " +
                        "other recompiler-related options on this screen - if you turn it on and a game " +
                        "develops a crash, black screen, or graphical glitch that didn't happen before, " +
                        "turning this back off is a quick way to check whether these passes are the cause."
            ),
        )
        Toggle(
            label = tr("Deep IR Optimizations"),
            description = tr("More thorough recompiler cleanup passes. See \"What does this do?\" above."),
            initialCheckedState = NativeSettings::isAdvancedIMLOptimizationsEnabled,
            onCheckedChanged = NativeSettings::setAdvancedIMLOptimizationsEnabled,
        )
        InfoDialogButton(
            title = tr("Audio/video sync"),
            explanation = tr(
                "What this does: the queued audio buffer is drained by your device's real audio " +
                        "clock but filled at the pace of the emulated game/video clock. Those two clocks " +
                        "are independent and slowly drift apart over long play sessions - without " +
                        "correction, the buffer keeps growing past its target size until it hits its hard " +
                        "limit, at which point a large chunk of audio has to be dropped all at once (an " +
                        "audible pop). This option continuously trims a very small amount (at most one " +
                        "audio block, a few milliseconds) of the oldest queued audio whenever the buffer " +
                        "drifts meaningfully past its target, so the correction happens gradually in the " +
                        "background instead of building up to that one large drop.\n\n" +
                        "Honest limitations: this does not stretch or crossfade the trimmed audio - it's a " +
                        "small, direct cut. That trades a rare, larger pop for occasional small waveform " +
                        "cuts, which could themselves be audible as faint clicks depending on the audio and " +
                        "your hardware. This hasn't been confirmed inaudible across long real play " +
                        "sessions yet, which is why it's off by default - try it during a long session and " +
                        "judge for yourself whether it sounds better or worse than the occasional pop."
            ),
        )
        Toggle(
            label = tr("Audio/video sync"),
            description = tr("Gradually corrects audio buffer drift instead of one large pop. See \"What does this do?\" above."),
            initialCheckedState = NativeSettings::getAudioVideoSync,
            onCheckedChanged = NativeSettings::setAudioVideoSync,
        )
        InfoDialogButton(
            title = tr("Fast Audio Resampling"),
            explanation = tr(
                "What this does: the Wii U's audio engine (AX) mixes every individual sound - " +
                        "every music track, sound effect, and voice - at its own pitch, and each game " +
                        "picks how each of those sounds gets resampled to the shared mixing rate: a more " +
                        "sophisticated filter (\"Tap\"), simple linear interpolation (\"Linear\"), or the " +
                        "cheapest option, nearest-neighbor with no interpolation at all (\"None\"). This " +
                        "option forces every sound to use the cheapest \"None\" method regardless of what " +
                        "the game asked for, trading audio fidelity for CPU headroom on the audio thread. " +
                        "It only affects what you hear - it doesn't change what the game itself thinks " +
                        "its own audio settings are.\n\n" +
                        "Honest limitation: this Wii U emulator hasn't actually implemented the fancier " +
                        "\"Tap\" filter yet - asking for it silently falls back to the same \"Linear\" " +
                        "method today. So in practice this toggle has one real effect: sounds that would " +
                        "have used Tap or Linear now use None instead. On a phone's built-in speaker the " +
                        "difference is usually subtle to inaudible; with headphones or a proper speaker, " +
                        "expect a slightly rougher/harsher sound in games with a lot of simultaneous audio. " +
                        "Off by default."
            ),
        )
        Toggle(
            label = tr("Fast Audio Resampling"),
            description = tr("Cheaper audio resampling for every sound, trading fidelity for CPU headroom. See \"What does this do?\" above."),
            initialCheckedState = NativeSettings::isFastAudioResamplingEnabled,
            onCheckedChanged = NativeSettings::setFastAudioResamplingEnabled,
        )
        InfoDialogButton(
            title = tr("Force Audio Downmix"),
            explanation = tr(
                "What this does: it only matters if \"TV channels\" (Audio Settings, not this " +
                        "screen) is set to Stereo or Mono, and only for games that actually use full " +
                        "5.1 audio internally - not all of them do. Today, when TV channels is Stereo, " +
                        "Cemu just takes the Front-Left/Front-Right channels and throws away Center, " +
                        "LFE (subwoofer), and the Surround/rear channels completely - it's not a real " +
                        "downmix, it's a cut. In Mono it's worse: everything except Front-Left is " +
                        "discarded, including Front-Right. That's what causes muffled dialogue or " +
                        "missing sound effects in games with surround audio. With this option on, " +
                        "those channels get mixed into Left/Right instead of thrown away, using the " +
                        "same Lo/Ro formula TV broadcasters use to fold 5.1 down to stereo (ITU-R " +
                        "BS.775): Center and Surround are attenuated by -3dB before being summed into " +
                        "the front channels; the LFE channel is intentionally left out, same as in that " +
                        "standard, since it only carries constant low-end with no dialogue or " +
                        "direction. For Mono, the same Lo/Ro mix is computed and then averaged into one " +
                        "channel - that averaging step isn't an official standard, just a safe way to " +
                        "combine the two without doubling the volume. The Gamepad's own audio never had " +
                        "true surround channels, but its Mono mode had the same bug of discarding the " +
                        "right channel outright - this option fixes that too, by averaging Left+Right " +
                        "instead.\n\n" +
                        "Honest limitations: this is new code, not tested on real hardware yet. Since " +
                        "several channels are now summed together instead of just one being copied, the " +
                        "result could in theory get louder than before in scenes with a lot of " +
                        "simultaneous surround audio (everything is still clamped to prevent ugly " +
                        "clipping, but it can still sound different than before) - if that happens, turn " +
                        "it back off. Takes effect immediately, without needing to restart the game. Off " +
                        "by default."
            ),
        )
        Toggle(
            label = tr("Force Audio Downmix"),
            description = tr("Mixes Center/LFE/Surround into Stereo/Mono output instead of discarding them. See \"What does this do?\" above."),
            initialCheckedState = NativeSettings::isForceAudioDownmixEnabled,
            onCheckedChanged = NativeSettings::setForceAudioDownmixEnabled,
        )
        InfoDialogButton(
            title = tr("Audio Buffer Sync"),
            explanation = tr(
                "What this actually does, honestly: the audio buffer is fed by the emulation " +
                        "thread (which can be busy with heavy graphics work) and drained by the " +
                        "device's real-time audio callback. Today, while waiting between refill " +
                        "attempts, the emulation thread already checks the buffer and shortens its " +
                        "wait when more data is needed soon (\"fast\" cadence, about 2.9ms instead of " +
                        "3ms). This option adds one more level: when the buffer is critically low - " +
                        "under about 2 blocks queued, close enough to start playing silence within " +
                        "the next cycle or two - it polls again as soon as the app's existing safety " +
                        "floor (1.7ms, unchanged) allows, instead of waiting for the normal fast " +
                        "cadence. This is only ever a read of how much audio is queued; it never adds " +
                        "a blocking wait between the audio callback thread and the emulation thread. " +
                        "A stricter, blocking synchronization between those two threads was " +
                        "deliberately not implemented, since forcing the real-time audio callback to " +
                        "wait on the emulation thread would risk priority inversion - the exact " +
                        "clicking/stutter this is meant to avoid, not fix.\n\n" +
                        "Honest limitations: this only changes how soon the app tries again to top " +
                        "up the buffer - it does not create new audio data faster, so it won't help " +
                        "if the actual cause of stutter is the audio-feeding code itself running too " +
                        "infrequently (for example, because it's tied to the emulated CPU's own " +
                        "scheduling loop rather than a dedicated thread). Off by default."
            ),
        )
        Toggle(
            label = tr("Audio Buffer Sync"),
            description = tr("Polls the audio backend faster when its buffer is about to run dry. See \"What does this do?\" above."),
            initialCheckedState = NativeSettings::isAudioBufferSyncEnabled,
            onCheckedChanged = NativeSettings::setAudioBufferSyncEnabled,
        )
        InfoDialogButton(
            title = tr("Memory Usage Brake"),
            explanation = tr(
                "What this actually does, honestly: this is NOT a way to pause loading a " +
                        "texture or shader the game is asking for right now - this is an emulator, " +
                        "not a game engine with adjustable quality, so every texture the Wii U " +
                        "requests is the one needed to draw that frame correctly; skipping it would " +
                        "just show a broken/missing texture, which is worse than the problem this is " +
                        "meant to solve. What it does instead: the app already discards textures it " +
                        "hasn't used in a while, gradually, a few at a time every frame, to avoid " +
                        "stutter. When your chosen ceiling is crossed, this option just makes that " +
                        "same cleanup happen all at once, right away, instead of trickling in over " +
                        "several seconds - speeding up housekeeping the app was already going to do, " +
                        "rather than pausing anything the game needs this instant.\n\n" +
                        "The ceiling is compared against this app's own memory usage (not the whole " +
                        "device's), checked roughly every 2 seconds in the background - not every " +
                        "frame, since memory usage doesn't change fast enough for that to be worth " +
                        "checking 60 times a second.\n\n" +
                        "Honest limitations: this only frees textures that were already safe to " +
                        "discard (unused for a little while) - it doesn't shrink the emulated Wii U's " +
                        "own memory, and in a scene where almost everything is actively in use it may " +
                        "not free much. This helps most in long sessions with a lot of scene/texture " +
                        "turnover, not necessarily at the single heaviest moment of one scene. Off by " +
                        "default."
            ),
        )
        Toggle(
            label = tr("Memory Usage Brake"),
            description = tr("Aggressively frees already-discardable textures once the app crosses the memory ceiling below. See \"What does this do?\" above."),
            initialCheckedState = NativeSettings::isMemoryUsageBrakeEnabled,
            onCheckedChanged = {
                memoryBrakeEnabled = it
                NativeSettings.setMemoryUsageBrakeEnabled(it)
            },
        )
        SingleSelection(
            label = tr("Memory Ceiling"),
            initialChoice = NativeSettings::getMemoryCeilingMB,
            onChoiceChanged = NativeSettings::setMemoryCeilingMB,
            choiceToString = { memoryCeilingToString(it) },
            enabled = memoryBrakeEnabled,
            choices = listOf(
                NativeSettings.MemoryCeiling.GB_2,
                NativeSettings.MemoryCeiling.GB_4,
                NativeSettings.MemoryCeiling.GB_6,
                NativeSettings.MemoryCeiling.GB_8,
                NativeSettings.MemoryCeiling.GB_12,
            ),
        )
        InfoDialogButton(
            title = tr("Limit to 30 FPS / Emulation speed"),
            explanation = tr(
                "What this actually does, honestly: this is the global default for two settings " +
                        "that genuinely change how fast the emulated console's own clock advances (CPU " +
                        "timer + vsync period), not just how the frames are displayed. \"Limit to 30 " +
                        "FPS\" locks games that target 60 FPS to a stable 30 FPS instead - useful on " +
                        "slower devices where a game can't consistently reach 60 FPS, trading peak " +
                        "framerate for a smoother, more consistent one. \"Emulation speed\" runs the " +
                        "whole game genuinely faster, from 100% (normal) up to 200% (2x) - while it's " +
                        "on, \"Limit to 30 FPS\" is disabled, since the two are mutually exclusive. " +
                        "Neither has any effect on a title where a graphic pack already defines its " +
                        "own custom vsync timing - that always takes priority, to avoid conflicting " +
                        "with a fix the pack's author already built for that specific game. Every game " +
                        "can also override either setting for itself only, in that game's own profile " +
                        "screen - leaving both there untouched means it just follows whatever is " +
                        "chosen here.\n\n" +
                        "Honest limitations: this changes real emulated timing, not a display trick - " +
                        "audio, animations and in-game logic all speed up or slow down together with " +
                        "it, so a game's music/sound effects will sound pitched up or down at anything " +
                        "other than 100%, exactly like physically speeding up a video would. Some " +
                        "games may not behave correctly, or may become unstable, at framerates or " +
                        "speeds far from what they were built for. Requires restarting the game to " +
                        "take effect. Off by default (30 FPS off, speed 100%)."
            ),
        )
        var limitToHalfFps by rememberSaveable { mutableStateOf(NativeSettings.getLimitToHalfFps()) }
        Toggle(
            label = tr("Limit to 30 FPS"),
            description = tr("Locks games that target 60 FPS to a stable 30 FPS instead. See \"What does this do?\" above."),
            initialCheckedState = { limitToHalfFps },
            onCheckedChanged = {
                limitToHalfFps = it
                NativeSettings.setLimitToHalfFps(it)
            },
        )
        Slider(
            label = tr("Emulation speed"),
            initialValue = NativeSettings::getEmulationSpeedPercent,
            valueFrom = 100,
            valueTo = 200,
            steps = 3,
            enabled = !limitToHalfFps,
            labelFormatter = {
                if (limitToHalfFps)
                    tr("Disabled while \"Limit to 30 FPS\" is on")
                else
                    tr("{0}% - the game genuinely plays at {1}x speed, not just the display", it, "%.2f".format(it / 100f))
            },
            onValueChange = NativeSettings::setEmulationSpeedPercent,
        )
    }
}
