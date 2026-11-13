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
import info.cemu.cemu.common.ui.components.SingleSelection
import info.cemu.cemu.common.ui.components.Toggle
import info.cemu.cemu.common.ui.localization.tr
import info.cemu.cemu.nativeinterface.NativeActiveSettings
import info.cemu.cemu.nativeinterface.NativeSettings
import java.io.File

private const val VK_TRACE_FILE = "vk_trace.txt"

// O teste de ordenacao estrita de memoria do JIT foi concluido (descartado como causa) e fica OCULTO para ninguem ligar sem querer.
// Para mostrar de novo: troque para true aqui E para true em kOrdenacaoEstritaDisponivel (BackendAArch64.cpp).
private const val MOSTRAR_ORDENACAO_ESTRITA = true // reativado: repetir o teste em multi-core depois da correcao do alinhamento do heap

// Reducao da memoria informada ao jogo: cada valor em MB corresponde a uma chave de diagnostico (apenas uma fica ligada por vez).
private val MEM_REDUCE_FLAGS = listOf(
    64 to NativeSettings.DiagFlag.REDUCE_MEM_64,
    128 to NativeSettings.DiagFlag.REDUCE_GAME_MEM,
    256 to NativeSettings.DiagFlag.REDUCE_MEM_256,
    384 to NativeSettings.DiagFlag.REDUCE_MEM_384,
    512 to NativeSettings.DiagFlag.REDUCE_MEM_512,
)

private fun memReduceCurrentChoice(): Int =
    MEM_REDUCE_FLAGS.firstOrNull { NativeSettings.getDiagFlag(it.second) }?.first ?: 0

private fun setMemReduceChoice(megabytes: Int) {
    for ((mb, flag) in MEM_REDUCE_FLAGS) {
        NativeSettings.setDiagFlag(flag, mb == megabytes)
    }
}

// Teste do JIT (bissecao): cada opcao manda uma faixa de codigo do jogo para o INTERPRETADOR (o resto continua no JIT).
// Opcao 0 = desligado. Ver Diag::JitBisectRange (DiagnosticsFlags.h).
private val JIT_BISECT_OPTIONS = listOf(
    1 to NativeSettings.DiagFlag.JIT_BISECT_1,
    2 to NativeSettings.DiagFlag.JIT_BISECT_2,
    3 to NativeSettings.DiagFlag.JIT_BISECT_3,
    4 to NativeSettings.DiagFlag.JIT_BISECT_4,
    5 to NativeSettings.DiagFlag.JIT_BISECT_5,
    6 to NativeSettings.DiagFlag.JIT_BISECT_6,
    7 to NativeSettings.DiagFlag.JIT_BISECT_7,
    8 to NativeSettings.DiagFlag.JIT_BISECT_8,
)

// Quantum das threads: opcao 0 = valor do perfil do jogo; 1..4 = 20000 / 10000 / 5000 / 2000 instrucoes por fatia.
private val QUANTUM_OPTIONS = listOf(
    1 to NativeSettings.DiagFlag.QUANTUM_20K,
    2 to NativeSettings.DiagFlag.QUANTUM_10K,
    3 to NativeSettings.DiagFlag.QUANTUM_5K,
    4 to NativeSettings.DiagFlag.QUANTUM_2K,
)

private fun quantumLabel(option: Int): String = when (option) {
    0 -> tr("Padrao (perfil do jogo)")
    1 -> tr("20000 instrucoes por fatia")
    2 -> tr("10000 instrucoes por fatia")
    3 -> tr("5000 instrucoes por fatia")
    else -> tr("2000 instrucoes por fatia")
}

private fun quantumCurrentChoice(): Int =
    QUANTUM_OPTIONS.firstOrNull { NativeSettings.getDiagFlag(it.second) }?.first ?: 0

private fun setQuantumChoice(option: Int) {
    for ((n, flag) in QUANTUM_OPTIONS) {
        NativeSettings.setDiagFlag(flag, n == option)
    }
}

private fun jitBisectLabel(option: Int): String = when (option) {
    0 -> tr("Desligado (tudo no JIT)")
    1 -> tr("1 - Todo o codigo do jogo no interpretador")
    2 -> tr("2 - XML, strings e alocador (02030000-020A0000)")
    3 -> tr("3 - Primeira metade da 2 (02030000-02070000)")
    4 -> tr("4 - Segunda metade da 2 (02070000-020A0000)")
    5 -> tr("5 - Primeiro quarto (02030000-02050000)")
    6 -> tr("6 - Segundo quarto (02050000-02070000)")
    7 -> tr("7 - Terceiro quarto (02070000-02080000)")
    else -> tr("8 - Quarto quarto (02080000-020A0000)")
}

private fun jitBisectCurrentChoice(): Int =
    JIT_BISECT_OPTIONS.firstOrNull { NativeSettings.getDiagFlag(it.second) }?.first ?: 0

private fun setJitBisectChoice(option: Int) {
    for ((n, flag) in JIT_BISECT_OPTIONS) {
        NativeSettings.setDiagFlag(flag, n == option)
    }
}

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

        InfoDialogButton(
            title = tr("Quantum das threads"),
            explanation = tr(
                "O que e: no modo Single-core os 3 nucleos do Wii U rodam um de cada vez, cada um por uma fatia de instrucoes. " +
                        "No Wii U real eles rodam em PARALELO. Com fatias grandes, uma thread pode correr muito a frente de outra e quebrar " +
                        "a ordem que o jogo espera (ex.: a thread de arquivos usa um objeto que a Main Thread ainda nao criou).\n\n" +
                        "O que faz: substitui o valor do perfil do jogo. Menor = os nucleos se intercalam mais, mais parecido com o Wii U real " +
                        "(um pouco mais lento).\n\n" +
                        "Como usar: se o jogo crashar perto do inicio com ponteiro nulo, tente 5000 ou 2000. Vale a partir do proximo jogo. " +
                        "No log aparece \"[Diag] quantum das threads\"."
            ),
        )
        SingleSelection(
            label = tr("Quantum das threads"),
            initialChoice = { quantumCurrentChoice() },
            choices = listOf(0, 1, 2, 3, 4),
            choiceToString = { quantumLabel(it) },
            onChoiceChanged = { setQuantumChoice(it) },
        )

        InfoDialogButton(
            title = tr("Teste do JIT (bissecao do crash)"),
            explanation = tr(
                "O que e: o Watch Dogs crasha so no recompilador (JIT) e passa no interpretador. Este teste descobre QUAL parte do " +
                        "codigo o JIT traduz errado.\n\n" +
                        "O que faz: a faixa de codigo escolhida roda no INTERPRETADOR (lento, mas correto) e o resto continua no JIT. " +
                        "As faixas 3 e 4 sao as metades da 2; 5 a 8 sao os quartos. A opcao 1 manda TODO o codigo para o interpretador " +
                        "(confirma que o JIT e a causa).\n\n" +
                        "Como usar: use o modo de CPU \"Single-core recompiler\" no perfil do jogo, escolha uma faixa, feche o jogo e abra de novo " +
                        "(vale a partir do proximo jogo). Se o crash SUMIR, o bug esta nessa faixa: me diga qual foi. Se continuar igual, o bug " +
                        "esta fora dela. O jogo fica mais lento no inicio. Desligue depois. No log aparece \"[Diag] JIT bissecao\"."
            ),
        )
        SingleSelection(
            label = tr("Teste do JIT: faixa no interpretador"),
            initialChoice = { jitBisectCurrentChoice() },
            choices = listOf(0, 1, 2, 3, 4, 5, 6, 7, 8),
            choiceToString = { jitBisectLabel(it) },
            onChoiceChanged = { setJitBisectChoice(it) },
        )

        InfoDialogButton(
            title = tr("Reduzir memoria informada ao jogo"),
            explanation = tr(
                "O que e: o Watch Dogs pede ao sistema TODA a memoria disponivel (cerca de 980 MB) de uma vez e a divide com o " +
                        "alocador proprio. Em testes, informar menos memoria evitou o crash do inicio do jogo. O emulador reserva " +
                        "menos memoria para o sistema do que o Wii U real, entao sobra mais para o jogo.\n\n" +
                        "O que faz: informa ao jogo a quantidade escolhida de MEM2 a menos. Quanto maior a reducao, menos memoria " +
                        "o jogo tem; se ele faltar memoria, escolha um valor menor.\n\n" +
                        "Como usar: escolha um valor, feche o jogo e abra de novo (vale a partir do proximo jogo). " +
                        "No log aparece \"[Memoria] OSGetMemBound(tipo=2)\"."
            ),
        )
        SingleSelection(
            label = tr("Reduzir memoria informada ao jogo (teste)"),
            initialChoice = { memReduceCurrentChoice() },
            choices = listOf(0, 64, 128, 256, 384, 512),
            choiceToString = { if (it == 0) tr("Desligado") else "$it MB" },
            onChoiceChanged = { setMemReduceChoice(it) },
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
            label = tr("Aumentar volume do audio (x1,5)"),
            description = tr(
                "Aumenta o volume do jogo em 50% com um limitador suave para nao estourar. Use se o som estiver baixo mesmo com " +
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
            label = tr("Locked Cache indisponivel (teste)"),
            description = tr(
                "O Watch Dogs crasha em MergeVisibleNodesGPUVisibilityLCDMA, uma funcao que usa o Locked Cache. Com esta chave ligada, " +
                        "LCHardwareIsAvailable() devolve falso; se o jogo checar isso, usa o caminho SEM Locked Cache. E um TESTE: " +
                        "pode nao ter efeito ou o jogo pode nao abrir. Desligado = comportamento normal. Vale no proximo jogo que voce abrir."
            ),
            initialCheckedState = { NativeSettings.getDiagFlag(NativeSettings.DiagFlag.LOCKED_CACHE_OFF) },
            onCheckedChanged = { NativeSettings.setDiagFlag(NativeSettings.DiagFlag.LOCKED_CACHE_OFF, it) },
        )
        Toggle(
            label = tr("Desligar o piso do OSSleepTicks (100 us)"),
            description = tr(
                "Por padrao, esperas do jogo menores que 100 microssegundos (OSSleepTicks) duram 100 us. Isso corta um laco de " +
                        "espera da thread de video que fazia ~1 milhao de chamadas por segundo, gastando CPU e esquentando o " +
                        "aparelho. Ligue esta chave so para testar sem o piso. Vale no proximo jogo que voce abrir."
            ),
            initialCheckedState = { NativeSettings.getDiagFlag(NativeSettings.DiagFlag.SLEEP_FLOOR_OFF) },
            onCheckedChanged = { NativeSettings.setDiagFlag(NativeSettings.DiagFlag.SLEEP_FLOOR_OFF, it) },
        )
        Toggle(
            label = tr("Desligar o atraso de partida da thread de arquivos"),
            description = tr(
                "Por padrao a thread CDelayedArchivesOpenThread (Watch Dogs) espera 100 ms antes de comecar. Isso corrige o crash de " +
                        "ponteiro nulo no inicio do jogo (nos testes: com atraso o jogo entrou 7 de 7 vezes; sem ele crashou sempre). " +
                        "Ligue esta chave so para testar sem o atraso. Vale no proximo jogo que voce abrir."
            ),
            initialCheckedState = { NativeSettings.getDiagFlag(NativeSettings.DiagFlag.THREAD_START_DELAY_OFF) },
            onCheckedChanged = { NativeSettings.setDiagFlag(NativeSettings.DiagFlag.THREAD_START_DELAY_OFF, it) },
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
