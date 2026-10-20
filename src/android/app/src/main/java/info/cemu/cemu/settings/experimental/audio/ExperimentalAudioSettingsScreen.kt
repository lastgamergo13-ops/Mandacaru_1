package info.cemu.cemu.settings.experimental.audio

import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import info.cemu.cemu.common.ui.components.Button
import info.cemu.cemu.common.ui.components.ScreenContent
import info.cemu.cemu.common.ui.components.ScrollableDialog
import info.cemu.cemu.common.ui.components.Toggle
import info.cemu.cemu.common.ui.localization.tr
import info.cemu.cemu.nativeinterface.NativeSettings

/** Botao "Tap for details" que abre um dialogo rolavel (mesmo padrao da tela Experimental). */
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
 * Experimental/Hackers > Audio: todas as opcoes de audio experimentais, reunidas num so lugar
 * (antes ficavam soltas no meio da tela Experimental).
 */
@Composable
fun ExperimentalAudioSettingsScreen(navigateBack: () -> Unit) {
    ScreenContent(
        appBarText = tr("Audio"),
        navigateBack = navigateBack,
    ) {
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
    }
}
