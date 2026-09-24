package info.cemu.cemu.emulation

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.FilterChip
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import info.cemu.cemu.common.ui.localization.tr
import info.cemu.cemu.nativeinterface.NativeInput

/**
 * Quality of Life (Experimental/Hackers): apply a saved controller mapping profile to an active
 * controller slot right now, without pausing or restarting the game.
 *
 * This does NOT switch the emulated Wii U peripheral (GamePad/Pro Controller/Classic Controller)
 * that the game itself sees - doing that mid-game would just confuse most titles, which don't
 * expect it. It switches which physical mapping (which buttons/device control that slot) is
 * active - e.g. a "Bluetooth" profile for a physical gamepad vs. a "Touch" profile for on-screen
 * controls - the same kind of live profile load the emulator already does on its own when a game
 * with a per-title profile assigned is launched.
 */
@Composable
fun QuickControllerProfileDialog(onDismiss: () -> Unit) {
    val activeControllerIndices = remember {
        (0 until NativeInput.MAX_CONTROLLERS).filter { !NativeInput.isControllerDisabled(it) }
    }
    val profiles = remember { NativeInput.getControllerProfiles().toList() }
    var selectedControllerIndex by remember { mutableIntStateOf(activeControllerIndices.firstOrNull() ?: 0) }
    var newProfileName by remember { mutableStateOf("") }
    var lastResultMessage by remember { mutableStateOf<String?>(null) }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(tr("Switch controller profile")) },
        text = {
            Column {
                if (activeControllerIndices.isEmpty()) {
                    Text(tr("No controller is currently active."))
                    return@Column
                }

                if (activeControllerIndices.size > 1) {
                    Row(modifier = Modifier.fillMaxWidth()) {
                        activeControllerIndices.forEach { index ->
                            FilterChip(
                                modifier = Modifier.padding(end = 4.dp),
                                selected = selectedControllerIndex == index,
                                onClick = { selectedControllerIndex = index },
                                label = { Text(tr("Controller {0}", index + 1)) },
                            )
                        }
                    }
                    HorizontalDivider(modifier = Modifier.padding(vertical = 8.dp))
                }

                if (profiles.isEmpty()) {
                    Text(tr("No saved profiles yet. Save the current mapping as a new profile below to get started."))
                } else {
                    Text(
                        text = tr("Saved profiles"),
                        fontWeight = FontWeight.Medium,
                        modifier = Modifier.padding(bottom = 4.dp),
                    )
                    Column {
                        profiles.forEach { profileName ->
                            TextButton(
                                modifier = Modifier.fillMaxWidth(),
                                onClick = {
                                    val applied = NativeInput.loadControllerProfile(selectedControllerIndex, profileName)
                                    lastResultMessage = if (applied)
                                        tr("Applied \"{0}\"", profileName)
                                    else
                                        tr("Failed to apply \"{0}\"", profileName)
                                },
                            ) {
                                Text(profileName, modifier = Modifier.fillMaxWidth())
                            }
                        }
                    }
                }

                HorizontalDivider(modifier = Modifier.padding(vertical = 8.dp))

                Text(
                    text = tr("Save current mapping as new profile"),
                    fontWeight = FontWeight.Medium,
                    modifier = Modifier.padding(bottom = 4.dp),
                )
                Row(modifier = Modifier.fillMaxWidth()) {
                    OutlinedTextField(
                        modifier = Modifier.weight(1f),
                        value = newProfileName,
                        onValueChange = { newProfileName = it },
                        singleLine = true,
                        label = { Text(tr("Profile name")) },
                    )
                    Button(
                        modifier = Modifier.padding(start = 8.dp),
                        enabled = activeControllerIndices.isNotEmpty()
                                && NativeInput.isValidControllerProfileName(newProfileName),
                        onClick = {
                            val saved = NativeInput.saveControllerProfile(selectedControllerIndex, newProfileName)
                            lastResultMessage = if (saved)
                                tr("Saved \"{0}\"", newProfileName)
                            else
                                tr("Failed to save \"{0}\"", newProfileName)
                            onDismiss()
                        },
                    ) {
                        Text(tr("Save"))
                    }
                }

                lastResultMessage?.let {
                    Text(text = it, modifier = Modifier.padding(top = 8.dp))
                }
            }
        },
        confirmButton = {
            TextButton(onClick = onDismiss) { Text(tr("Close")) }
        },
    )
}
