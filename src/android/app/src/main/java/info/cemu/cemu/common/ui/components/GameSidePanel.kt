package info.cemu.cemu.common.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.dropUnlessResumed
import info.cemu.cemu.common.ui.localization.tr
import info.cemu.cemu.common.ui.theme.SteamDeckColors
import info.cemu.cemu.games.GameIcon
import info.cemu.cemu.nativeinterface.NativeGameTitles
import info.cemu.cemu.nativeinterface.NativeGameTitles.Game

/**
 * Painel lateral que aparece dentro do contexto de um jogo (telas "Sobre o
 * título" e "Editar perfil do jogo"), no lugar da AppSidebar padrão - igual
 * ao vídeo de referência: capa, botão JOGAR, Favorito, Editar perfil do
 * jogo, Remove shader caches e o ID/Versão do título.
 */
@Composable
fun GameSidePanel(
    game: Game,
    onPlay: () -> Unit,
    onEditProfile: () -> Unit,
    onFavoriteChanged: (Boolean) -> Unit,
    onRemoveShaderCaches: () -> Unit,
) {
    val gameHasShaderCaches = rememberSaveable {
        NativeGameTitles.titleHasShaderCacheFiles(game.titleId)
    }

    Column(
        modifier = Modifier
            .width(210.dp)
            .fillMaxHeight()
            .background(SteamDeckColors.SidebarBackground)
            .padding(12.dp),
    ) {
        GameIcon(
            game = game,
            modifier = Modifier
                .fillMaxWidth()
                .padding(bottom = 12.dp),
        )

        Button(
            onClick = dropUnlessResumed(block = onPlay),
            colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF4CAF50)),
            shape = RoundedCornerShape(6.dp),
            modifier = Modifier.fillMaxWidth(),
        ) {
            Text(text = "▶ " + tr("Start"))
        }

        Toggle(
            label = tr("Favorite"),
            initialCheckedState = { game.isFavorite },
            onCheckedChanged = onFavoriteChanged,
        )

        Column(
            modifier = Modifier.padding(top = 8.dp),
            verticalArrangement = Arrangement.spacedBy(4.dp),
        ) {
            Text(
                text = tr("Edit game profile"),
                color = SteamDeckColors.TextPrimary,
                fontSize = 14.sp,
                modifier = Modifier
                    .clickable(onClick = dropUnlessResumed(block = onEditProfile))
                    .fillMaxWidth()
                    .padding(vertical = 10.dp),
            )
            Text(
                text = tr("Remove shader caches"),
                color = if (gameHasShaderCaches) SteamDeckColors.Danger else SteamDeckColors.TextSecondary,
                fontSize = 14.sp,
                modifier = Modifier
                    .clickable(
                        enabled = gameHasShaderCaches,
                        onClick = dropUnlessResumed(block = onRemoveShaderCaches),
                    )
                    .fillMaxWidth()
                    .padding(vertical = 10.dp),
            )
        }

        Text(
            text = tr("ID: {0} | Version: {1}", game.titleId, game.version),
            color = SteamDeckColors.TextSecondary,
            fontSize = 11.sp,
            fontWeight = FontWeight.Light,
            modifier = Modifier.padding(top = 8.dp),
        )
    }
}
