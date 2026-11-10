package org.moneromerchant.pos

import android.graphics.Bitmap
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.SystemBarStyle
import androidx.activity.compose.BackHandler
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
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
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.withStyle
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
private val Green = Color(0xFF45F879)
private val Muted = Color.White.copy(alpha = 0.4f)
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
    PosTheme {
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
                    disconnect = model::disconnect, reconnect = model::reconnect) { PairingScreen(model, connection, Modifier) }
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
                if (screen == "sale") {
                    Text(buildAnnotatedString {
                        append(merchant)
                        withStyle(SpanStyle(fontSize = MaterialTheme.typography.labelSmall.fontSize, color = Muted)) {
                            append(" v${BuildConfig.VERSION_NAME}")
                        }
                    }, style = MaterialTheme.typography.titleMedium)
                } else {
                    Text(if (screen == "transactions") "Transactions" else "Settings", style = MaterialTheme.typography.titleMedium)
                }
            }
            Box {
                FilledIconButton(onClick = { menu = true }, modifier = Modifier.testTag("settingsMenu"),
                    colors = IconButtonDefaults.filledIconButtonColors(
                        containerColor = MaterialTheme.colorScheme.secondaryContainer,
                        contentColor = MaterialTheme.colorScheme.onPrimary,
                    )) {
                    Icon(painterResource(R.drawable.settings_24px), contentDescription = "Open menu", modifier = Modifier.size(20.dp))
                }
                DropdownMenu(expanded = menu, onDismissRequest = { menu = false }) {
                    DropdownMenuItem(text = { Text("Checkout") }, onClick = { menu = false; screen = "sale" })
                    DropdownMenuItem(text = { Text("Transactions") }, onClick = { menu = false; screen = "transactions" })
                    DropdownMenuItem(text = { Text("Settings") }, onClick = { menu = false; screen = "settings" })
                }
            }
        }
        if (!online && screen != "settings") Text(connectionDetail, color = Color(0xFFFFD477), modifier = Modifier.padding(horizontal = 24.dp), fontSize = 12.sp)
        Box(Modifier.weight(1f).fillMaxWidth()) {
            when (screen) {
                "settings" -> settings()
                "transactions" -> TransactionsScreen(state, model::loadMoreInvoices)
                else -> if (state.order == null) AmountScreen(state, online, model::key, model::create)
                        else InvoiceScreen(state, model::newOrder)
            }
        }
    }
}
@Composable
private fun AmountScreen(state: CheckoutState, online: Boolean, key: (String) -> Unit, create: () -> Unit) {
    BoxWithConstraints(Modifier.fillMaxSize().padding(24.dp)) {
        // Fill tall screens, but let the controls scroll on compact displays.
        val viewportHeight = maxHeight
        Column(
            Modifier.fillMaxWidth().verticalScroll(rememberScrollState()).heightIn(min = viewportHeight),
            verticalArrangement = Arrangement.SpaceBetween,
        ) {
            Column(Modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                Surface(color = MaterialTheme.colorScheme.surfaceVariant, shape = MaterialTheme.shapes.medium, modifier = Modifier.fillMaxWidth()) {
                    Column(Modifier.padding(24.dp)) {
                        Text("XMR", color = MaterialTheme.colorScheme.primary, fontSize = 16.sp)
                        Text(state.input, fontSize = 32.sp, modifier = Modifier.testTag("amount"))
                    }
                }
                if (state.error.isNotEmpty()) ErrorText(state.error)
                Spacer(Modifier.height(10.dp))
            }
            Column(Modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                for (row in listOf(listOf("1", "2", "3"), listOf("4", "5", "6"), listOf("7", "8", "9"))) {
                    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                        row.forEach { digit -> Key(digit, Modifier.weight(1f), !state.busy) { key(digit) } }
                    }
                }
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    Key("0", Modifier.weight(2f), !state.busy) { key("0") }
                    Key(".", Modifier.weight(1f), !state.busy) { key(".") }
                }
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    CheckoutControl(R.drawable.close_24px, "Clear", Modifier.weight(1f).testTag("clearAmount"), !state.busy, Color.White, MaterialTheme.colorScheme.onPrimaryContainer) { key("clear") }
                    CheckoutControl(R.drawable.arrow_back_24px, "Delete", Modifier.weight(1f), !state.busy, MaterialTheme.colorScheme.secondaryContainer, MaterialTheme.colorScheme.onSecondaryContainer) { key("delete") }
                    CheckoutControl(R.drawable.check_24px, "Charge", Modifier.weight(1f).testTag("createInvoice"), online && state.initialized && !state.busy && XmrInput.valid(state.input), MaterialTheme.colorScheme.primary, MaterialTheme.colorScheme.onPrimary, create)
                }
            }
        }
    }
}
@Composable
private fun RowScope.Key(label: String, modifier: Modifier, enabled: Boolean, action: () -> Unit) {
    Surface(onClick = action, enabled = enabled, modifier = modifier.height(64.dp).testTag("key$label"),
        shape = MaterialTheme.shapes.medium,
        color = MaterialTheme.colorScheme.surface,
        contentColor = MaterialTheme.colorScheme.onSurface.copy(alpha = if (enabled) 1f else 0.38f)) {
        Box(contentAlignment = Alignment.Center) {
            Text(label, style = MaterialTheme.typography.titleLarge)
        }
    }
}
@Composable
private fun CheckoutControl(icon: Int, label: String, modifier: Modifier, enabled: Boolean,
    containerColor: Color, contentColor: Color, action: () -> Unit) {
    FilledTonalButton(onClick = action, enabled = enabled, modifier = modifier.height(64.dp),
        colors = ButtonDefaults.buttonColors(
            containerColor = containerColor,
            contentColor = contentColor,
            disabledContainerColor = containerColor,
            disabledContentColor = contentColor.copy(alpha = 0.38f),
        )) {
        Icon(painterResource(icon), contentDescription = label, modifier = Modifier.size(28.dp))
    }
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
            if (state.busy) CircularProgressIndicator(color = MaterialTheme.colorScheme.primary)
            if (invoice != null && invoice.creationState == "ready") {
                if (!invoice.stale && invoice.paymentUri.isNotBlank() && invoice.status in setOf("pending", "partial")) PaymentQr(invoice.paymentUri)
                Text("Received ${XmrInput.display(invoice.received)} XMR", color = Muted)
                Text("Status: ${invoice.status}${if (invoice.stale) " · last known, reconnecting" else ""}", color = if (invoice.stale) Color(0xFFFFD477) else MaterialTheme.colorScheme.primary, modifier = Modifier.testTag("invoiceStatus"))
            }
            FilledTonalButton(onClick = newOrder, enabled = !state.busy,
                modifier = Modifier.fillMaxWidth().height(64.dp).testTag("cancelOrder")) { Text("Cancel") }
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
    image?.let { Image(it, contentDescription = "Monero payment QR", modifier = Modifier.fillMaxWidth().aspectRatio(1f).testTag("paymentQr")) }
}
@Composable
private fun TransactionsScreen(state: CheckoutState, loadMore: () -> Unit) {
    val listState = rememberLazyListState()
    val nearEnd by remember {
        derivedStateOf {
            val layout = listState.layoutInfo
            (layout.visibleItemsInfo.lastOrNull()?.index ?: -1) >= layout.totalItemsCount - 3
        }
    }
    LaunchedEffect(nearEnd, state.next, state.historyLoading, state.busy, state.historyError) {
        if (nearEnd && state.next != null && !state.historyLoading && !state.busy && state.historyError.isEmpty()) loadMore()
    }
    LazyColumn(Modifier.fillMaxSize().testTag("invoiceList"), state = listState,
        contentPadding = PaddingValues(24.dp), verticalArrangement = Arrangement.spacedBy(14.dp)) {
        item(key = "heading") { Text("Invoices", style = MaterialTheme.typography.titleLarge) }
        if (state.invoices.isEmpty() && state.historyError.isEmpty()) {
            item(key = "empty") { Text("No invoices yet.", color = Muted) }
        }
        items(state.invoices, key = { it.id }) { invoice ->
            Card(Modifier.fillMaxWidth()) {
                Column(Modifier.padding(18.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                    Text("${XmrInput.display(invoice.amount)} XMR", fontSize = 21.sp)
                    Text(invoice.status.replaceFirstChar { it.uppercase() } + if (invoice.stale) " · last known" else "", color = if (invoice.accepted) Green else MaterialTheme.colorScheme.primary)
                    Text("Received ${XmrInput.display(invoice.received)} · remaining ${XmrInput.display(invoice.remaining)} XMR", color = Muted, fontSize = 12.sp)
                    Text("Order ${invoice.orderId.take(8)} · ${DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT).format(Date(invoice.createdAt))}", color = Muted, fontSize = 11.sp)
                    if (invoice.review) ErrorText("Review required")
                }
            }
        }
        if (state.historyLoading) {
            item(key = "loading") {
                Box(Modifier.fillMaxWidth(), contentAlignment = Alignment.Center) { CircularProgressIndicator() }
            }
        }
        if (state.historyError.isNotEmpty()) {
            item(key = "error") {
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    ErrorText(state.historyError)
                    if (state.next != null) TextButton(onClick = loadMore, enabled = !state.historyLoading && !state.busy) { Text("Retry") }
                }
            }
        }
    }
}
@Composable
private fun ErrorText(text: String) { Text(text, color = Color(0xFFFF8C8C), fontSize = 13.sp) }
