package org.moneromerchant.pos

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.SystemBarStyle
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import com.journeyapps.barcodescanner.ScanContract
import com.journeyapps.barcodescanner.ScanOptions
import java.text.DateFormat
import java.util.Date

@Composable
fun PairingScreen(model: ConnectionViewModel, state: ConnectionUiState,
    modifier: Modifier = Modifier.safeDrawingPadding()) {
    if (state.merchant != null) {
        TerminalSettingsScreen(state, model::reconnect, model::disconnect, model::forget, modifier)
        return
    }
    // Invitations contain a secret: deliberately keep them out of saved instance state.
    var invitation by remember { mutableStateOf("") }
    var name by rememberSaveable { mutableStateOf("Counter 1") }
    var confirmForget by remember { mutableStateOf(false) }
    val scanner = rememberLauncherForActivityResult(ScanContract()) { result ->
        result.contents?.let { invitation = it }
    }
    PosTheme {
        Surface(Modifier.fillMaxSize().then(modifier), color = MaterialTheme.colorScheme.background) {
            Column(Modifier.verticalScroll(rememberScrollState()).padding(24.dp), verticalArrangement = Arrangement.spacedBy(20.dp)) {
                Spacer(Modifier.height(12.dp))
                Card(shape = RoundedCornerShape(24.dp), colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface)) {
                    Column(Modifier.padding(24.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                        Text(state.phase, style = MaterialTheme.typography.titleLarge, color = if (state.online) Color(0xFF45F879) else MaterialTheme.colorScheme.primary)
                        Text(state.detail)
                        if (state.busy) LinearProgressIndicator(Modifier.fillMaxWidth(), color = MaterialTheme.colorScheme.primary)
                        state.merchant?.let { merchant ->
                            Text(merchant.name, style = MaterialTheme.typography.titleMedium)
                            Identity("Merchant identity", merchant.peerId)
                            Identity("Delivery topic", merchant.topic)
                        }
                        if (state.localPeer.isNotEmpty()) Identity("This terminal · compare before approval", state.localPeer)
                        state.lastChecked?.let { Text("Last reply ${DateFormat.getTimeInstance().format(Date(it))}", style = MaterialTheme.typography.labelSmall) }
                    }
                }
                OutlinedTextField(name, { name = it.take(80) }, label = { Text("Terminal name") }, singleLine = true, modifier = Modifier.fillMaxWidth(), enabled = !state.busy)
                OutlinedButton(onClick = {
                    scanner.launch(ScanOptions().setDesiredBarcodeFormats(ScanOptions.QR_CODE).setPrompt("Scan merchant pairing invitation").setBeepEnabled(false).setOrientationLocked(false))
                }, modifier = Modifier.fillMaxWidth(), enabled = !state.busy) { Text("Scan pairing QR") }
                OutlinedTextField(invitation, { invitation = it.take(8192) }, label = { Text("Or paste pairing invitation") }, minLines = 3, maxLines = 5, modifier = Modifier.fillMaxWidth(), enabled = !state.busy)
                Button(onClick = { model.pair(invitation, name); invitation = "" }, enabled = !state.busy && invitation.isNotBlank(), modifier = Modifier.fillMaxWidth().height(56.dp)) { Text("Pair with merchant") }
                // Recovery remains available if the Keystore key was invalidated.
                if (state.phase == "Storage unavailable") TextButton(onClick = { confirmForget = true }) { Text("Forget terminal identity") }
                HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
                Text("MERCHANT CONNECTION", color = MaterialTheme.colorScheme.primary, style = MaterialTheme.typography.labelMedium)
                Text("Logos Delivery · authenticated HPKE\nEncrypted merchant messages", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.4f))
            }
            if (confirmForget) AlertDialog(onDismissRequest = { confirmForget = false }, title = { Text("Forget this terminal?") },
                text = { Text("Deletes the saved merchant and terminal key on this phone. Revoke the old identity on the merchant computer too.") },
                confirmButton = { TextButton(onClick = { confirmForget = false; model.forget() }) { Text("Forget") } },
                dismissButton = { TextButton(onClick = { confirmForget = false }) { Text("Keep pairing") } })
        }
    }
}
@Composable
private fun Identity(label: String, value: String) {
    Text(label, color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.4f), style = MaterialTheme.typography.labelSmall)
    SelectionContainer { Text(value, fontFamily = FontFamily.Monospace, style = MaterialTheme.typography.bodySmall) }
}
