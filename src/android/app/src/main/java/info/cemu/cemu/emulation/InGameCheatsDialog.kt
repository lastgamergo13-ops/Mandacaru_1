package info.cemu.cemu.emulation

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.MaterialTheme
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
import info.cemu.cemu.common.ui.localization.tr
import info.cemu.cemu.nativeinterface.NativeCheats

/**
 * In-game Cheats manager: lists the cheats belonging to whichever graphic packs are
 * currently active for the running game, and lets the player switch them on or off
 * immediately - no restart needed.
 *
 * This only covers cheats (see NativeCheats.kt) - it deliberately does NOT also expose
 * turning whole graphic packs (visual/texture mods) on or off from here. That's a
 * different, coarser operation (NativeGraphicPacks.setEnabled) which this codebase only
 * ever applies the next time the game is launched, not live - toggling it while playing
 * touches renderer state (shader/texture rules) with no synchronization against the
 * render thread, unlike cheats, which do have that (see
 * GraphicPack2::ApplyPatchGroupWithDependencies). Mixing a "changes instantly" cheat
 * list with a "needs a restart to take effect" mod list in the same in-game quick-access
 * dialog would be misleading, so mods stay in the regular Graphic Packs settings screen
 * for now.
 */
@Composable
fun InGameCheatsDialog(onDismiss: () -> Unit) {
    var cheats by remember { mutableStateOf(NativeCheats.getCheatsForCurrentGame().toList()) }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(tr("Cheats")) },
        text = {
            if (cheats.isEmpty()) {
                Text(tr("No cheats available for this game. Cheats come from graphic packs enabled for it - check the Graphic Packs settings screen."))
            } else {
                LazyColumn {
                    items(cheats, key = { it.packName + "/" + it.cheatName }) { cheat ->
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(vertical = 8.dp),
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            Column(modifier = Modifier.weight(1f)) {
                                Text(text = cheat.cheatName)
                                Text(
                                    text = cheat.packName,
                                    style = MaterialTheme.typography.bodySmall,
                                )
                            }
                            Switch(
                                checked = cheat.enabled,
                                onCheckedChange = { isChecked ->
                                    val succeeded = NativeCheats.setCheatEnabled(
                                        cheat.packName,
                                        cheat.cheatName,
                                        isChecked,
                                    )
                                    // Re-read from native rather than trusting isChecked/succeeded
                                    // blindly - a cheat can fail to apply (e.g. an
                                    // unresolvable dependency), and this keeps every
                                    // switch in the list showing the real current state,
                                    // including any dependency cheats this one pulled in
                                    // or released.
                                    if (succeeded) {
                                        cheats = NativeCheats.getCheatsForCurrentGame().toList()
                                    }
                                },
                            )
                        }
                    }
                }
            }
        },
        confirmButton = {},
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text(tr("Close"))
            }
        },
    )
}
