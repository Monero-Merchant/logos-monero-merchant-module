package org.moneromerchant.pos

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import java.text.DateFormat
import java.util.Date

@Composable
fun TerminalSettingsScreen(
    state: ConnectionUiState,
    reconnect: () -> Unit,
    disconnect: () -> Unit,
    forget: () -> Unit,
    modifier: Modifier = Modifier,
) {
    var showTechnicalDetails by rememberSaveable { mutableStateOf(false) }
    var confirmForget by remember { mutableStateOf(false) }
    val secondaryText = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.65f)
    val statusColor = when {
        state.online -> Color(0xFF6DD9A1)
        state.manuallyDisconnected -> secondaryText
        else -> Color(0xFFFFCE85)
    }
    Box(Modifier.fillMaxSize().then(modifier), contentAlignment = Alignment.TopCenter) {
        Column(
            Modifier.widthIn(max = 600.dp).fillMaxSize().verticalScroll(rememberScrollState())
                .padding(horizontal = 24.dp, vertical = 16.dp),
            verticalArrangement = Arrangement.spacedBy(24.dp),
        ) {
            SettingsSection("Merchant connection") {
                Surface(color = statusColor.copy(alpha = 0.12f), shape = CircleShape) {
                    Row(Modifier.padding(horizontal = 12.dp, vertical = 8.dp),
                        verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        Box(Modifier.size(8.dp).background(statusColor, CircleShape))
                        Text(state.phase, color = statusColor, style = MaterialTheme.typography.labelMedium)
                    }
                }
                Text(state.detail, style = MaterialTheme.typography.bodyMedium, color = secondaryText)
                if (state.busy) LinearProgressIndicator(Modifier.fillMaxWidth())
                SettingsValue("Last response", state.lastChecked?.let {
                    DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT).format(Date(it))
                } ?: "No response yet")
                Button(onClick = reconnect, enabled = !state.busy,
                    modifier = Modifier.fillMaxWidth().heightIn(min = 52.dp)) {
                    Text(if (state.online) "Check connection" else "Reconnect")
                }
                if (!state.manuallyDisconnected) {
                    FilledTonalButton(onClick = disconnect, enabled = !state.busy,
                        modifier = Modifier.fillMaxWidth().heightIn(min = 52.dp)) { Text("Disconnect") }
                }
                FilledTonalButton(onClick = { confirmForget = true }, enabled = !state.busy,
                    modifier = Modifier.fillMaxWidth().heightIn(min = 52.dp)) { Text("Remove pairing") }
                if (!state.manuallyDisconnected) {
                    Text("Disconnecting keeps your pairing and current order saved.",
                        style = MaterialTheme.typography.bodySmall, color = secondaryText)
                }
            }
            SettingsSection("Terminal") {
                SettingsValue("Merchant approval", when {
                    state.approved -> "Approved"
                    state.phase == "Access revoked" -> "Revoked"
                    state.phase == "Not enrolled" -> "Not enrolled"
                    else -> "Awaiting approval"
                })
                if (!state.approved && state.localPeer.isNotEmpty()) {
                    Text("Compare this terminal’s identity with the one shown in Basecamp before approval.",
                        style = MaterialTheme.typography.bodySmall, color = secondaryText)
                    SettingsIdentity("Terminal identity", state.localPeer)
                }
                Text("Messages between this terminal and your merchant are encrypted.",
                    style = MaterialTheme.typography.bodySmall, color = secondaryText)
                TextButton(onClick = { showTechnicalDetails = !showTechnicalDetails }) {
                    Text(if (showTechnicalDetails) "Hide technical details" else "Show technical details")
                }
                if (showTechnicalDetails) {
                    HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
                    if (state.approved && state.localPeer.isNotEmpty()) SettingsIdentity("Terminal identity", state.localPeer)
                    state.merchant?.let { merchant ->
                        SettingsIdentity("Merchant identity", merchant.peerId)
                        SettingsIdentity("Delivery topic", merchant.topic)
                        SettingsValue("Delivery network", merchant.network)
                    }
                }
            }

        }
    }
    if (confirmForget) AlertDialog(
        onDismissRequest = { confirmForget = false },
        title = { Text("Remove this pairing?") },
        text = { Text("This removes the saved merchant and terminal identity from this device. Revoke the old terminal in Basecamp too. You’ll need a new invitation to pair again.") },
        confirmButton = {
            TextButton(onClick = { confirmForget = false; forget() },
                colors = ButtonDefaults.textButtonColors(contentColor = Color(0xFFFFA3AA))) { Text("Remove pairing") }
        },
        dismissButton = { TextButton(onClick = { confirmForget = false }) { Text("Keep pairing") } },
    )
}

@Composable
private fun SettingsSection(title: String, content: @Composable ColumnScope.() -> Unit) {
    Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Text(title, style = MaterialTheme.typography.titleSmall,
            color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.65f))
        Card(Modifier.fillMaxWidth(), colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface)) {
            Column(Modifier.fillMaxWidth().padding(20.dp), verticalArrangement = Arrangement.spacedBy(16.dp), content = content)
        }
    }
}

@Composable
private fun SettingsValue(label: String, value: String) {
    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
        Text(label, style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.65f))
        Text(value, style = MaterialTheme.typography.bodyMedium)
    }
}

@Composable
private fun SettingsIdentity(label: String, value: String) {
    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
        Text(label, style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.65f))
        SelectionContainer { Text(value, style = MaterialTheme.typography.bodySmall, fontFamily = FontFamily.Monospace) }
    }
}

@Preview(showBackground = true, widthDp = 393, heightDp = 852)
@Composable
private fun ConnectedSettingsPreview() {
    PosTheme {
        Surface(color = MaterialTheme.colorScheme.background) {
            TerminalSettingsScreen(ConnectionUiState(
                merchant = MerchantEndpoint("merchant-id", "delivery-topic", "Monero Merchant"),
                phase = "Connected", detail = "Basecamp · stagenet · wallet synchronized",
                online = true, approved = true, busy = false, localPeer = "terminal-id",
            ), {}, {}, {})
        }
    }
}

@Preview(showBackground = true, widthDp = 320, heightDp = 640)
@Composable
private fun DisconnectedSettingsPreview() {
    PosTheme {
        Surface(color = MaterialTheme.colorScheme.background) {
            TerminalSettingsScreen(ConnectionUiState(
                merchant = MerchantEndpoint("merchant-id", "delivery-topic", "Monero Merchant"),
                phase = "Disconnected", detail = "Pairing and current order saved. Tap Reconnect to check Monero Merchant in Basecamp.",
                manuallyDisconnected = true, approved = true, busy = false,
            ), {}, {}, {})
        }
    }
}
