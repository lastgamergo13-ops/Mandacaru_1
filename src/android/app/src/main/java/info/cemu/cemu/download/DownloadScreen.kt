package info.cemu.cemu.download

import android.app.Activity
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import info.cemu.cemu.common.cemufiles.tryOpenCemuFolder
import info.cemu.cemu.common.cemufiles.tryShareLogFile
import info.cemu.cemu.common.ui.components.AppSidebar
import info.cemu.cemu.common.ui.components.SidebarDestination
import info.cemu.cemu.common.ui.components.SidebarToggleButton
import info.cemu.cemu.common.ui.components.rememberDefaultSidebarActions
import info.cemu.cemu.common.ui.localization.tr
import info.cemu.cemu.common.ui.theme.SteamDeckColors

/**
 * Seção de Downloads (jogos, DLCs, etc via loja/rede). Ainda não existe
 * a integração real por trás - esta tela já mostra a barra lateral e a
 * navegação real, com um estado vazio até essa funcionalidade existir.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun DownloadScreen(
    navigateBack: () -> Unit,
    goToSettings: () -> Unit,
    goToTitleManager: () -> Unit,
    goToMedia: () -> Unit,
    goToAboutCemu: () -> Unit,
) {
    var sidebarExpanded by rememberSaveable { mutableStateOf(true) }
    val context = LocalContext.current

    Scaffold(
        containerColor = SteamDeckColors.Background,
        topBar = {
            TopAppBar(
                navigationIcon = {
                    SidebarToggleButton(
                        expanded = sidebarExpanded,
                        onClick = { sidebarExpanded = !sidebarExpanded },
                    )
                },
                title = { Text(tr("Download")) },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = SteamDeckColors.Background,
                    titleContentColor = SteamDeckColors.TextPrimary,
                ),
            )
        },
    ) { scaffoldPadding ->
        Row(
            modifier = Modifier
                .padding(scaffoldPadding)
                .fillMaxSize(),
        ) {
            AppSidebar(
                expanded = sidebarExpanded,
                activeDestination = SidebarDestination.DOWNLOAD,
                onCloseEmulator = { (context as? Activity)?.finish() },
                actions = rememberDefaultSidebarActions(
                    goToLibrary = navigateBack,
                    goToMedia = goToMedia,
                    goToDownload = {},
                    goToSettings = goToSettings,
                    goToTitleManager = goToTitleManager,
                    openCemuFolder = { tryOpenCemuFolder(context) },
                    shareLogFile = { tryShareLogFile(context) },
                    goToAboutCemu = goToAboutCemu,
                ),
            )

            Box(
                modifier = Modifier.fillMaxSize(),
                contentAlignment = Alignment.Center,
            ) {
                Text(
                    text = tr("This feature isn't available yet"),
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
    }
}
