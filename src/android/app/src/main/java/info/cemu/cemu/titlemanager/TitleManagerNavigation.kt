package info.cemu.cemu.titlemanager

import androidx.navigation.NavGraphBuilder
import androidx.navigation.NavHostController
import androidx.navigation.compose.composable
import info.cemu.cemu.about.AboutCemuRoute
import info.cemu.cemu.download.DownloadRoute
import info.cemu.cemu.media.MediaRoute
import info.cemu.cemu.settings.SettingsRoute
import kotlinx.serialization.Serializable

@Serializable
object TitleManagerRoute

fun NavGraphBuilder.titleManagerNavigation(navController: NavHostController) {
    composable<TitleManagerRoute> {
        TitleManagerScreen(
            navigateBack = { navController.popBackStack() },
            goToSettings = { navController.navigate(SettingsRoute) },
            goToMedia = { navController.navigate(MediaRoute) },
            goToDownload = { navController.navigate(DownloadRoute) },
            goToAboutCemu = { navController.navigate(AboutCemuRoute) },
        )
    }
}
