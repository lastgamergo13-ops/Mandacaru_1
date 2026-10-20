package info.cemu.cemu.settings.experimental.qualityoflife

import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.lifecycle.viewmodel.compose.viewModel
import info.cemu.cemu.common.ui.components.Button
import info.cemu.cemu.common.ui.components.DateField
import info.cemu.cemu.common.ui.components.ScreenContent
import info.cemu.cemu.common.ui.components.ScrollableDialog
import info.cemu.cemu.common.ui.components.Slider
import info.cemu.cemu.common.ui.components.TimeField
import info.cemu.cemu.common.ui.components.Toggle
import info.cemu.cemu.common.ui.localization.tr
import info.cemu.cemu.nativeinterface.NativeCheats
import info.cemu.cemu.nativeinterface.NativeRenderer
import info.cemu.cemu.nativeinterface.NativeSettings
import java.time.Instant
import java.time.LocalDateTime
import java.time.LocalTime
import java.time.ZoneId
import java.time.ZoneOffset

/**
 * "What does this do?" button that opens a scrollable dialog with a longer explanation - the
 * same small helper ExperimentalSettingsScreen uses, kept as a private local copy here on
 * purpose: this tab is meant to stay self-contained and not get tangled up with the other
 * Experimental/Hackers screens as it grows.
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
 * "Quality of Life" tab, nested inside Experimental/Hackers: convenience features ported from an
 * old branch, kept apart from the other Experimental/Hackers toggles (texture compression,
 * recompiler options, etc.) so this still-untested batch doesn't mix in with those. Everything
 * here is off by default, same as the rest of Experimental/Hackers.
 */
@Composable
fun QualityOfLifeSettingsScreen(
    navigateBack: () -> Unit,
    goToTitleManager: () -> Unit,
    viewModel: QualityOfLifeSettingsViewModel = viewModel(),
) {
    val guiSettings by viewModel.guiSettings.collectAsState()

    ScreenContent(
        appBarText = tr("Quality of Life"),
        navigateBack = navigateBack,
    ) {
        InfoDialogButton(
            title = tr("Quality of Life"),
            explanation = tr(
                "Convenience features ported from an older Mandacaru branch, grouped here " +
                        "separately from the rest of Experimental/Hackers so they don't mix in with " +
                        "those. Like everything else in Experimental/Hackers, all of it is off by " +
                        "default and meant for advanced users - if something here misbehaves, it's " +
                        "always safe to switch it back off."
            ),
        )

        // --- Menu screen rotation --------------------------------------------------------
        InfoDialogButton(
            title = tr("Lock menu orientation"),
            explanation = tr(
                "What this does: the app's menu screens (game list, settings) already rotate " +
                        "freely with the sensor. This option locks them to whatever orientation " +
                        "they're in instead, right away - it doesn't force them back to portrait, " +
                        "so there's no sudden jump when you turn it on.\n\n" +
                        "This only affects the menu. The game screen always stays fixed in " +
                        "landscape either way, since the Wii U itself has no portrait mode - " +
                        "turning this on or off never changes anything during gameplay."
            ),
        )
        Toggle(
            label = tr("Lock menu orientation"),
            checked = guiSettings.lockMenuOrientation,
            description = tr("Stops the menu (not gameplay) from rotating with the sensor. See \"What does this do?\" above."),
            onCheckedChanged = viewModel::setLockMenuOrientation,
        )

        // --- Internal clock (RTC) -----------------------------------------------------------
        InfoDialogButton(
            title = tr("Custom internal clock"),
            explanation = tr(
                "What this does: the emulated Wii U's clock is normally set from your device's " +
                        "real clock, once, when a game starts - it then keeps advancing normally in " +
                        "real time for the rest of that session. With this on, it starts from the " +
                        "date/time you choose below instead, which can be useful for games with " +
                        "date-locked content (seasonal events, time-limited unlocks).\n\n" +
                        "Honest limitations: this is only read once at the start of a game, so it " +
                        "only takes effect the next time a game is (re)started, not for one that's " +
                        "already running - and the clock isn't \"frozen\" at your chosen date, it keeps " +
                        "advancing from there like normal. Dates before 2000-01-01 or after " +
                        "2100-01-01 aren't representable by the Wii U's own clock and are " +
                        "automatically pulled back to the nearest valid date. Off by default."
            ),
        )
        var rtcUseCustomTime by remember { mutableStateOf(!NativeSettings.getRtcUseSystemTime()) }
        // Kept in the device's local timezone throughout (matching what the emulated console
        // actually ends up using - see CafeSystem.cpp: cemu_initForGame()), converted to/from a
        // Unix epoch timestamp (seconds) only for storage/JNI.
        val initialCustomDateTime = remember {
            Instant.ofEpochSecond(NativeSettings.getRtcCustomTimestamp())
                .atZone(ZoneId.systemDefault())
        }
        var customDateMillis by remember {
            mutableLongStateOf(
                initialCustomDateTime.toLocalDate().atStartOfDay(ZoneOffset.UTC).toInstant()
                    .toEpochMilli()
            )
        }
        var customHour by remember { mutableIntStateOf(initialCustomDateTime.hour) }
        var customMinute by remember { mutableIntStateOf(initialCustomDateTime.minute) }

        fun saveCustomTimestamp() {
            // dateMillis from DatePicker is UTC-midnight of the picked calendar day (a Material3
            // quirk) - re-read just the Y/M/D from it, then combine with hour/minute and interpret
            // the result in the device's actual local timezone to get a real Unix timestamp.
            val localDate = Instant.ofEpochMilli(customDateMillis).atZone(ZoneOffset.UTC).toLocalDate()
            val localDateTime = LocalDateTime.of(localDate, LocalTime.of(customHour, customMinute))
            val epochSeconds = localDateTime.atZone(ZoneId.systemDefault()).toEpochSecond()
            NativeSettings.setRtcCustomTimestamp(epochSeconds)
        }

        Toggle(
            label = tr("Custom internal clock"),
            description = tr("Use a fixed date/time instead of the device clock. See \"What does this do?\" above."),
            initialCheckedState = { rtcUseCustomTime },
            onCheckedChanged = {
                rtcUseCustomTime = it
                NativeSettings.setRtcUseSystemTime(!it)
            },
        )
        if (rtcUseCustomTime) {
            DateField(
                label = tr("Custom date"),
                dateMillis = customDateMillis,
                onDateChange = {
                    customDateMillis = it
                    saveCustomTimestamp()
                },
            )
            TimeField(
                label = tr("Custom time"),
                hour = customHour,
                minute = customMinute,
                onTimeChange = { hour, minute ->
                    customHour = hour
                    customMinute = minute
                    saveCustomTimestamp()
                },
            )
        }

        // HUD (CPU por nucleo, VRAM, frame time, termico, layout horizontal) e opacidade do fundo: agora em
        // Settings > Overlay settings (juntos com as demais opcoes do HUD).

        // --- Quick screenshot ------------------------------------------------------------
        InfoDialogButton(
            title = tr("Quick screenshot"),
            explanation = tr(
                "Adds a \"Take Screenshot\" button to the in-game side menu once turned on " +
                        "below. The capture comes straight from the same clean rendering " +
                        "pipeline the desktop build's F12 hotkey uses - it never includes the " +
                        "FPS/notification HUD or the on-screen touch controls, on any platform, " +
                        "so there's nothing to hide or restore around the capture. Saved as a PNG " +
                        "to Pictures/Cemu, visible in your gallery app.\n\n" +
                        "Honest limitation: on Android 9 or older (a small and shrinking share of " +
                        "devices), saving can fail because this doesn't include an in-app prompt " +
                        "for the storage permission that's still required there - you'll see a " +
                        "\"Failed to save screenshot\" message rather than a silent failure, but it " +
                        "won't work on those devices without that. Off by default."
            ),
        )
        Toggle(
            label = tr("Quick screenshot"),
            description = tr("Adds a \"Take Screenshot\" button to the in-game menu. See \"What does this do?\" above."),
            initialCheckedState = NativeRenderer::isQuickScreenshotEnabled,
            onCheckedChanged = NativeRenderer::setQuickScreenshotEnabled,
        )

        // --- Quick controller profile switcher --------------------------------------------
        InfoDialogButton(
            title = tr("Quick controller profile switcher"),
            explanation = tr(
                "Adds a \"Switch controller profile\" button to the in-game side menu once " +
                        "turned on below. It lets you apply a saved controller MAPPING profile " +
                        "to an active controller slot instantly, without pausing or restarting " +
                        "the game - useful for switching between, say, a \"Bluetooth\" profile " +
                        "for a physical gamepad and a \"Touch\" profile for on-screen controls.\n\n" +
                        "This does NOT change which Wii U peripheral the game itself sees " +
                        "(GamePad/Pro Controller/Classic Controller) - only the mapping. Changing " +
                        "the peripheral type mid-game would confuse most titles, which don't " +
                        "expect that; this only reuses the same live profile-loading mechanism " +
                        "the emulator already relies on internally (e.g. for per-title controller " +
                        "profiles). Off by default."
            ),
        )
        Toggle(
            label = tr("Quick controller profile switcher"),
            description = tr("Adds a \"Switch controller profile\" button to the in-game menu. See \"What does this do?\" above."),
            initialCheckedState = NativeSettings::isQuickControllerProfileSwitcherEnabled,
            onCheckedChanged = NativeSettings::setQuickControllerProfileSwitcherEnabled,
        )

        // --- In-game cheats manager ---------------------------------------------------------
        InfoDialogButton(
            title = tr("In-game cheats manager"),
            explanation = tr(
                "Adds a \"Cheats\" button to the in-game side menu once turned on below. It " +
                        "lists every cheat from every graphic pack already active for the " +
                        "running game, each with its own switch, and applies changes instantly - " +
                        "no restart needed.\n\n" +
                        "This is not a new cheat engine - it exposes the same graphic pack patch " +
                        "system Cemu already has, just switchable one at a time instead of all " +
                        "or nothing. If a cheat needs a helper value defined by another cheat in " +
                        "the same pack, that helper is switched on automatically alongside it (and " +
                        "back off again once nothing needs it anymore, unless you also turned it " +
                        "on yourself directly).\n\n" +
                        "Only cheats, not visual/texture mods (whole graphic packs) - switching a " +
                        "mod on mid-game isn't something Cemu's renderer currently supports " +
                        "safely, so that stays in the regular Graphic Packs settings screen, " +
                        "applied the next time the game starts. Off by default."
            ),
        )
        Toggle(
            label = tr("In-game cheats manager"),
            description = tr("Adds a \"Cheats\" button to the in-game menu. See \"What does this do?\" above."),
            initialCheckedState = NativeCheats::isInGameCheatsEnabled,
            onCheckedChanged = NativeCheats::setInGameCheatsEnabled,
        )

        // --- WUX -> WUA converter shortcut -------------------------------------------------
        InfoDialogButton(
            title = tr("Convert WUX to WUA (Title manager)"),
            explanation = tr(
                "This does not add a new converter - the Title manager screen (reachable from " +
                        "the game list's menu) already converts a .wux (or unpacked) title into a " +
                        "compressed .wua archive; this is only a second shortcut to that exact " +
                        "same screen, placed here as requested. It converts the one title you " +
                        "pick, producing a single .wua file at a location you choose - not a " +
                        "folder, and not automatically placed next to the original.\n\n" +
                        "While going through this, a real bug was found and fixed in how the " +
                        "converter wrote its output file: a failed or partial disk write (e.g. a " +
                        "full storage device) was previously not checked at all and could silently " +
                        "produce a corrupt .wua with no warning. It now retries partial writes " +
                        "correctly and, if a write genuinely fails, cancels and reports an error " +
                        "instead of a false \"success\"."
            ),
        )
        Button(
            label = tr("Open title manager"),
            description = tr("Same screen as \"Title manager\" in the game list menu. See \"What does this do?\" above."),
            onClick = goToTitleManager,
        )
    }
}
