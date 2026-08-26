package info.cemu.cemu.common.ui.theme

import androidx.compose.ui.graphics.Color

/**
 * Paleta de cores usada nas telas com o novo visual estilo Steam Deck / SteamOS.
 * Extraída do protótipo Mandacaru_interface.html.
 *
 * Usada localmente pelas telas que já foram portadas para o novo visual
 * (por enquanto: tela de Configurações). Não substitui o MaterialTheme
 * global do app, para não afetar as telas que ainda não foram portadas.
 */
object SteamDeckColors {
    val Background = Color(0xFF171D25)
    val SidebarBackground = Color(0xFF1B2838)
    val CardBackground = Color(0xFF1B2838)
    val CardBackgroundHover = Color(0xFF223247)
    val ActiveItemBackground = Color(0xFF2A475E)
    val Accent = Color(0xFF66C0F4)
    val TextPrimary = Color(0xFFFFFFFF)
    val TextSecondary = Color(0xFF8F98A0)
    val TextBody = Color(0xFFC7D5E0)
    val Success = Color(0xFF4CBB17)
    val Danger = Color(0xFFFF5252)
    val IconBlack = Color(0xFF000000)
}
