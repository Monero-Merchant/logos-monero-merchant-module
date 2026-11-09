package org.moneromerchant.pos

import android.graphics.Bitmap
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.SystemBarStyle
import androidx.activity.compose.BackHandler
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.Image
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.repeatOnLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import com.google.zxing.BarcodeFormat
import com.google.zxing.EncodeHintType
import com.google.zxing.qrcode.QRCodeWriter
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext
import java.text.DateFormat
import java.util.Date

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge(statusBarStyle = SystemBarStyle.dark(android.graphics.Color.TRANSPARENT), navigationBarStyle = SystemBarStyle.dark(android.graphics.Color.TRANSPARENT))
        setContent { MerchantApp() }
    }
}
private val Accent = Color(0xFF8DE2EB)
private val Green = Color(0xFF45F879)
private val Muted = Color(0xFFB8B9C4)
@Composable
fun MerchantApp(model: ConnectionViewModel = viewModel()) {
    val connection by model.state.collectAsStateWithLifecycle()
    val lifecycle = LocalLifecycleOwner.current.lifecycle
    DisposableEffect(lifecycle) {
        val observer = LifecycleEventObserver { _, event ->
            if (event == Lifecycle.Event.ON_START) model.setForeground(true)
            if (event == Lifecycle.Event.ON_STOP) model.setForeground(false)
        }
        lifecycle.addObserver(observer)
        if (lifecycle.currentState.isAtLeast(Lifecycle.State.STARTED)) model.setForeground(true)
        onDispose { lifecycle.removeObserver(observer) }
    }
    MaterialTheme(colorScheme = darkColorScheme(primary = Accent, onPrimary = Color(0xFF20212B), background = Color(0xFF282A36), surface = Color(0xFF20212B), onSurface = Color(0xFFF4F4F6))) {
        Surface(Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
            if (!connection.approved || connection.merchant == null) PairingScreen(model, connection)
            else {
                val checkout: CheckoutViewModel = viewModel(key = "checkout-${connection.merchant!!.peerId}-${connection.localPeer}", factory = object : ViewModelProvider.Factory {
                    @Suppress("UNCHECKED_CAST") override fun <T : ViewModel> create(modelClass: Class<T>): T = CheckoutViewModel(model.connection.checkoutFor(connection.merchant!!.peerId, connection.localPeer)) as T
                })
                LaunchedEffect(checkout, lifecycle) {
                    lifecycle.repeatOnLifecycle(Lifecycle.State.STARTED) {
                        while (true) { if (model.state.value.online) checkout.refresh(); delay(5000) }
                    }
                }
                CheckoutApp(checkout, connection.online, connection.merchant!!.name, connection.detail,
                    manuallyDisconnected = connection.manuallyDisconnected, connectionBusy = connection.busy,
                    disconnect = model::disconnect, reconnect = model::reconnect) { PairingScreen(model, connection) }
            }
        }
    }
}
@Composable
fun CheckoutApp(model: CheckoutViewModel, online: Boolean, merchant: String, connectionDetail: String,
    manuallyDisconnected: Boolean = false, connectionBusy: Boolean = false,
    disconnect: () -> Unit = {}, reconnect: () -> Unit = {}, settings: @Composable () -> Unit) {
    val state by model.state.collectAsStateWithLifecycle()
    var screen by rememberSaveable { mutableStateOf("sale") }
    var menu by remember { mutableStateOf(false) }
    LaunchedEffect(model, online) { model.connectionChanged(online) }
    BackHandler(screen != "sale") { screen = "sale" }
    Column(Modifier.fillMaxSize().safeDrawingPadding()) {
        Row(Modifier.fillMaxWidth().padding(horizontal = 20.dp, vertical = 8.dp), verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text(if (screen == "sale") merchant else if (screen == "transactions") "Transactions" else "Settings", style = MaterialTheme.typography.titleMedium)
            }
            Box {
                TextButton(onClick = { menu = true }, modifier = Modifier.testTag("settingsMenu")) { Text("Menu") }
                DropdownMenu(expanded = menu, onDismissRequest = { menu = false }) {
                    DropdownMenuItem(text = { Text("Checkout") }, onClick = { menu = false; screen = "sale" })
                    DropdownMenuItem(text = { Text("Transactions") }, onClick = { menu = false; screen = "transactions" })
                    DropdownMenuItem(text = { Text("Settings") }, onClick = { menu = false; screen = "settings" })
                }
            }
        }
        if (!online) Text(connectionDetail, color = Color(0xFFFFD477), modifier = Modifier.padding(horizontal = 24.dp), fontSize = 12.sp)
        Box(Modifier.weight(1f).fillMaxWidth()) {
            when (screen) {
                "settings" -> settings()
                "transactions" -> TransactionsScreen(state, model::page)
                else -> if (state.order == null) AmountScreen(state, online, model::key, model::create)
                        else InvoiceScreen(state, model::newOrder)
            }
        }
    }
}
@Composable
private fun AmountScreen(state: CheckoutState, online: Boolean, key: (String) -> Unit, create: () -> Unit) {
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(24.dp), verticalArrangement = Arrangement.spacedBy(16.dp)) {
        Spacer(Modifier.height(20.dp))
        Surface(color = Color(0xFF425665), shape = RoundedCornerShape(20.dp), modifier = Modifier.fillMaxWidth()) {
            Column(Modifier.padding(24.dp)) {
                Text("XMR", color = Accent, fontSize = 16.sp)
                Text(state.input, fontSize = 34.sp, fontFamily = FontFamily.Monospace, modifier = Modifier.testTag("amount"))
            }
        }
        if (state.error.isNotEmpty()) ErrorText(state.error)
        for (row in listOf(listOf("1", "2", "3"), listOf("4", "5", "6"), listOf("7", "8", "9"))) {
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(14.dp)) {
                row.forEach { digit -> Key(digit, Modifier.weight(1f), !state.busy) { key(digit) } }
            }
        }
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(14.dp)) {
            Key("0", Modifier.weight(2f), !state.busy) { key("0") }
            Key(".", Modifier.weight(1f), !state.busy) { key(".") }
        }
        HorizontalDivider(color = Color(0xFF474A58))
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(14.dp)) {
            Button(onClick = { key("clear") }, modifier = Modifier.weight(1f).height(64.dp).testTag("clearAmount"), colors = ButtonDefaults.buttonColors(containerColor = Color(0xFFFF5555), contentColor = Color.Black)) { Text("Clear") }
            Button(onClick = { key("delete") }, modifier = Modifier.weight(1f).height(64.dp), colors = ButtonDefaults.buttonColors(containerColor = Color(0xFFF2FF88), contentColor = Color.Black)) { Text("Delete") }
            Button(onClick = create, enabled = online && state.initialized && !state.busy && XmrInput.valid(state.input), modifier = Modifier.weight(1f).height(64.dp).testTag("createInvoice"), colors = ButtonDefaults.buttonColors(containerColor = Green, contentColor = Color.Black)) { Text("Charge") }
        }
    }
}
@Composable
private fun RowScope.Key(label: String, modifier: Modifier, enabled: Boolean, action: () -> Unit) {
    OutlinedButton(onClick = action, enabled = enabled, modifier = modifier.height(64.dp).testTag("key$label"), shape = RoundedCornerShape(24.dp), border = BorderStroke(1.5.dp, Color(0xFF707BA8)), colors = ButtonDefaults.outlinedButtonColors(contentColor = Accent, containerColor = Color(0xFF30323F))) { Text(label, fontSize = 24.sp) }
}
@Composable
private fun InvoiceScreen(state: CheckoutState, newOrder: () -> Unit) {
    val invoice = state.invoice
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(24.dp), horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(18.dp)) {
        if (invoice?.accepted == true) {
            Text("✓", color = Green, fontSize = 90.sp)
            Text(if (invoice.status == "paid") "Payment confirmed" else "Payment accepted", style = MaterialTheme.typography.headlineMedium, modifier = Modifier.testTag("paymentSuccess"))
            Text("${XmrInput.display(invoice.amount)} XMR", fontSize = 28.sp)
            Text(if (invoice.status == "paid") "The invoice has met its confirmation policy." else "Full amount received. Waiting for ${invoice.confirmations} confirmations.", color = Muted)
            if (invoice.review) ErrorText("The merchant has flagged this invoice for review.")
            Button(onClick = newOrder, enabled = !state.busy, modifier = Modifier.fillMaxWidth().height(64.dp).testTag("newOrder"), colors = ButtonDefaults.buttonColors(containerColor = Green, contentColor = Color.Black)) { Text("New order") }
        } else {
            Text(if (invoice?.status == "partial") "Partial payment received" else "Waiting for payment", style = MaterialTheme.typography.headlineMedium)
            Text("${XmrInput.display(invoice?.remaining ?: state.order!!.amount)} XMR", fontSize = 30.sp)
            if (state.busy) CircularProgressIndicator(color = Accent)
            if (invoice != null && invoice.creationState == "ready") {
                if (!invoice.stale && invoice.paymentUri.isNotBlank() && invoice.status in setOf("pending", "partial")) PaymentQr(invoice.paymentUri)
                Text("Received ${XmrInput.display(invoice.received)} XMR", color = Muted)
                Text("Status: ${invoice.status}${if (invoice.stale) " · last known, reconnecting" else ""}", color = if (invoice.stale) Color(0xFFFFD477) else Accent, modifier = Modifier.testTag("invoiceStatus"))
            }
            if (invoice == null || invoice.creationState != "ready") {
                Button(onClick = newOrder, enabled = !state.busy, modifier = Modifier.testTag("newOrder")) { Text("New order") }
            }
        }
        if (state.error.isNotEmpty()) ErrorText(state.error)
        if (invoice?.review == true && !invoice.accepted) ErrorText("Merchant review required after a payment record changed.")
        Text("Order ${state.order!!.id.take(8)}", color = Muted, fontFamily = FontFamily.Monospace)
    }
}
@Composable
private fun PaymentQr(uri: String) {
    val image by produceState<ImageBitmap?>(null, uri) {
        value = withContext(Dispatchers.Default) { runCatching {
            val matrix = QRCodeWriter().encode(uri, BarcodeFormat.QR_CODE, 512, 512, mapOf(EncodeHintType.MARGIN to 4))
            val pixels = IntArray(512 * 512) { i -> if (matrix[i % 512, i / 512]) android.graphics.Color.BLACK else android.graphics.Color.WHITE }
            Bitmap.createBitmap(pixels, 512, 512, Bitmap.Config.ARGB_8888).asImageBitmap()
        }.getOrNull() }
    }
    image?.let { Image(it, contentDescription = "Monero payment QR", modifier = Modifier.sizeIn(maxWidth = 300.dp, maxHeight = 300.dp).fillMaxWidth().aspectRatio(1f).testTag("paymentQr")) }
}
@Composable
private fun TransactionsScreen(state: CheckoutState, page: (Int) -> Unit) {
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(24.dp), verticalArrangement = Arrangement.spacedBy(14.dp)) {
        Text("Invoices", style = MaterialTheme.typography.titleLarge)
        if (state.historyError.isNotEmpty()) ErrorText(state.historyError)
        if (state.invoices.isEmpty()) Text("No invoices on this page.", color = Muted)
        state.invoices.forEach { invoice ->
            Card(Modifier.fillMaxWidth()) {
                Column(Modifier.padding(18.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                    Text("${XmrInput.display(invoice.amount)} XMR", fontSize = 21.sp)
                    Text(invoice.status.replaceFirstChar { it.uppercase() } + if (invoice.stale) " · last known" else "", color = if (invoice.accepted) Green else Accent)
                    Text("Received ${XmrInput.display(invoice.received)} · remaining ${XmrInput.display(invoice.remaining)} XMR", color = Muted, fontSize = 12.sp)
                    Text("Order ${invoice.orderId.take(8)} · ${DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT).format(Date(invoice.createdAt))}", color = Muted, fontSize = 11.sp)
                    if (invoice.review) ErrorText("Review required")
                }
            }
        }
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            TextButton(onClick = { page((state.offset - 5).coerceAtLeast(0)) }, enabled = state.offset > 0 && !state.busy) { Text("Previous") }
            TextButton(onClick = { state.next?.let(page) }, enabled = state.next != null && !state.busy) { Text("Next") }
        }
    }
}
@Composable
private fun ErrorText(text: String) { Text(text, color = Color(0xFFFF8C8C), fontSize = 13.sp) }
