package info.cemu.cemu

import android.app.Application
import info.cemu.cemu.common.android.context.internalFolder
import info.cemu.cemu.common.settings.AppSettingsStore
import info.cemu.cemu.common.ui.localization.setLanguage
import info.cemu.cemu.common.ui.localization.setTranslations
import info.cemu.cemu.nativeinterface.NativeActiveSettings.initializeActiveSettings
import info.cemu.cemu.nativeinterface.NativeActiveSettings.setInternalDir
import info.cemu.cemu.nativeinterface.NativeActiveSettings.setNativeLibDir
import info.cemu.cemu.nativeinterface.NativeEmulation.initializeEmulation
import info.cemu.cemu.nativeinterface.NativeEmulation.setDPI
import info.cemu.cemu.nativeinterface.NativeFiles
import info.cemu.cemu.nativeinterface.NativeGraphicPacks.refreshGraphicPacks
import info.cemu.cemu.nativeinterface.NativeLogging.crashLog
import info.cemu.cemu.nativeinterface.NativeLogging
import info.cemu.cemu.nativeinterface.NativeRenderer
import info.cemu.cemu.nativeinterface.NativeSwkbd.initializeSwkbd
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.runBlocking
import java.io.File
import java.io.IOException
import java.io.PrintWriter
import java.io.StringWriter
import java.util.regex.Pattern

class CemuApplication : Application() {
    override fun onCreate() {
        super.onCreate()

        configureExceptionHandler()

        AppSettingsStore.init(this)

        NativeFiles.initialize(contentResolver)

        // Quality of Life: gives the screenshot save-callback (NativeRenderer.cpp ->
        // NativeRenderer.kt) access to a Context, the same pattern NativeFiles above already uses.
        NativeRenderer.initialize(this)

        initializeTranslations()

        // Extrai os graphic packs pré-embutidos no APK ANTES de initializeCemu(),
        // para que refreshGraphicPacks() (chamado logo abaixo) já os encontre
        // nesta mesma execução, sem precisar reiniciar o app.
        saveBundledGraphicPacks()

        initializeCemu()

        saveDataFiles()
    }

    private fun initializeTranslations() {
        setTranslations(this)

        val language = runBlocking {
            AppSettingsStore.dataStore.data.map { it.guiSettings.language }.first()
        }

        setLanguage(language, this)
    }

    /**
     * Percorre recursivamente uma pasta dentro de assets/, retornando o caminho
     * relativo (a partir de [path]) de cada arquivo folha encontrado.
     */
    private fun traverseAssets(path: String = ""): Iterator<String> = iterator {
        val assetFiles = assets.list(path) ?: return@iterator

        if (assetFiles.isEmpty()) {
            yield(path)
        }

        for (assetFile in assetFiles) {
            val assetPath = path + (if (path == "") "" else "/") + assetFile
            for (file in traverseAssets(assetPath)) {
                yield(file)
            }
        }
    }

    private fun saveDataFiles() {
        val dataFolder = File(internalCemuDataFolder)

        if (!dataFolder.exists() && !dataFolder.mkdirs()) {
            return
        }

        val hashFileName = "hash.txt"
        val hashFile = dataFolder.resolve(hashFileName)
        val oldHash = if (hashFile.isFile) hashFile.readText() else "invalid"

        val newHash = try {
            assets.open(hashFileName).use { it.reader().readText() }
        } catch (_: IOException) {
            return
        }

        if (oldHash == newHash) {
            return
        }

        dataFolder.deleteRecursively()
        dataFolder.mkdirs()
        dataFolder.resolve(hashFileName).writeText(newHash)

        val filePatterns = arrayOf(
            Pattern.compile("gameProfiles/.*"),
            Pattern.compile("resources/.*"),
        )

        fun isFileValid(file: String): Boolean {
            return filePatterns.any { pattern -> pattern.matcher(file).matches() }
        }

        for (assetFile in traverseAssets()) {
            if (!isFileValid(assetFile)) {
                continue
            }

            val outFile = dataFolder.resolve(assetFile)
            outFile.parentFile?.mkdirs()
            assets.open(assetFile)
                .use { asset -> outFile.outputStream().use { out -> asset.copyTo(out) } }
        }
    }

    /**
     * Extrai app/src/main/assets/graphicPacks/ (pacotes gráficos pré-embutidos no APK)
     * para a MESMA pasta onde GraphicPack2::LoadAll() varre no lado nativo:
     * ActiveSettings::GetUserDataPath("graphicPacks") == internalCemuUserFolder/graphicPacks.
     *
     * Comparamos o version.txt embutido no APK com um marcador já extraído. Isso garante que:
     *  - Numa instalação nova, os packs já aparecem sem precisar baixar nada.
     *  - Numa atualização do APK que traga uma versão mais nova de pacotes embutidos,
     *    eles são extraídos de novo automaticamente.
     *  - NUNCA apagamos a pasta graphicPacks/ inteira (ao contrário de saveDataFiles()),
     *    então os pacotes baixados pelo usuário via GraphicPacksDownloader
     *    (em graphicPacks/downloadedGraphicPacks/) e quaisquer pacotes adicionados
     *    manualmente pelo usuário nunca são apagados por esta função.
     */
    private fun saveBundledGraphicPacks() {
        val assetsRoot = "graphicPacks"
        val bundledVersion = try {
            assets.open("$assetsRoot/version.txt").use { it.reader().readText().trim() }
        } catch (_: IOException) {
            // Nenhum graphic pack foi embutido neste build do APK.
            return
        }

        val graphicPacksFolder = File(internalCemuUserFolder).resolve("graphicPacks")
        graphicPacksFolder.mkdirs()

        val markerFile = graphicPacksFolder.resolve(".bundled_marker")
        val previousBundledVersion = if (markerFile.isFile) markerFile.readText().trim() else ""

        if (previousBundledVersion == bundledVersion) {
            // Já extraído nesta versão do APK — não repete o trabalho a cada abertura.
            return
        }

        for (assetFile in traverseAssets(assetsRoot)) {
            // assetFile vem como "graphicPacks/<...>"; removemos o prefixo para
            // gravar relativo à pasta graphicPacks/ real.
            val relativePath = assetFile.removePrefix("$assetsRoot/")
            if (relativePath.isEmpty()) continue

            val outFile = graphicPacksFolder.resolve(relativePath)
            outFile.parentFile?.mkdirs()
            try {
                assets.open(assetFile)
                    .use { asset -> outFile.outputStream().use { out -> asset.copyTo(out) } }
            } catch (_: IOException) {
                // Ignora arquivos individuais que falharem e continua com o resto do pacote.
            }
        }

        markerFile.writeText(bundledVersion)
    }

    private fun configureExceptionHandler() {
        if (DefaultUncaughtExceptionHandler == null) {
            DefaultUncaughtExceptionHandler = Thread.getDefaultUncaughtExceptionHandler()
        }
        Thread.setDefaultUncaughtExceptionHandler { thread: Thread, exception: Throwable ->
            val stringWriter = StringWriter()
            val printWriter = PrintWriter(stringWriter)
            exception.printStackTrace(printWriter)
            val stacktrace = stringWriter.toString()
            crashLog(stacktrace)
            DefaultUncaughtExceptionHandler!!.uncaughtException(
                thread,
                exception,
            )
        }
    }

    private fun initializeCemu() {
        val displayMetrics = resources.displayMetrics
        setDPI(displayMetrics.density)
        initializeActiveSettings(
            userDataPath = internalCemuUserFolder,
            configPath = internalCemuUserFolder,
            dataPath = internalCemuDataFolder,
            cachePath = internalCemuUserFolder,
        )
        setNativeLibDir(applicationInfo.nativeLibraryDir)
        setInternalDir(dataDir.absolutePath)
        initializeEmulation()
        initializeSwkbd()
        refreshGraphicPacks()
        logAppEnvironment()
    }

    // V11.28: informacoes do aparelho/permissoes no log desde a abertura do app
    private fun logAppEnvironment() {
        try {
            NativeLogging.log("[App] aparelho=${android.os.Build.MANUFACTURER} ${android.os.Build.MODEL} android=${android.os.Build.VERSION.RELEASE} sdk=${android.os.Build.VERSION.SDK_INT} abi=${android.os.Build.SUPPORTED_ABIS.joinToString()}")
            val persisted = contentResolver.persistedUriPermissions
            NativeLogging.log("[App] permissoes de pasta persistidas: ${persisted.size}")
            for (p in persisted) {
                NativeLogging.log("[App]   ${p.uri} leitura=${p.isReadPermission} escrita=${p.isWritePermission}")
            }
        } catch (e: Throwable) {
            NativeLogging.log("[App] falha ao registrar ambiente: ${e}")
        }
    }

    private val internalCemuDataFolder: String
        get() = internalFolder().resolve("data").toString()

    private val internalCemuUserFolder: String
        get() = internalFolder().toString()

    companion object {
        init {
            System.loadLibrary("CemuAndroid")
        }

        private var DefaultUncaughtExceptionHandler: Thread.UncaughtExceptionHandler? = null
    }
}
