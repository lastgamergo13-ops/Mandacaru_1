@file:OptIn(ExperimentalMaterial3Api::class)

package info.cemu.cemu.common.ui.components

import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TextField
import androidx.compose.material3.TimePicker
import androidx.compose.material3.TimePickerDialog
import androidx.compose.material3.rememberTimePickerState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.unit.dp
import info.cemu.cemu.R
import info.cemu.cemu.common.ui.localization.tr

/**
 * Time-of-day picker field, mirroring DateField's structure/style. The app already had a date
 * picker (DateField) but no time equivalent, so this is a new, separate component rather than
 * overloading DateField for two different jobs.
 *
 * @param hour 0-23
 * @param minute 0-59
 */
@Composable
fun TimeField(
    label: String,
    hour: Int,
    minute: Int,
    onTimeChange: (hour: Int, minute: Int) -> Unit,
) {
    var showTimePicker by remember { mutableStateOf(false) }

    TextField(
        value = formatTime(hour, minute),
        onValueChange = {},
        readOnly = true,
        label = { Text(label) },
        trailingIcon = {
            IconButton(onClick = { showTimePicker = !showTimePicker }) {
                Icon(painterResource(R.drawable.ic_date_range), tr("Pick a time"))
            }
        },
        modifier = Modifier
            .fillMaxWidth()
            .padding(8.dp),
    )

    if (showTimePicker) {
        TimePickerModal(
            initialHour = hour,
            initialMinute = minute,
            onTimeChanged = onTimeChange,
            onDismiss = { showTimePicker = false },
        )
    }
}

@Composable
private fun TimePickerModal(
    initialHour: Int,
    initialMinute: Int,
    onTimeChanged: (hour: Int, minute: Int) -> Unit,
    onDismiss: () -> Unit,
) {
    val timePickerState = rememberTimePickerState(
        initialHour = initialHour,
        initialMinute = initialMinute,
        is24Hour = true,
    )

    TimePickerDialog(
        title = { Text(tr("Select time")) },
        onDismissRequest = onDismiss,
        confirmButton = {
            TextButton(onClick = {
                onTimeChanged(timePickerState.hour, timePickerState.minute)
                onDismiss()
            }) {
                Text(tr("OK"))
            }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text(tr("Cancel")) } },
    ) {
        TimePicker(state = timePickerState)
    }
}

private fun formatTime(hour: Int, minute: Int): String =
    "%02d:%02d".format(hour, minute)
