package info.cemu.cemu.download

import androidx.navigation.NavGraphBuilder
import androidx.navigation.NavHostController
import androidx.navigation.compose.composable
import info.cemu.cemu.about.AboutCemuRoute
import info.cemu.cemu.media.MediaRoute
import info.cemu.cemu.settings.SettingsRoute
import info.cemu.cemu.titlemanager.TitleManagerRoute
import kotlinx.serialization.Serializable

@Serializable
object DownloadRoute

fun NavGraphBuilder.downloadNavigation(navController: NavHostController) {
    composable<DownloadRoute> {
        DownloadScreen(
            navigateBack = { navController.popBackStack() },
            goToSettings = { navController.navigate(SettingsRoute) },
            goToTitleManager = { navController.navigate(TitleManagerRoute) },
            goToMedia = { navController.navigate(MediaRoute) },
            goToAboutCemu = { navController.navigate(AboutCemuRoute) },
        )
    }
}
