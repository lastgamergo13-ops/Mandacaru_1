package info.cemu.cemu.settings.experimental.qualityoflife

import androidx.datastore.core.DataStore
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import info.cemu.cemu.common.settings.AppSettings
import info.cemu.cemu.common.settings.AppSettingsStore
import info.cemu.cemu.common.settings.GuiSettings
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

/**
 * Backs the handful of Quality of Life toggles that are pure Android/UI concerns (no relation
 * to the C++ emulation core), so they're kept in the same Kotlin-only DataStore
 * (AppSettingsStore) that "Language" and "GamePad position" already use, instead of going
 * through CemuConfig/JNI like most other Experimental/Hackers options.
 */
class QualityOfLifeSettingsViewModel(
    private val dataStore: DataStore<AppSettings> = AppSettingsStore.dataStore,
) : ViewModel() {
    val guiSettings = dataStore.data.map { it.guiSettings }
        .stateIn(
            viewModelScope,
            SharingStarted.WhileSubscribed(5000),
            GuiSettings(),
        )

    fun setLockMenuOrientation(locked: Boolean) {
        viewModelScope.launch {
            dataStore.updateData { it.copy(guiSettings = it.guiSettings.copy(lockMenuOrientation = locked)) }
        }
    }
}
