package info.cemu.cemu.games.list

import android.content.Context
import android.content.Intent
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.documentfile.provider.DocumentFile
import info.cemu.cemu.common.ui.localization.tr
import info.cemu.cemu.nativeinterface.NativeGameTitles
import info.cemu.cemu.nativeinterface.NativeLogging
import info.cemu.cemu.nativeinterface.NativeSettings
import info.cemu.cemu.nativeinterface.toNativePath
import kotlinx.coroutines.delay

private const val PREFS = "cemu_first_run"
private const val KEY_SKIPPED = "games_folder_prompt_skipped"

private fun prefs(context: Context) = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

fun markGamesFolderPromptSkipped(context: Context) {
    prefs(context).edit().putBoolean(KEY_SKIPPED, true).apply()
}

fun isGamesFolderPromptSkipped(context: Context): Boolean =
    prefs(context).getBoolean(KEY_SKIPPED, false)

/**
 * Guarda a permissao da pasta escolhida. Tenta leitura+escrita e cai para so leitura (alguns celulares/pastas,
 * como a raiz de Download ou cartao SD, negam escrita e antes isso derrubava o fluxo sem adicionar a pasta).
 * Devolve true se alguma permissao persistente foi obtida.
 */
fun takeTreePermission(context: Context, uri: Uri): Boolean {
    val attempts = listOf(
        Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION,
        Intent.FLAG_GRANT_READ_URI_PERMISSION,
    )
    for (flags in attempts) {
        try {
            context.contentResolver.takePersistableUriPermission(uri, flags)
            return true
        } catch (e: Exception) {
            NativeLogging.log("[Pasta] takePersistableUriPermission($flags) falhou para $uri: ${e.message}")
        }
    }
    return false
}

/** Caminhos content:// configurados que o app NAO tem mais permissao para ler (ex.: app reinstalado). */
fun gamePathsWithoutPermission(context: Context): List<String> {
    val granted = try {
        context.contentResolver.persistedUriPermissions.filter { it.isReadPermission }
            .map { it.uri.toString() }
    } catch (e: Exception) {
        return emptyList()
    }
    // o caminho guardado no config usa a forma "nativa" (barras decodificadas depois do ultimo %3A); a permissao
    // persistida vem na forma original do Android. Compara as duas formas para nao dar falso "permissao perdida".
    val grantedForms = granted.flatMap { g ->
        listOf(g, try { Uri.parse(g).toNativePath() } catch (e: Exception) { g })
    }
    return NativeSettings.getGamesPaths().filter { path ->
        path.startsWith("content://") && grantedForms.none { g -> path == g || path.startsWith(g) }
    }
}

/**
 * Adiciona arquivos de jogo escolhidos diretamente (.wua/.wud/.wux/.wuhb) - alternativa quando a varredura de
 * pasta nao funciona no aparelho. Guarda a permissao de leitura e manda o emulador reler a biblioteca.
 */
fun addGameFiles(context: Context, uris: List<Uri>, gamesListViewModel: GamesListViewModel) {
    var added = 0
    for (uri in uris) {
        val granted = try {
            context.contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
            true
        } catch (e: Exception) {
            NativeLogging.log("[Pasta] permissao persistente do arquivo $uri falhou: ${e.message}")
            false
        }
        val path = uri.toNativePath()
        NativeLogging.log("[Pasta] arquivo de jogo escolhido: $path (permissao persistente: $granted)")
        if (!NativeSettings.getGamesPaths().contains(path)) {
            NativeSettings.addGamesPath(path)
            added++
        }
    }
    if (added > 0) {
        NativeSettings.saveSettings()
        gamesListViewModel.gamePathsHaveChanged()
        gamesListViewModel.refreshGames()
    }
}

/**
 * Fluxo de primeira execucao: pede a pasta dos jogos, mostra a varredura e permite pular.
 * Tambem aparece quando a pasta configurada perdeu a permissao (causa comum de biblioteca vazia).
 * [forceShow] abre o dialogo mesmo se o usuario ja tinha pulado (botao da tela vazia).
 */
@Composable
fun GamesFolderSetup(
    gamesListViewModel: GamesListViewModel,
    gamesCount: Int,
    forceShow: Boolean,
    onForceShowConsumed: () -> Unit,
) {
    val context = LocalContext.current
    var step by remember { mutableStateOf(0) } // 0 nada, 1 perguntar, 2 varrendo, 3 resultado
    var lostPermission by remember { mutableStateOf(false) }
    var scanStartCount by remember { mutableStateOf(0) }
    var folderName by remember { mutableStateOf("") }

    LaunchedEffect(Unit) {
        val noPaths = NativeSettings.getGamesPaths().isEmpty()
        val lost = gamePathsWithoutPermission(context)
        if (lost.isNotEmpty()) {
            lostPermission = true
            NativeLogging.log("[Pasta] sem permissao para ${lost.size} pasta(s) de jogos: $lost")
            step = 1
        } else if (noPaths && !isGamesFolderPromptSkipped(context)) {
            step = 1
        }
    }
    LaunchedEffect(forceShow) {
        if (forceShow) {
            step = 1
            onForceShowConsumed()
        }
    }

    val launcher = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
        if (uri == null) {
            step = 1
            return@rememberLauncherForActivityResult
        }
        val granted = takeTreePermission(context, uri)
        val doc = DocumentFile.fromTreeUri(context, uri)
        val path = doc?.uri?.toString() ?: uri.toString()
        folderName = doc?.name ?: path
        NativeLogging.log("[Pasta] pasta escolhida: $path (permissao persistente: $granted)")
        // remove entradas antigas que perderam a permissao e adiciona a nova
        gamePathsWithoutPermission(context).forEach { NativeSettings.removeGamesPath(it) }
        if (!NativeSettings.getGamesPaths().contains(path))
            NativeSettings.addGamesPath(path)
        NativeSettings.saveSettings()
        lostPermission = false
        scanStartCount = gamesCount
        gamesListViewModel.gamePathsHaveChanged()
        gamesListViewModel.refreshGames()
        step = 2
    }

    // varredura: termina quando o emulador avisa que acabou de varrer (isScanningTitles) e a lista parou de crescer.
    // Antes terminava apos 4 s sem jogo novo, o que dava "nenhum jogo" em pastas grandes/lentas mesmo com a varredura ainda rodando.
    LaunchedEffect(step) {
        if (step != 2) return@LaunchedEffect
        var last = -1
        var stable = 0
        var elapsed = 0
        while (elapsed < 300_000) {
            delay(500)
            elapsed += 500
            val now = gamesListViewModel.currentGamesCount()
            if (now != last) {
                last = now
                stable = 0
            } else stable += 500
            val scanning = try { NativeGameTitles.isScanningTitles() } catch (e: Throwable) { false }
            if (elapsed >= 1_000 && !scanning && stable >= 1_500) break
        }
        NativeLogging.log("[Pasta] varredura concluida na interface: ${gamesListViewModel.currentGamesCount()} jogo(s) em ${elapsed} ms")
        step = 3
    }

    when (step) {
        1 -> AlertDialog(
            onDismissRequest = {},
            title = { Text(if (lostPermission) tr("Permissao da pasta perdida") else tr("Pasta de jogos")) },
            text = {
                Text(
                    if (lostPermission)
                        tr("O app perdeu o acesso a pasta de jogos (acontece depois de reinstalar ou atualizar). Escolha a pasta novamente para os jogos voltarem a aparecer na biblioteca.")
                    else
                        tr("Escolha a pasta onde ficam seus jogos de Wii U (.wua, .wud, .wux, .wuhb ou pastas extraidas). O emulador vai varrer a pasta e subpastas ate encontrar os jogos.")
                )
            },
            confirmButton = { TextButton(onClick = { launcher.launch(null) }) { Text(tr("Escolher pasta")) } },
            dismissButton = {
                TextButton(onClick = {
                    markGamesFolderPromptSkipped(context)
                    step = 0
                }) { Text(tr("Pular")) }
            },
        )

        2 -> AlertDialog(
            onDismissRequest = {},
            title = { Text(tr("Procurando jogos...")) },
            text = {
                Column {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        CircularProgressIndicator(modifier = Modifier.size(28.dp))
                        Text(
                            tr("Varrendo: {0}", folderName),
                            modifier = Modifier.padding(start = 16.dp),
                        )
                    }
                    Text(
                        tr("Jogos encontrados ate agora: {0}", gamesCount),
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(top = 12.dp),
                    )
                }
            },
            confirmButton = { TextButton(onClick = { step = 3 }) { Text(tr("Ocultar")) } },
        )

        3 -> {
            val found = gamesCount
            val dismissAgain: (@Composable () -> Unit)? = if (found == 0) {
                @Composable { TextButton(onClick = { launcher.launch(null) }) { Text(tr("Escolher outra pasta")) } }
            } else null
            AlertDialog(
                onDismissRequest = { step = 0 },
                title = { Text(if (found > 0) tr("Varredura concluida") else tr("Nenhum jogo encontrado")) },
                text = {
                    Text(
                        if (found > 0)
                            tr("{0} jogo(s) encontrado(s).", found)
                        else
                            tr("Nao encontrei jogos nessa pasta. Confira se ela tem arquivos .wua/.wud/.wux/.wuhb (ou pastas code/content/meta) e tente escolher outra pasta. Se o problema continuar, use \"Share log file\" no menu e envie o log.")
                    )
                },
                confirmButton = { TextButton(onClick = { step = 0 }) { Text(tr("OK")) } },
                dismissButton = dismissAgain,
            )
        }
    }
}
