package info.cemu.cemu.common.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.unit.dp
import info.cemu.cemu.common.ui.theme.SteamDeckColors

/**
 * Cartão escuro arredondado usado em cada item das telas de configurações
 * (Toggle, Slider, SingleSelection), igual ao protótipo de referência.
 */
@Composable
fun SettingsCard(
    modifier: Modifier = Modifier,
    content: @Composable () -> Unit,
) {
    Box(
        modifier = modifier
            .fillMaxWidth()
            .padding(vertical = 4.dp)
            .clip(RoundedCornerShape(10.dp))
            .background(SteamDeckColors.CardBackground),
    ) {
        content()
    }
}
