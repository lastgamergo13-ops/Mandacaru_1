package info.cemu.cemu.common.cemufiles

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.provider.DocumentsContract
import androidx.documentfile.provider.DocumentFile
import info.cemu.cemu.nativeinterface.NativeActiveSettings
import info.cemu.cemu.provider.DocumentsProvider
import java.io.File

private const val LOG_FILE_NAME = "log.txt"

fun logFileExists(): Boolean {
    return File(NativeActiveSettings.getUserDataPath()).resolve(LOG_FILE_NAME).isFile
}

fun tryShareLogFile(context: Context): Boolean {
    try {
        val fileUri = DocumentsContract.buildDocumentUri(
            DocumentsProvider.AUTHORITY,
            DocumentsProvider.ROOT_ID + "/$LOG_FILE_NAME"
        )

        val documentFile = DocumentFile.fromSingleUri(context, fileUri)!!

        val intent = Intent(Intent.ACTION_SEND)
            .setDataAndType(documentFile.uri, "text/plain")
            .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            .putExtra(Intent.EXTRA_STREAM, documentFile.uri)

        context.startActivity(Intent.createChooser(intent, null))

        return true
    } catch (_: Exception) {
        return false
    }
}

fun tryOpenCemuFolder(context: Context): Boolean {
    try {
        val intent = Intent(Intent.ACTION_VIEW)
            .addCategory(Intent.CATEGORY_DEFAULT)
            .addFlags(
                Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION
                        or Intent.FLAG_GRANT_READ_URI_PERMISSION
                        or Intent.FLAG_GRANT_PREFIX_URI_PERMISSION
                        or Intent.FLAG_GRANT_WRITE_URI_PERMISSION
            )
        intent.data = DocumentsContract.buildRootUri(
            DocumentsProvider.AUTHORITY,
            DocumentsProvider.ROOT_ID
        )
        context.startActivity(intent)

        return true
    } catch (_: Exception) {
        return false
    }
}

/** Abre uma URL (ex.: página de releases do GitHub) no navegador do sistema. */
fun tryOpenUrl(context: Context, url: String): Boolean {
    try {
        context.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(url)))
        return true
    } catch (_: Exception) {
        return false
    }
}
