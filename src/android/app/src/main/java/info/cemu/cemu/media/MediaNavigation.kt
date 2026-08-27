package info.cemu.cemu.media

import androidx.navigation.NavGraphBuilder
import androidx.navigation.NavHostController
import androidx.navigation.compose.composable
import info.cemu.cemu.about.AboutCemuRoute
import info.cemu.cemu.download.DownloadRoute
import info.cemu.cemu.settings.SettingsRoute
import info.cemu.cemu.titlemanager.TitleManagerRoute
import kotlinx.serialization.Serializable

@Serializable
object MediaRoute

fun NavGraphBuilder.mediaNavigation(navController: NavHostController) {
    composable<MediaRoute> {
        MediaScreen(
            navigateBack = { navController.popBackStack() },
            goToSettings = { navController.navigate(SettingsRoute) },
            goToTitleManager = { navController.navigate(TitleManagerRoute) },
            goToDownload = { navController.navigate(DownloadRoute) },
            goToAboutCemu = { navController.navigate(AboutCemuRoute) },
        )
    }
}
