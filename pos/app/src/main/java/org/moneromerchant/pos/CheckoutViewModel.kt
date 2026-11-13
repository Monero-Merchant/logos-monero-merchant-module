package org.moneromerchant.pos

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import java.util.UUID

data class CheckoutState(
    val input: String = "0", val order: OrderIntent? = null, val invoice: PosInvoice? = null,
    val invoices: List<PosInvoice> = emptyList(), val offset: Int = 0, val next: Int? = null,
    val historyLoading: Boolean = false, val busy: Boolean = false, val initialized: Boolean = false, val error: String = "", val historyError: String = "",
)
class CheckoutViewModel(private val gateway: CheckoutGateway) : ViewModel() {
    private val mutable = MutableStateFlow(CheckoutState())
    val state = mutable.asStateFlow()
    private val operations = Mutex()
    private var connected = true
    fun connectionChanged(online: Boolean) {
        connected = online
        if (!online) mutable.value = mutable.value.copy(invoice = mutable.value.invoice?.copy(stale = true), invoices = mutable.value.invoices.map { it.copy(stale = true) })
    }
    init {
        viewModelScope.launch {
            try { val order = gateway.savedOrder(); mutable.value = mutable.value.copy(order = order, input = order?.amount ?: "0", initialized = true) }
            catch (e: Exception) { mutable.value = mutable.value.copy(error = "Could not restore the last order: ${e.message}") }
        }
    }
    fun key(key: String) {
        val state = mutable.value
        if (!state.busy && state.initialized && state.order == null) mutable.value = state.copy(input = XmrInput.key(state.input, key), error = "")
    }
    fun create() {
        if (mutable.value.busy || !mutable.value.initialized) return
        mutable.value = mutable.value.copy(busy = true, error = "")
        viewModelScope.launch {
            operations.withLock {
                try {
                    val order = mutable.value.order ?: OrderIntent(UUID.randomUUID().toString(), XmrInput.canonical(mutable.value.input))
                    // Save BEFORE making a network request. Never replace this ID
                    // automatically, even if the response is lost or the app exits.
                    gateway.saveOrder(order)
                    mutable.value = mutable.value.copy(order = order)
                    val invoice = gateway.createInvoice(order)
                    mutable.value = mutable.value.copy(invoice = invoice.copy(stale = invoice.stale || !connected))
                } catch (e: CancellationException) { throw e }
                catch (e: Exception) { mutable.value = mutable.value.copy(error = e.message ?: "Invoice request failed. Retry this order.") }
                finally { mutable.value = mutable.value.copy(busy = false) }
            }
        }
    }
    suspend fun refreshActiveInvoice() {
        if (!mutable.value.initialized || mutable.value.busy || mutable.value.historyLoading || !operations.tryLock()) return
        try {
            val order = mutable.value.order
            if (order != null) {
                try {
                    val invoice = gateway.getInvoice(order.id)
                    // A late update for a completed order must not take the next
                    // order away from its keypad.
                    if (mutable.value.order?.id == order.id) mutable.value = mutable.value.copy(invoice = invoice.copy(stale = invoice.stale || !connected), error = "")
                } catch (e: CancellationException) { throw e }
                catch (e: Exception) { mutable.value = mutable.value.copy(invoice = mutable.value.invoice?.copy(stale = true), error = e.message ?: "Status unavailable") }
            }
        } finally { operations.unlock() }
    }
    suspend fun refreshHistory() {
        if (!mutable.value.initialized || mutable.value.busy || mutable.value.historyLoading || !operations.tryLock()) return
        try {
            try {
                // Refresh the loaded range together so polling never replaces
                // the scrollable history with just one five-invoice page.
                val lastOffset = mutable.value.offset
                val lastInvoiceId = mutable.value.invoices.lastOrNull()?.id
                val invoices = mutableListOf<PosInvoice>()
                var offset = 0
                var page: InvoicePage
                while (true) {
                    page = gateway.getInvoices(offset)
                    invoices += page.invoices
                    val next = page.next
                    if (next == null || next <= offset || (offset >= lastOffset && (lastInvoiceId == null || invoices.any { it.id == lastInvoiceId }))) break
                    offset = next
                }
                mutable.value = mutable.value.copy(
                    invoices = invoices.distinctBy { it.id }.map { it.copy(stale = it.stale || !connected) },
                    offset = offset, next = page.next?.takeIf { it > offset }, historyError = "",
                )
            } catch (e: CancellationException) { throw e }
            catch (e: Exception) { mutable.value = mutable.value.copy(invoices = mutable.value.invoices.map { it.copy(stale = true) }, historyError = e.message ?: "Transactions unavailable") }
        } finally { operations.unlock() }
    }
    fun loadMoreInvoices() {
        if (mutable.value.busy || mutable.value.historyLoading || mutable.value.next == null) return
        mutable.value = mutable.value.copy(historyLoading = true, historyError = "")
        viewModelScope.launch {
            operations.withLock {
                try {
                    val offset = mutable.value.next ?: return@withLock
                    val page = gateway.getInvoices(offset)
                    mutable.value = mutable.value.copy(
                        invoices = (mutable.value.invoices + page.invoices.map { it.copy(stale = it.stale || !connected) }).distinctBy { it.id },
                        offset = offset, next = page.next?.takeIf { it > offset }, historyError = "",
                    )
                } catch (e: CancellationException) { throw e }
                catch (e: Exception) { mutable.value = mutable.value.copy(historyError = e.message ?: "Could not load more invoices") }
                finally { mutable.value = mutable.value.copy(historyLoading = false) }
            }
        }
    }
    fun newOrder() {
        if (mutable.value.busy) return
        mutable.value = mutable.value.copy(busy = true)
        viewModelScope.launch {
            operations.withLock {
                try {
                    gateway.saveOrder(null); mutable.value = mutable.value.copy(order = null, invoice = null, input = "0", error = "")
                }
                catch (e: Exception) { mutable.value = mutable.value.copy(error = e.message ?: "Could not start a new order") }
                finally { mutable.value = mutable.value.copy(busy = false) }
            }
        }
    }
}
