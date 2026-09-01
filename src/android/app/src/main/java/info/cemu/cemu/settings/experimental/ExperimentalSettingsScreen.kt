package info.cemu.cemu.settings.experimental

import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import info.cemu.cemu.common.ui.components.ScreenContent
import info.cemu.cemu.common.ui.localization.tr
import info.cemu.cemu.common.ui.theme.SteamDeckColors

/**
 * Ainda não tem nenhuma configuração experimental real por trás - existe só
 * para o botão "Experimental/Hackers" do menu de Configurações ter um destino
 * de verdade (igual ao vídeo de referência), até essas opções serem definidas.
 */
@Composable
fun ExperimentalSettingsScreen(navigateBack: () -> Unit) {
    ScreenContent(
        appBarText = tr("Experimental/Hackers"),
        navigateBack = navigateBack,
    ) {
        Card(
            modifier = Modifier
                .fillMaxWidth()
                .padding(8.dp),
            shape = RoundedCornerShape(10.dp),
            colors = CardDefaults.cardColors(
                containerColor = SteamDeckColors.CardBackground,
            ),
        ) {
            Text(
                text = tr("Experimental/Hackers"),
                color = SteamDeckColors.TextPrimary,
                fontWeight = FontWeight.Bold,
                fontSize = 16.sp,
                modifier = Modifier.padding(top = 16.dp, start = 16.dp, end = 16.dp),
            )
            Text(
                text = tr("Advanced and experimental system settings"),
                color = SteamDeckColors.TextSecondary,
                fontSize = 13.sp,
                modifier = Modifier.padding(16.dp),
            )
        }
    }
}
