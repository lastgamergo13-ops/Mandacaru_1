package info.cemu.cemu.settings.experimental.logs

import android.widget.Toast
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import info.cemu.cemu.common.cemufiles.logFileExists
import info.cemu.cemu.common.cemufiles.tryShareLogFile
import info.cemu.cemu.common.cemufiles.tryShareUserDataFile
import info.cemu.cemu.common.cemufiles.userDataFileExists
import info.cemu.cemu.common.ui.components.Button
import info.cemu.cemu.common.ui.components.ScreenContent
import info.cemu.cemu.common.ui.components.ScrollableDialog
import info.cemu.cemu.common.ui.components.Toggle
import info.cemu.cemu.common.ui.localization.tr
import info.cemu.cemu.nativeinterface.NativeActiveSettings
import info.cemu.cemu.nativeinterface.NativeSettings
import java.io.File

private const val VK_TRACE_FILE = "vk_trace.txt"

// O teste de ordenacao estrita de memoria do JIT foi concluido (descartado como causa) e fica OCULTO para ninguem ligar sem querer.
// Para mostrar de novo: troque para true aqui E para true em kOrdenacaoEstritaDisponivel (BackendAArch64.cpp).
private const val MOSTRAR_ORDENACAO_ESTRITA = false

/** Botao "Tap for details" que abre um dialogo rolavel (mesmo padrao da tela Experimental). */
@Composable
private fun InfoDialogButton(title: String, explanation: String) {
    var showDialog by remember { mutableStateOf(false) }
    Button(label = title, description = tr("Tap for details"), onClick = { showDialog = true })
    if (showDialog) {
        ScrollableDialog(title = title, onDismissRequest = { showDialog = false }) {
            Text(text = explanation)
        }
    }
}

/**
 * "Logs e Diagnostico": reune num so lugar todas as opcoes de log/diagnostico, o botao de exportar e os botoes de apagar
 * arquivos auxiliares (quarentena etc.), para nao ficarem espalhados pelo emulador.
 */
@Composable
fun LogsSettingsScreen(navigateBack: () -> Unit) {
    val context = LocalContext.current
    var vkSyncDebug by rememberSaveable { mutableStateOf(NativeSettings.getVkSyncDebug()) }

    ScreenContent(
        appBarText = tr("Logs e Diagnostico"),
        navigateBack = navigateBack,
    ) {
        InfoDialogButton(
            title = tr("Logs e Diagnostico"),
            explanation = tr(
                "Esta tela reune as opcoes de log do emulador. Elas servem para descobrir por que um jogo " +
                        "trava, fecha ou mostra textura errada. Deixe desligadas no uso normal: algumas deixam o " +
                        "jogo mais lento.\n\n" +
                        "Fluxo recomendado: ligue a opcao pedida, reproduza o problema, volte aqui e use " +
                        "\"Exportar log principal\" para enviar o arquivo.\n\n" +
                        "As informacoes do diagnostico de texturas e do log do OpenGL vao para o log principal " +
                        "(log.txt)."
            ),
        )

        // ---- Exportar -------------------------------------------------------------------------------------------
        Button(
            label = tr("Exportar log principal (log.txt)"),
            description = tr("Escolha onde salvar/enviar o log. Inclui as linhas [GLDEBUG], [TexDiag], [Stall] e [Quarentena]."),
            onClick = {
                if (!logFileExists() || !tryShareLogFile(context))
                    Toast.makeText(context, tr("Nenhum log ainda. Abra um jogo primeiro."), Toast.LENGTH_LONG).show()
            },
        )
        Button(
            label = tr("Exportar log do Vulkan (vk_trace.txt)"),
            description = tr("So existe se o \"Vulkan diagnostic log\" abaixo foi ligado e a GPU travou."),
            onClick = {
                if (!userDataFileExists(VK_TRACE_FILE) || !tryShareUserDataFile(context, VK_TRACE_FILE))
                    Toast.makeText(context, tr("Nenhum log do Vulkan ainda. Ligue o diagnostico e reproduza o problema."), Toast.LENGTH_LONG).show()
            },
        )

        // ---- Opcoes de log --------------------------------------------------------------------------------------
        Toggle(
            label = tr("Log profundo do OpenGL"),
            description = tr(
                "Registra TODAS as mensagens do driver OpenGL ES (KHR_debug sincrono, todas as severidades, ate 6 por tipo) " +
                        "e pede um contexto de depuracao ao driver. Sem isso o log traz so 2 mensagens por sessao. " +
                        "Vale a partir do proximo jogo que voce abrir. Pode deixar o jogo um pouco mais lento."
            ),
            initialCheckedState = { NativeSettings.getDiagFlag(NativeSettings.DiagFlag.GL_DEBUG) },
            onCheckedChanged = { NativeSettings.setDiagFlag(NativeSettings.DiagFlag.GL_DEBUG, it) },
        )
        Toggle(
            label = tr("Diagnostico de texturas"),
            description = tr(
                "Guarda formato, dimensao, endereco e tempo das ultimas 64 texturas carregadas. Quando a GPU congela " +
                        "ou o Vulkan perde o dispositivo, as ultimas 24 vao para o log; a linha \"EM ANDAMENTO\" e a " +
                        "textura em que o emulador estava. Cargas acima de 100 ms tambem sao registradas na hora. " +
                        "Vale na hora."
            ),
            initialCheckedState = { NativeSettings.getDiagFlag(NativeSettings.DiagFlag.TEX_LOG) },
            onCheckedChanged = { NativeSettings.setDiagFlag(NativeSettings.DiagFlag.TEX_LOG, it) },
        )
        Toggle(
            label = tr("Vulkan diagnostic log"),
            description = tr(
                "Liga o diagnostico do Vulkan: espera cada submit da GPU terminar (MAIS LENTO) e, se a GPU " +
                        "travar (VK_ERROR_DEVICE_LOST), grava os shaders do submit que travou em vk_trace.txt. " +
                        "Use so para gerar o log e depois desligue. Vale na hora."
            ),
            initialCheckedState = { vkSyncDebug },
            onCheckedChanged = {
                vkSyncDebug = it
                NativeSettings.setVkSyncDebug(it)
            },
        )

        if (MOSTRAR_ORDENACAO_ESTRITA) {
            InfoDialogButton(
                title = tr("Ordenacao estrita de memoria do JIT"),
                explanation = tr(
                    "O que e: teste para descobrir se um crash vem da reordenacao de memoria do processador ARM. " +
                            "O PowerPC do Wii U e o ARM do celular podem executar leituras e gravacoes de memoria em ordem " +
                            "diferente entre os nucleos; se o jogo depende de uma ordem que o ARM nao garante, ele recebe " +
                            "dados velhos ou ponteiros nulos e fecha.\n\n" +
                            "O que faz: coloca uma barreira de memoria (dmb ish) antes de TODA leitura e depois de TODA " +
                            "gravacao do codigo do jogo. Isso forca a ordem correta.\n\n" +
                            "Cuidado: deixa o jogo MUITO mais lento. Serve so para teste: ligue, abra o jogo que crasha e " +
                            "veja se ele passa do ponto do crash. Se passar, a causa e a ordem de memoria. Depois desligue.\n\n" +
                            "Vale a partir do PROXIMO jogo que voce abrir (feche o jogo atual antes). No log aparece a linha " +
                            "\"[JIT] MODO DE TESTE: ordenacao estrita de memoria LIGADA\"."
                ),
            )
            Toggle(
                label = tr("Ordenacao estrita de memoria do JIT (teste)"),
                description = tr(
                    "Barreira de memoria em toda leitura/gravacao do jogo. MUITO mais lento; so para testar crashes. " +
                            "Vale no proximo jogo que voce abrir."
                ),
                initialCheckedState = { NativeSettings.getDiagFlag(NativeSettings.DiagFlag.JIT_STRICT_ORDERING) },
                onCheckedChanged = { NativeSettings.setDiagFlag(NativeSettings.DiagFlag.JIT_STRICT_ORDERING, it) },
            )
        }

        Toggle(
            label = tr("Reduzir memoria informada ao jogo em 128 MB (teste)"),
            description = tr(
                "Alguns jogos crasham por 'estouro interno' quando o emulador informa mais memoria do que o Wii U real tem " +
                        "(o emulador reserva menos memoria para o sistema). Esta opcao informa 128 MB a menos ao jogo. " +
                        "Use para testar crashes do Watch Dogs. Vale no proximo jogo que voce abrir."
            ),
            initialCheckedState = { NativeSettings.getDiagFlag(NativeSettings.DiagFlag.REDUCE_GAME_MEM) },
            onCheckedChanged = { NativeSettings.setDiagFlag(NativeSettings.DiagFlag.REDUCE_GAME_MEM, it) },
        )

        Toggle(
            label = tr("Preferir nucleos rapidos da CPU (teste)"),
            description = tr(
                "Mantem as threads principais do emulador (nucleos do Wii U e GPU) nos nucleos rapidos do processador. " +
                        "Pode reduzir a lentidao dos primeiros segundos (ex.: o primeiro video do jogo). So age se o aparelho " +
                        "tiver pelo menos 4 nucleos rapidos. Vale no proximo jogo que voce abrir."
            ),
            initialCheckedState = { NativeSettings.getDiagFlag(NativeSettings.DiagFlag.PREFER_BIG_CORES) },
            onCheckedChanged = { NativeSettings.setDiagFlag(NativeSettings.DiagFlag.PREFER_BIG_CORES, it) },
        )

        Toggle(
            label = tr("Aumentar volume do audio (x2)"),
            description = tr(
                "Dobra o volume do jogo com um limitador suave para nao estourar. Use se o som estiver baixo mesmo com " +
                        "o volume da TV em 100%. Vale no proximo jogo que voce abrir."
            ),
            initialCheckedState = { NativeSettings.getDiagFlag(NativeSettings.DiagFlag.AUDIO_BOOST) },
            onCheckedChanged = { NativeSettings.setDiagFlag(NativeSettings.DiagFlag.AUDIO_BOOST, it) },
        )

        InfoDialogButton(
            title = tr("Atraso na partida das threads do jogo"),
            explanation = tr(
                "O que e: teste para uma 'corrida de inicializacao'. Alguns jogos criam uma thread que logo usa um objeto " +
                        "global que outra thread ainda esta criando. No Wii U real isso funciona por causa do tempo; no celular " +
                        "a thread secundaria pode chegar antes e o jogo fecha com ponteiro nulo.\n\n" +
                        "O que faz: faz a thread CDelayedArchivesOpenThread (a unica que comprovadamente corre com a principal) " +
                        "esperar 100 ms antes de comecar, dando tempo para a thread principal terminar de preparar o objeto. As " +
                        "demais threads nao sao atrasadas.\n\n" +
                        "Como usar: ligue, feche o jogo, abra o jogo que crasha (ex.: Watch Dogs) e veja se ele passa do ponto do " +
                        "crash. Se passar, a causa e essa corrida. Vale a partir do PROXIMO jogo que voce abrir. " +
                        "No log aparece \"[Thread] atraso de partida\"."
            ),
        )
        Toggle(
            label = tr("Atraso na partida das threads do jogo (teste)"),
            description = tr(
                "A thread CDelayedArchivesOpenThread espera 100 ms antes de comecar (as outras nao). So para testar crashes de " +
                        "ponteiro nulo no inicio do jogo. Vale no proximo jogo que voce abrir."
            ),
            initialCheckedState = { NativeSettings.getDiagFlag(NativeSettings.DiagFlag.THREAD_START_DELAY) },
            onCheckedChanged = { NativeSettings.setDiagFlag(NativeSettings.DiagFlag.THREAD_START_DELAY, it) },
        )

        InfoDialogButton(
            title = tr("Vigia de memoria, tela e audio"),
            explanation = tr(
                "O que e: diagnostico para achar (1) quem corrompe a memoria do jogo, (2) por que a tela fica preta e (3) por que " +
                        "nao ha som.\n\n" +
                        "O que faz: a cada 2 segundos escreve no log a cor media da imagem na tela ([Tela]) e o volume do audio " +
                        "([Audio]); e vigia uma area da memoria do jogo e registra quem estava rodando quando um ponteiro foi " +
                        "trocado por lixo ([Vigia]).\n\n" +
                        "Cuidado: pode causar pequenas travadas a cada 2 s (a leitura da tela). Use so para diagnostico e desligue depois. " +
                        "Vale a partir do PROXIMO jogo que voce abrir."
            ),
        )
        Toggle(
            label = tr("Vigia de memoria, tela e audio (diagnostico)"),
            description = tr(
                "Registra no log a cor da tela, o volume do audio e corrupcoes de memoria. So para diagnostico. " +
                        "Vale no proximo jogo que voce abrir."
            ),
            initialCheckedState = { NativeSettings.getDiagFlag(NativeSettings.DiagFlag.GUEST_WATCH) },
            onCheckedChanged = { NativeSettings.setDiagFlag(NativeSettings.DiagFlag.GUEST_WATCH, it) },
        )

        Toggle(
            label = tr("Despejar shaders (dump/shaders)"),
            description = tr(
                "Salva o codigo de cada shader que o jogo compila, com o hash no nome do arquivo. Serve para analisar o " +
                        "shader exato que derruba a GPU (os hashes aparecem no vk_trace.txt e na quarentena). Ligue ANTES de abrir " +
                        "o jogo, reproduza o problema e use \"Exportar shaders\" abaixo. Gera muitos arquivos; desligue depois."
            ),
            initialCheckedState = { NativeSettings.getDiagFlag(NativeSettings.DiagFlag.DUMP_SHADERS) },
            onCheckedChanged = { NativeSettings.setDiagFlag(NativeSettings.DiagFlag.DUMP_SHADERS, it) },
        )
        Button(
            label = tr("Exportar shaders despejados (.zip)"),
            description = tr("Compacta a pasta dump/shaders em um .zip e abre o seletor para enviar."),
            onClick = {
                val zipName = "shaders_dump.zip"
                val ok = try {
                    val userData = File(NativeActiveSettings.getUserDataPath())
                    val dir = userData.resolve("dump/shaders")
                    val files = dir.listFiles()?.filter { it.isFile } ?: emptyList()
                    if (files.isEmpty()) false else {
                        java.util.zip.ZipOutputStream(userData.resolve(zipName).outputStream().buffered()).use { zip ->
                            files.forEach { f ->
                                zip.putNextEntry(java.util.zip.ZipEntry(f.name))
                                f.inputStream().use { it.copyTo(zip) }
                                zip.closeEntry()
                            }
                        }
                        tryShareUserDataFile(context, zipName)
                    }
                } catch (_: Throwable) {
                    false
                }
                if (!ok)
                    Toast.makeText(context, tr("Nenhum shader despejado ainda. Ligue \"Despejar shaders\" e abra o jogo."), Toast.LENGTH_LONG).show()
            },
        )
        Button(
            label = tr("Apagar shaders despejados"),
            description = tr("Remove a pasta dump/shaders e o .zip para o proximo teste comecar limpo."),
            onClick = {
                val userData = File(NativeActiveSettings.getUserDataPath())
                val removed = userData.resolve("dump/shaders").deleteRecursively()
                userData.resolve("shaders_dump.zip").delete()
                Toast.makeText(context, if (removed) tr("Shaders despejados apagados.") else tr("Nada para apagar."), Toast.LENGTH_LONG).show()
            },
        )

        // ---- Limpeza --------------------------------------------------------------------------------------------
        Toggle(
            label = tr("Limpar quarentena ao trocar de jogo"),
            description = tr(
                "Quando voce abre um jogo DIFERENTE do anterior, o emulador apaga os arquivos de quarentena " +
                        "(shaders/pipelines pulados depois de um crash). Abrir o mesmo jogo de novo NAO apaga, porque " +
                        "e justamente nessa abertura que a quarentena protege contra o crash."
            ),
            // a chave guardada e "limpeza DESLIGADA"; o botao mostra o contrario
            initialCheckedState = { !NativeSettings.getDiagFlag(NativeSettings.DiagFlag.AUTO_CLEAN_OFF) },
            onCheckedChanged = { NativeSettings.setDiagFlag(NativeSettings.DiagFlag.AUTO_CLEAN_OFF, !it) },
        )
        Button(
            label = tr("Apagar arquivos de quarentena agora"),
            description = tr("Apaga vk_quarantine*.txt e gl_shader_quarantine.txt. Use se um jogo perdeu efeitos/draws depois de um crash."),
            onClick = {
                val n = NativeSettings.deleteQuarantineFiles()
                Toast.makeText(
                    context,
                    if (n > 0) tr("Quarentena apagada ($n arquivo(s)).") else tr("Nenhum arquivo de quarentena para apagar."),
                    Toast.LENGTH_LONG,
                ).show()
            },
        )
        Button(
            label = tr("Apagar log do Vulkan (vk_trace.txt)"),
            description = tr("Remove o arquivo de diagnostico do Vulkan para o proximo teste comecar limpo."),
            onClick = {
                val deleted = File(NativeActiveSettings.getUserDataPath()).resolve(VK_TRACE_FILE).delete()
                Toast.makeText(
                    context,
                    if (deleted) tr("vk_trace.txt apagado.") else tr("Nenhum vk_trace.txt para apagar."),
                    Toast.LENGTH_LONG,
                ).show()
            },
        )
    }
}
