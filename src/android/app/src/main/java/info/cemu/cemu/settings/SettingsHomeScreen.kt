package info.cemu.cemu.settings

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.dropUnlessResumed
import info.cemu.cemu.R
import info.cemu.cemu.common.ui.localization.tr
import info.cemu.cemu.common.ui.theme.SteamDeckColors

private data class SettingsMenuEntry(
    val iconRes: Int,
    val label: String,
    val onClick: () -> Unit,
)

@Composable
fun SettingsHomeScreen(
    goToGeneralSettings: () -> Unit,
    goToInputSettings: () -> Unit,
    goToGraphicsSettings: () -> Unit,
    goToEmulatedUSBDevicesSettings: () -> Unit,
    goToAudioSettings: () -> Unit,
    goToAccountSettings: () -> Unit,
    goToOverlaySettings: () -> Unit,
    goToExperimentalSettings: () -> Unit,
    navigateBack: () -> Unit
) {
    val entries = listOf(
        SettingsMenuEntry(
            iconRes = R.drawable.ic_settings_general,
            label = tr("General settings"),
            onClick = dropUnlessResumed(block = goToGeneralSettings),
        ),
        SettingsMenuEntry(
            iconRes = R.drawable.ic_settings_controls,
            label = tr("Input settings"),
            onClick = dropUnlessResumed(block = goToInputSettings),
        ),
        SettingsMenuEntry(
            iconRes = R.drawable.ic_settings_graphics,
            label = tr("Graphics settings"),
            onClick = dropUnlessResumed(block = goToGraphicsSettings),
        ),
        SettingsMenuEntry(
            iconRes = R.drawable.ic_settings_audio,
            label = tr("Audio settings"),
            onClick = dropUnlessResumed(block = goToAudioSettings),
        ),
        SettingsMenuEntry(
            iconRes = R.drawable.ic_settings_overlay,
            label = tr("Overlay settings"),
            onClick = dropUnlessResumed(block = goToOverlaySettings),
        ),
        SettingsMenuEntry(
            iconRes = R.drawable.ic_settings_usb,
            label = tr("Emulated USB Devices"),
            onClick = dropUnlessResumed(block = goToEmulatedUSBDevicesSettings),
        ),
        SettingsMenuEntry(
            iconRes = R.drawable.ic_settings_account,
            label = tr("Account settings"),
            onClick = dropUnlessResumed(block = goToAccountSettings),
        ),
        SettingsMenuEntry(
            iconRes = R.drawable.ic_settings_experimental,
            label = tr("Experimental/Hackers"),
            onClick = dropUnlessResumed(block = goToExperimentalSettings),
        ),
    )

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(SteamDeckColors.SidebarBackground)
    ) {
        // Cabeçalho "Voltar", igual ao protótipo (Imagem 1)
        Text(
            text = tr("Back"),
            color = SteamDeckColors.Accent,
            fontSize = 15.sp,
            fontWeight = FontWeight.Bold,
            modifier = Modifier
                .clickable(onClick = dropUnlessResumed(block = navigateBack))
                .padding(horizontal = 16.dp, vertical = 14.dp)
        )

        Box(
            modifier = Modifier
                .fillMaxWidth()
                .height(1.dp)
                .background(SteamDeckColors.TextSecondary.copy(alpha = 0.15f))
        )

        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .padding(top = 6.dp)
        ) {
            items(entries) { entry ->
                SettingsSidebarItem(entry)
            }
        }
    }
}

@Composable
private fun SettingsSidebarItem(entry: SettingsMenuEntry) {
    Row(
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(14.dp),
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = entry.onClick)
            .padding(vertical = 14.dp, horizontal = 16.dp),
    ) {
        Icon(
            painter = painterResource(entry.iconRes),
            contentDescription = null,
            tint = SteamDeckColors.IconBlack,
            modifier = Modifier.size(20.dp),
        )
        Text(
            text = entry.label,
            color = SteamDeckColors.TextSecondary,
            fontSize = 14.sp,
        )
    }
}
