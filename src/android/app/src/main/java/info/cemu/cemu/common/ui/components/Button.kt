package info.cemu.cemu.common.ui.components

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import info.cemu.cemu.common.ui.theme.SteamDeckColors

@Composable
fun Button(label: String, description: String? = null, onClick: () -> Unit = {}) {
    Card(
        colors = CardDefaults.cardColors(
            containerColor = SteamDeckColors.CardBackground,
        ),
        shape = RoundedCornerShape(10.dp),
        onClick = onClick,
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 4.dp),
    ) {
        Column(
            modifier = Modifier
                .padding(12.dp)
                .fillMaxWidth(),
        ) {
            Text(
                modifier = Modifier,
                text = label,
                color = SteamDeckColors.TextPrimary,
                fontSize = 16.sp,
                fontWeight = FontWeight.Bold,
            )
            if (description != null) {
                Text(
                    modifier = Modifier.padding(top = 6.dp),
                    color = SteamDeckColors.TextSecondary,
                    text = description,
                    fontSize = 13.sp,
                )
            }
        }
    }
}