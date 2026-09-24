@file:Suppress("unused")

package info.cemu.cemu.nativeinterface

import android.content.ContentValues
import android.content.Context
import android.graphics.Bitmap
import android.os.Build
import android.os.Environment
import android.os.Handler
import android.os.Looper
import android.provider.MediaStore
import android.util.Log
import android.widget.Toast
import androidx.annotation.Keep
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Quality of Life (Experimental/Hackers): Quick Screenshot bridge.
 *
 * Does not implement screenshot capture itself - it hands already-captured, already-clean pixels
 * (see the comment in NativeRenderer.cpp) to Android's MediaStore API, so the image shows up in
 * the user's Photos/Gallery app under Pictures/Cemu without needing any storage permission on
 * Android 10+ (API 29+), which this app's minSdk does not guarantee for every device - see
 * trySaveScreenshot() below for what happens on older versions.
 */
object NativeRenderer {
    private lateinit var applicationContext: Context
    private var mainThreadHandler: Handler? = null

    fun initialize(context: Context) {
        applicationContext = context.applicationContext
        mainThreadHandler = Handler(Looper.getMainLooper())
    }

    external fun triggerScreenshot()

    // Quality of Life master switch: whether the "Take Screenshot" in-game menu button (see
    // EmulationScreen.kt) shows itself at all. Off by default, like the rest of Experimental/Hackers.
    @JvmStatic
    external fun isQuickScreenshotEnabled(): Boolean

    @JvmStatic
    external fun setQuickScreenshotEnabled(enabled: Boolean)

    /**
     * Called from native code (NativeRenderer.cpp, AndroidSaveScreenshot) once a screenshot has
     * been captured. [pixels] is a packed ARGB32 int array (alpha always 0xFF), row-major,
     * [width] x [height] - the standard format Bitmap.setPixels() expects, chosen specifically
     * because it has a documented, byte-order-safe layout (unlike a raw pixel buffer copied over
     * JNI).
     *
     * Runs on a native background thread, not the main thread - do not touch UI directly here.
     */
    @Keep
    @JvmStatic
    fun onScreenshotCaptured(pixels: IntArray, width: Int, height: Int, isMainWindow: Boolean): Boolean {
        val success = trySaveScreenshot(pixels, width, height)
        mainThreadHandler?.post {
            if (::applicationContext.isInitialized) {
                val message = if (success) "Screenshot saved" else "Failed to save screenshot"
                Toast.makeText(applicationContext, message, Toast.LENGTH_SHORT).show()
            }
        }
        return success
    }

    private fun trySaveScreenshot(pixels: IntArray, width: Int, height: Int): Boolean {
        if (!::applicationContext.isInitialized) return false
        if (width <= 0 || height <= 0 || pixels.size < width * height) return false

        val contentResolver = applicationContext.contentResolver
        var bitmap: Bitmap? = null
        return try {
            bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
            bitmap.setPixels(pixels, 0, width, 0, 0, width, height)

            val timestamp = SimpleDateFormat("yyyyMMdd_HHmmss_SSS", Locale.US).format(Date())
            val displayName = "Cemu_$timestamp.png"

            val values = ContentValues().apply {
                put(MediaStore.Images.Media.DISPLAY_NAME, displayName)
                put(MediaStore.Images.Media.MIME_TYPE, "image/png")
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                    put(MediaStore.Images.Media.RELATIVE_PATH, "${Environment.DIRECTORY_PICTURES}/Cemu")
                    put(MediaStore.Images.Media.IS_PENDING, 1)
                }
            }

            val itemUri = contentResolver.insert(
                MediaStore.Images.Media.EXTERNAL_CONTENT_URI,
                values
            ) ?: return false

            val written = contentResolver.openOutputStream(itemUri)?.use { out ->
                bitmap.compress(Bitmap.CompressFormat.PNG, 100, out)
            } ?: false

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                values.clear()
                values.put(MediaStore.Images.Media.IS_PENDING, 0)
                contentResolver.update(itemUri, values, null, null)
            }

            written
        } catch (e: Exception) {
            // Most likely cause on API 26-28 (pre-scoped-storage): the MediaStore insert/write
            // was rejected because this app does not request WRITE_EXTERNAL_STORAGE at runtime -
            // there is deliberately no runtime permission request flow for that here (see the
            // "Quick Screenshot" feature notes: it's a known, honest limitation on those older
            // versions, not a silent failure - the toast above still tells the user it failed).
            Log.d("NativeRenderer", "Failed to save screenshot: ${e.message}")
            false
        } finally {
            bitmap?.recycle()
        }
    }
}
