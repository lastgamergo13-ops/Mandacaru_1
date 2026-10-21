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
fun ExperimentalSettingsScreen(navigateBack: () -> Unit, goToQualityOfLifeSettings: () -> Unit, goToLogsSettings: () -> Unit, goToAudioSettings: () -> Unit) {
    val context = LocalContext.current
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
        Button(
            label = tr("Logs e Diagnostico"),
            description = tr("Todas as opcoes de log, exportar log e apagar arquivos de quarentena, num so lugar."),
            onClick = goToLogsSettings,
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
        // As opcoes de FSR/Frame Gen ficam recolhidas: so aparecem ao tocar em "FSR3 Frame Gen" (a tela nao fica poluida).
        var fsr3SectionExpanded by rememberSaveable { mutableStateOf(false) }
        Button(
            label = tr("FSR3 Frame Gen") + if (fsr3SectionExpanded) "  \u25B2" else "  \u25BC",
            description = tr("FSR upscale, frame generation e overlay do Frame Gen. Toque para mostrar ou esconder as opcoes."),
            onClick = { fsr3SectionExpanded = !fsr3SectionExpanded },
        )
        if (fsr3SectionExpanded) {
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
                choices = listOf(25, 50, 75, 100),
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
        }
        Button(
            label = tr("Quality of Life"),
            description = tr("Convenience features ported from an older branch, kept separate from everything else here. Off by default."),
            onClick = goToQualityOfLifeSettings,
        )
        InfoDialogButton(
            title = tr("Texture compression (BC1-BC5)"),
            explanation = tr(
                "Wii U games guardam muitas texturas nos formatos comprimidos BC1 a BC5 (cor, " +
                        "transparencia, mascaras e mapas de normal). A maioria das GPUs de celular nao " +
                        "le esses formatos direto. Este botao liga ou desliga os cinco de uma vez " +
                        "(BC1, BC2, BC3, BC4 e BC5).\n\n" +
                        "Ligado: o emulador converte as texturas BC para o formato que a sua GPU aceita " +
                        "(ETC2/EAC), quando ela aceita. Isso usa de 4 a 8 vezes menos memoria que decodificar " +
                        "para pixels normais. Desligado: as texturas BC sao decodificadas para pixels " +
                        "normais, que e o comportamento padrao e o mais seguro.\n\n" +
                        "Aviso: e experimental. No OpenGL ES esta opcao ficava sempre desativada (nos testes " +
                        "antigos ficou mais lenta e com pior qualidade) e o botao nao mudava nada. Agora ela " +
                        "funciona tambem no OpenGL ES, so para texturas 2D e 2D array (as demais continuam " +
                        "decodificadas). Se as texturas ficarem borradas, quadriculadas ou erradas depois de " +
                        "ligar, desligue de novo: o problema e esta opcao, nao o jogo."
            ),
        )
        Toggle(
            label = tr("Texture compression (BC1-BC5)"),
            description = tr("Liga/desliga BC1, BC2, BC3, BC4 e BC5 juntos. Veja a nota acima."),
            initialCheckedState = {
                NativeSettings.getTextureCompressionBC1() || NativeSettings.getTextureCompressionBC2() ||
                    NativeSettings.getTextureCompressionBC3() || NativeSettings.getTextureCompressionBC4() ||
                    NativeSettings.getTextureCompressionBC5()
            },
            onCheckedChanged = {
                NativeSettings.setTextureCompressionBC1(it)
                NativeSettings.setTextureCompressionBC2(it)
                NativeSettings.setTextureCompressionBC3(it)
                NativeSettings.setTextureCompressionBC4(it)
                NativeSettings.setTextureCompressionBC5(it)
            },
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
            title = tr("Performance"),
            explanation = tr(
                "Sobre \"overclock\": o Android nao permite que um aplicativo aumente o clock da CPU ou da GPU alem do " +
                        "limite do fabricante - isso so e possivel com root, e nao existe como o app fazer. No Eden, a opcao " +
                        "de \"overclock\" mexe no clock EMULADO do console, nao no do celular.\n\n" +
                        "O que da para fazer legitimamente e o modo de desempenho sustentado: o app pede ao sistema um " +
                        "nivel estavel de CPU/GPU que ele consegue manter, evitando a oscilacao de frequencia (throttling) " +
                        "quando o aparelho esquenta. Em muitos aparelhos isso da FPS mais constante, nao mais FPS maximo. " +
                        "So funciona se o aparelho declarar suporte; vale na proxima vez que voce abrir um jogo."
            ),
        )
        Toggle(
            label = tr("Sustained performance mode"),
            description = tr("Pede ao Android um nivel estavel de CPU/GPU (nao e overclock). Vale ao abrir o proximo jogo."),
            initialCheckedState = { NativeSettings.getDiagFlag(NativeSettings.DiagFlag.SUSTAINED_PERFORMANCE) },
            onCheckedChanged = { NativeSettings.setDiagFlag(NativeSettings.DiagFlag.SUSTAINED_PERFORMANCE, it) },
        )
        Button(
            label = tr("Audio"),
            description = tr("Audio/video sync, fast resampling, force downmix and audio buffer sync."),
            onClick = goToAudioSettings,
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
        InfoDialogButton(
            title = tr("Full sync at GX2DrawDone"),
            explanation = tr(
                "O que faz: quando o jogo pede para esperar a GPU terminar o desenho (GX2DrawDone), o " +
                        "emulador faz a CPU parar ate a GPU acabar tudo. E mais seguro, mas a CPU e a GPU " +
                        "deixam de trabalhar ao mesmo tempo: o tempo de cada quadro vira CPU + GPU em vez do " +
                        "maior dos dois. Em celulares onde a GPU e o gargalo (ex.: Moto G54) isso custa fps.\n\n" +
                        "Desligado: CPU e GPU trabalham em paralelo, o que pode aumentar o fps. Risco: alguns " +
                        "jogos que leem de volta a imagem ou usam consultas de oclusao podem ter pequenos " +
                        "defeitos visuais. Ligado por padrao. Precisa reiniciar o jogo."
            ),
        )
        Toggle(
            label = tr("Full sync at GX2DrawDone"),
            description = tr("Desligue para tentar mais fps quando a GPU for o gargalo. Veja \"O que faz\" acima."),
            initialCheckedState = NativeSettings::getGX2DrawDoneSync,
            onCheckedChanged = NativeSettings::setGX2DrawDoneSync,
        )
        InfoDialogButton(
            title = tr("Pular sombras"),
            explanation = tr(
                "O que faz: pula os desenhos que so escrevem profundidade, sem cor. Na pratica sao os mapas " +
                        "de sombra (e alguns pre-passes de profundidade). Eles custam bastante da GPU em " +
                        "celulares fracos; pulando, o fps sobe e o jogo pode chegar mais perto de 60 fps.\n\n" +
                        "Custo: as sombras dinamicas somem ou ficam erradas. Alguns jogos podem ter defeitos " +
                        "visuais. Desligado por padrao. Precisa reiniciar o jogo. " +
                        "O log mostra [PuladoDepth] com quantos desenhos foram pulados."
            ),
        )
        Toggle(
            label = tr("Pular sombras"),
            description = tr("Mais fps, sem sombras dinamicas. Veja \"O que faz\" acima."),
            initialCheckedState = NativeSettings::getSkipDepthOnlyDraws,
            onCheckedChanged = NativeSettings::setSkipDepthOnlyDraws,
        )
    }
}
