package info.cemu.cemu.download

import android.app.Activity
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Tab
import androidx.compose.material3.TabRow
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import info.cemu.cemu.common.cemufiles.tryOpenCemuFolder
import info.cemu.cemu.common.cemufiles.tryOpenUrl
import info.cemu.cemu.common.cemufiles.tryShareLogFile
import info.cemu.cemu.common.ui.components.AppSidebarWithToggle
import info.cemu.cemu.common.ui.components.SidebarDestination
import info.cemu.cemu.common.ui.components.rememberDefaultSidebarActions
import info.cemu.cemu.common.ui.localization.tr
import info.cemu.cemu.common.ui.theme.SteamDeckColors

private const val EMULATOR_RELEASES_URL = "https://github.com/Ango299/Mandacaru_Wii_U_Android/releases"
private const val GRAPHIC_PACKS_URL = "https://github.com/cemu-project/cemu_graphic_packs"

private data class TurnipDriverLink(val developerName: String, val url: String)

private val turnipDriverLinks = listOf(
    TurnipDriverLink("K11MCH1", "https://github.com/K11MCH1/AdrenoToolsDrivers/releases"),
    TurnipDriverLink("StevenMXZ", "https://github.com/StevenMXZ/Adreno-Tools-Drivers/releases"),
    TurnipDriverLink("The412Banner", "https://github.com/The412Banner/Banners-Turnip/releases"),
    TurnipDriverLink("nihui", "https://github.com/nihui/mesa-turnip-android-driver/releases"),
    TurnipDriverLink("Vauzi-17", "https://github.com/Vauzi-17/710/releases"),
    TurnipDriverLink("MrPurple666", "https://github.com/MrPurple666/purple-turnip/releases"),
)

/**
 * Seção de Download: atualização do emulador, Graphic Packs e drivers
 * Turnip/Adreno de vários desenvolvedores - os links são os mesmos do
 * Mandacaru_interface.html.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun DownloadScreen(
    navigateBack: () -> Unit,
    goToSettings: () -> Unit,
    goToTitleManager: () -> Unit,
    goToMedia: () -> Unit,
    goToAboutCemu: () -> Unit,
    goToGraphicPacksManager: () -> Unit,
) {
    var sidebarExpanded by rememberSaveable { mutableStateOf(true) }
    var selectedTab by rememberSaveable { mutableIntStateOf(0) }
    val context = LocalContext.current

    Scaffold(
        containerColor = SteamDeckColors.Background,
    ) { scaffoldPadding ->
        Row(
            modifier = Modifier
                .padding(scaffoldPadding)
                .fillMaxSize(),
        ) {
            AppSidebarWithToggle(
                expanded = sidebarExpanded,
                onToggleExpanded = { sidebarExpanded = !sidebarExpanded },
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

            Column(modifier = Modifier.fillMaxSize()) {
                TabRow(
                    selectedTabIndex = selectedTab,
                    containerColor = SteamDeckColors.Background,
                    contentColor = SteamDeckColors.Accent,
                ) {
                    Tab(
                        selected = selectedTab == 0,
                        onClick = { selectedTab = 0 },
                        text = { Text(tr("Emulator update")) },
                    )
                    Tab(
                        selected = selectedTab == 1,
                        onClick = { selectedTab = 1 },
                        text = { Text(tr("Graphic packs")) },
                    )
                    Tab(
                        selected = selectedTab == 2,
                        onClick = { selectedTab = 2 },
                        text = { Text(tr("Download Turnip/Adreno")) },
                    )
                }

                Column(
                    modifier = Modifier
                        .fillMaxSize()
                        .verticalScroll(rememberScrollState())
                        .padding(16.dp),
                    verticalArrangement = Arrangement.spacedBy(12.dp),
                ) {
                    when (selectedTab) {
                        0 -> DownloadPanel(
                            title = tr("Mandacaru Wii U Android"),
                            description = tr("Access the emulator's official repository."),
                        ) {
                            DownloadLinkButton(tr("Download from GitHub")) {
                                tryOpenUrl(context, EMULATOR_RELEASES_URL)
                            }
                        }

                        1 -> DownloadPanel(
                            title = tr("Official Graphic Packs"),
                            description = tr("Official graphic packs repository for better resolution and performance."),
                        ) {
                            DownloadLinkButton(tr("Download from GitHub")) {
                                tryOpenUrl(context, GRAPHIC_PACKS_URL)
                            }
                            DownloadLinkButton(
                                text = tr("Manage installed graphic packs"),
                                containerColor = SteamDeckColors.ActiveItemBackground,
                                onClick = goToGraphicPacksManager,
                            )
                        }

                        2 -> DownloadPanel(
                            title = tr("Turnip / Adreno Drivers (Multiple Developers)"),
                            description = tr("Pick a developer to download optimized drivers for Adreno GPUs:"),
                        ) {
                            turnipDriverLinks.forEach { driver ->
                                DownloadLinkButton(driver.developerName) {
                                    tryOpenUrl(context, driver.url)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun DownloadPanel(
    title: String,
    description: String,
    content: @Composable ColumnScope.() -> Unit,
) {
    Text(text = title, color = SteamDeckColors.TextPrimary, fontSize = 18.sp, fontWeight = FontWeight.Bold)
    Text(text = description, color = SteamDeckColors.TextSecondary, fontSize = 14.sp)
    Column(
        verticalArrangement = Arrangement.spacedBy(8.dp),
        modifier = Modifier.padding(top = 6.dp),
    ) {
        content()
    }
}

@Composable
private fun DownloadLinkButton(
    text: String,
    containerColor: Color = Color(0xFF4CBB17),
    onClick: () -> Unit,
) {
    Button(
        onClick = onClick,
        shape = RoundedCornerShape(6.dp),
        colors = ButtonDefaults.buttonColors(containerColor = containerColor),
        modifier = Modifier.fillMaxWidth(),
    ) {
        Text(text)
    }
}
