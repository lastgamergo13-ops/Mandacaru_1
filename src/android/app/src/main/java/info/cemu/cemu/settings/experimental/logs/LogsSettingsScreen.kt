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
