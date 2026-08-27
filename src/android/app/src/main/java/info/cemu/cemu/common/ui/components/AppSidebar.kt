package info.cemu.cemu.common.ui.components

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.expandHorizontally
import androidx.compose.animation.shrinkHorizontally
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import info.cemu.cemu.R
import info.cemu.cemu.common.ui.localization.tr
import info.cemu.cemu.common.ui.theme.SteamDeckColors

enum class SidebarDestination {
    LIBRARY, MEDIA, DOWNLOAD, SETTINGS, TITLE_MANAGER
}

data class SidebarAction(
    val iconRes: Int,
    val label: String,
    val iconTint: Color,
    val onClick: () -> Unit,
    val destination: SidebarDestination? = null,
)

/**
 * Barra lateral persistente (retrátil) usada em toda a navegação principal do app,
 * igual ao protótipo (Biblioteca / Mídia / Download / Configurações / Gerenciador de
 * títulos / Open cemu folder / Share log file / Sobre o emulador / Fecha emulador).
 *
 * Empurra o conteúdo ao lado (não é um overlay/modal) - use dentro de um Row junto
 * com o conteúdo da tela.
 */
@Composable
fun AppSidebar(
    expanded: Boolean,
    activeDestination: SidebarDestination?,
    actions: List<SidebarAction>,
    onCloseEmulator: () -> Unit,
) {
    AnimatedVisibility(
        visible = expanded,
        enter = expandHorizontally(),
        exit = shrinkHorizontally(),
    ) {
        Column(
            modifier = Modifier
                .width(230.dp)
                .fillMaxHeight()
                .background(SteamDeckColors.SidebarBackground),
        ) {
            actions.forEach { action ->
                SidebarRow(
                    action = action,
                    isActive = action.destination != null && action.destination == activeDestination,
                )
            }

            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(1.dp)
                    .padding(vertical = 8.dp)
                    .background(SteamDeckColors.TextSecondary.copy(alpha = 0.15f))
            )

            SidebarRow(
                action = SidebarAction(
                    iconRes = R.drawable.ic_close,
                    label = tr("Close emulator"),
                    iconTint = SteamDeckColors.Danger,
                    onClick = onCloseEmulator,
                ),
                isActive = false,
                labelColor = SteamDeckColors.Danger,
            )
        }
    }
}

@Composable
private fun SidebarRow(
    action: SidebarAction,
    isActive: Boolean,
    labelColor: Color = SteamDeckColors.TextSecondary,
) {
    Row(
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(14.dp),
        modifier = Modifier
            .fillMaxWidth()
            .background(if (isActive) SteamDeckColors.ActiveItemBackground else Color.Transparent)
            .clickable(onClick = action.onClick)
            .padding(vertical = 12.dp, horizontal = 16.dp),
    ) {
        Icon(
            painter = painterResource(action.iconRes),
            contentDescription = null,
            tint = action.iconTint,
            modifier = Modifier.size(20.dp),
        )
        Text(
            text = action.label,
            color = if (isActive) SteamDeckColors.TextPrimary else labelColor,
            fontSize = 14.sp,
        )
    }
}

/** Botão redondo azul que abre/fecha a AppSidebar - igual ao das screenshots/vídeo de referência. */
@Composable
fun SidebarToggleButton(expanded: Boolean, onClick: () -> Unit) {
    IconButton(
        onClick = onClick,
        modifier = Modifier
            .padding(start = 8.dp)
            .size(36.dp)
            .clip(CircleShape)
            .background(SteamDeckColors.Accent),
    ) {
        Icon(
            painter = painterResource(R.drawable.ic_more_vert),
            contentDescription = if (expanded) tr("Hide menu") else tr("Show menu"),
            tint = Color.Black,
        )
    }
}

/**
 * Monta a lista padrão de itens da AppSidebar (Biblioteca / Mídia / Download /
 * Configurações / Gerenciador de títulos / Open cemu folder / Share log file /
 * Sobre o emulador), na mesma ordem/cores usadas em todas as telas.
 *
 * Cada tela passa `onClick = {}` para o próprio item (destino em que já está).
 */
@Composable
fun rememberDefaultSidebarActions(
    goToLibrary: () -> Unit,
    goToMedia: () -> Unit,
    goToDownload: () -> Unit,
    goToSettings: () -> Unit,
    goToTitleManager: () -> Unit,
    openCemuFolder: () -> Unit,
    shareLogFile: () -> Unit,
    goToAboutCemu: () -> Unit,
): List<SidebarAction> = listOf(
    SidebarAction(
        iconRes = R.drawable.ic_sidebar_folder,
        label = tr("Library"),
        iconTint = Color(0xFFFFC107),
        onClick = goToLibrary,
        destination = SidebarDestination.LIBRARY,
    ),
    SidebarAction(
        iconRes = R.drawable.ic_sidebar_media,
        label = tr("Media"),
        iconTint = Color(0xFF4CAF50),
        onClick = goToMedia,
        destination = SidebarDestination.MEDIA,
    ),
    SidebarAction(
        iconRes = R.drawable.ic_download,
        label = tr("Download"),
        iconTint = Color(0xFF29B6F6),
        onClick = goToDownload,
        destination = SidebarDestination.DOWNLOAD,
    ),
    SidebarAction(
        iconRes = R.drawable.ic_settings_general,
        label = tr("Settings"),
        iconTint = SteamDeckColors.TextSecondary,
        onClick = goToSettings,
        destination = SidebarDestination.SETTINGS,
    ),
    SidebarAction(
        iconRes = R.drawable.ic_sidebar_titlemanager,
        label = tr("Title manager"),
        iconTint = Color(0xFFE0E0E0),
        onClick = goToTitleManager,
        destination = SidebarDestination.TITLE_MANAGER,
    ),
    SidebarAction(
        iconRes = R.drawable.ic_sidebar_folder,
        label = tr("Open Cemu folder"),
        iconTint = Color(0xFFFFC107),
        onClick = openCemuFolder,
    ),
    SidebarAction(
        iconRes = R.drawable.ic_sidebar_share,
        label = tr("Share log file"),
        iconTint = Color(0xFFFF7043),
        onClick = shareLogFile,
    ),
    SidebarAction(
        iconRes = R.drawable.ic_sidebar_info,
        label = tr("About Cemu"),
        iconTint = Color(0xFF29B6F6),
        onClick = goToAboutCemu,
    ),
)
