package info.cemu.cemu.common.ui.components

import androidx.compose.animation.core.animateDpAsState
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LocalMinimumInteractiveComponentSize
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
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

private val SIDEBAR_WIDTH = 230.dp
private val TOGGLE_BUTTON_SIZE = 24.dp
private val TOGGLE_ICON_SIZE = 16.dp

/**
 * Barra lateral persistente (retrátil) com o botão de abrir/fechar "grudado"
 * na borda direita dela, deslizando junto conforme ela abre/fecha - igual ao
 * protótipo de referência. Ocupa a tela inteira (sem barra de título acima).
 *
 * Use no lugar de um Scaffold(topBar = ...): chame isso dentro de um Row,
 * junto com o conteúdo da tela ao lado.
 */
@Composable
fun AppSidebarWithToggle(
    expanded: Boolean,
    onToggleExpanded: () -> Unit,
    activeDestination: SidebarDestination?,
    actions: List<SidebarAction>,
    onCloseEmulator: () -> Unit,
) {
    val sidebarWidth by animateDpAsState(
        targetValue = if (expanded) SIDEBAR_WIDTH else 0.dp,
        label = "sidebarWidth",
    )

    Box(modifier = Modifier.fillMaxHeight()) {
        Column(
            modifier = Modifier
                .width(sidebarWidth)
                .fillMaxHeight()
                .background(SteamDeckColors.SidebarBackground)
                .verticalScroll(rememberScrollState()),
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

        // botão "grudado" na borda direita da barra lateral - anda junto com ela.
        // CompositionLocalProvider desliga o tamanho mínimo de toque de 48dp que o
        // Material3 força por padrão em qualquer IconButton, senão o botão renderiza
        // bem maior do que TOGGLE_BUTTON_SIZE por baixo dos panos.
        CompositionLocalProvider(LocalMinimumInteractiveComponentSize provides 0.dp) {
            IconButton(
                onClick = onToggleExpanded,
                modifier = Modifier
                    .padding(top = 20.dp, start = 16.dp)
                    .offset(x = sidebarWidth)
                    .size(TOGGLE_BUTTON_SIZE)
                    .clip(RoundedCornerShape(4.dp))
                    .background(SteamDeckColors.Accent),
            ) {
                Icon(
                    painter = painterResource(R.drawable.ic_more_vert),
                    contentDescription = if (expanded) tr("Hide menu") else tr("Show menu"),
                    tint = Color.Black,
                    modifier = Modifier.size(TOGGLE_ICON_SIZE),
                )
            }
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
        iconTint = SteamDeckColors.IconBlack,
        onClick = goToLibrary,
        destination = SidebarDestination.LIBRARY,
    ),
    SidebarAction(
        iconRes = R.drawable.ic_sidebar_media,
        label = tr("Media"),
        iconTint = SteamDeckColors.IconBlack,
        onClick = goToMedia,
        destination = SidebarDestination.MEDIA,
    ),
    SidebarAction(
        iconRes = R.drawable.ic_download,
        label = tr("Download"),
        iconTint = SteamDeckColors.IconBlack,
        onClick = goToDownload,
        destination = SidebarDestination.DOWNLOAD,
    ),
    SidebarAction(
        iconRes = R.drawable.ic_settings_general,
        label = tr("Settings"),
        iconTint = SteamDeckColors.IconBlack,
        onClick = goToSettings,
        destination = SidebarDestination.SETTINGS,
    ),
    SidebarAction(
        iconRes = R.drawable.ic_sidebar_titlemanager,
        label = tr("Title manager"),
        iconTint = SteamDeckColors.IconBlack,
        onClick = goToTitleManager,
        destination = SidebarDestination.TITLE_MANAGER,
    ),
    SidebarAction(
        iconRes = R.drawable.ic_sidebar_folder,
        label = tr("Open Cemu folder"),
        iconTint = SteamDeckColors.IconBlack,
        onClick = openCemuFolder,
    ),
    SidebarAction(
        iconRes = R.drawable.ic_sidebar_share,
        label = tr("Share log file"),
        iconTint = SteamDeckColors.IconBlack,
        onClick = shareLogFile,
    ),
    SidebarAction(
        iconRes = R.drawable.ic_sidebar_info,
        label = tr("About Cemu"),
        iconTint = SteamDeckColors.IconBlack,
        onClick = goToAboutCemu,
    ),
)
