package org.moneromerchant.pos

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import org.json.JSONObject

data class ConnectionUiState(
    val merchant: MerchantEndpoint? = null,
    val phase: String = "Not paired",
    val detail: String = "Scan the merchant’s pairing invitation to connect this terminal.",
    val localPeer: String = "",
    val busy: Boolean = true,
    val online: Boolean = false,
    val lastChecked: Long? = null,
    val approved: Boolean = false,
    val manuallyDisconnected: Boolean = false,
)

class ConnectionViewModel(app: Application) : AndroidViewModel(app) {
    val connection = MerchantConnection(SecureStore(app))
    private val mutable = MutableStateFlow(ConnectionUiState())
    val state = mutable.asStateFlow()
    private var active: Job? = null
    private var foreground = false
    private var desired = true
    private val initialized = CompletableDeferred<Unit>()

    init {
        viewModelScope.launch {
            try {
                val merchant = connection.restore()
                mutable.value = mutable.value.copy(merchant = merchant, busy = false,
                    phase = if (merchant == null) "Not paired" else "Disconnected")
            } catch (_: Exception) {
                mutable.value = ConnectionUiState(busy = false, phase = "Storage unavailable",
                    detail = "Saved pairing is unavailable or uses the previous transport. Forget this terminal and pair again over Delivery.")
            }
            initialized.complete(Unit)
        }
    }
    fun setForeground(value: Boolean) {
        foreground = value
        val previous = active
        previous?.cancel()
        active = viewModelScope.launch {
            initialized.await()
            previous?.join()
            connection.disconnect()
            try {
                val merchant = connection.restore()
                mutable.value = mutable.value.copy(merchant = merchant, busy = false, online = false,
                    phase = if (merchant == null) "Not paired" else "Disconnected")
                if (foreground && desired && merchant != null) watch()
            } catch (e: CancellationException) { throw e }
            catch (_: Exception) {
                mutable.value = mutable.value.copy(busy = false, online = false, phase = "Storage unavailable",
                    detail = "Saved pairing is unavailable or uses the previous transport. Forget this terminal and pair again over Delivery.")
            }
        }
    }
    fun pair(text: String, name: String) {
        if (mutable.value.busy) return
        active?.cancel(); desired = true
        mutable.value = mutable.value.copy(manuallyDisconnected = false)
        active = viewModelScope.launch {
            mutable.value = mutable.value.copy(busy = true, phase = "Connecting", detail = "Verifying merchant identity over Logos Delivery…", online = false)
            try {
                val invite = PairingInvite.parse(text)
                require(name.trim().length in 1..80) { "Enter a terminal name (1–80 characters)" }
                // Show the pin even after an uncertain submission, enabling recovery.
                val result = connection.pair(invite, name.trim())
                mutable.value = mutable.value.copy(merchant = result.first, localPeer = connection.localPeer())
                applyReply(result.second)
                mutable.value = mutable.value.copy(busy = false)
                if (foreground) watch()
            } catch (e: CancellationException) { throw e }
            catch (e: Exception) {
                connection.disconnect()
                val restored = runCatching { connection.restore() }
                mutable.value = mutable.value.copy(merchant = restored.getOrNull(), busy = false,
                    phase = if (restored.isFailure) "Storage unavailable" else "Pairing incomplete",
                    detail = e.message ?: "Pairing failed")
            }
        }
    }
    fun reconnect() {
        if (mutable.value.busy || mutable.value.merchant == null) return
        desired = true
        val previous = active
        previous?.cancel()
        mutable.value = mutable.value.copy(manuallyDisconnected = false, busy = true, online = false, phase = "Connecting", detail = "Checking Monero Merchant in Basecamp…")
        active = viewModelScope.launch {
            try { previous?.join(); connection.disconnect(); watch() }
            catch (e: CancellationException) { throw e }
            catch (e: Exception) { mutable.value = mutable.value.copy(busy = false, online = false, phase = "Disconnected", detail = e.message ?: "Could not reconnect to Basecamp") }
        }
    }
    private suspend fun watch() {
        var retry = 2000L
        while (foreground && desired) {
            try {
                mutable.value = mutable.value.copy(busy = !mutable.value.online, phase = if (mutable.value.online) mutable.value.phase else "Connecting")
                val session = connection.session()
                mutable.value = mutable.value.copy(localPeer = connection.localPeer())
                val status = session.optString("status")
                mutable.value = mutable.value.copy(approved = status == "approved")
                if (status == "approved") applyReply(connection.status()) else applyReply(session)
                mutable.value = mutable.value.copy(busy = false)
                if (status == "revoked" || status == "not_paired") { desired = false; connection.disconnect(); return }
                retry = 2000L
                delay(5000)
            } catch (e: CancellationException) { throw e }
            catch (e: Exception) {
                connection.disconnect()
                mutable.value = mutable.value.copy(busy = false, online = false, phase = "Reconnecting", detail = e.message ?: "Connection lost")
                delay(retry); retry = (retry * 2).coerceAtMost(30000)
            }
        }
    }
    private fun applyReply(reply: JSONObject) {
        val status = reply.optString("status")
        val phase: String
        val detail: String
        var online = false
        when (status) {
            "pending" -> { phase = "Awaiting approval"; detail = "Approve this terminal’s identity on the merchant computer." }
            "revoked" -> { phase = "Access revoked"; detail = "The merchant revoked this terminal. Forget this pairing before enrolling again." }
            "not_paired" -> { phase = "Not enrolled"; detail = "Pairing was not recorded. Forget this pairing and scan a new invitation." }
            "approved" -> { phase = "Paired"; detail = "Terminal approved. Checking the merchant backend…" }
            "ready" -> {
                phase = "Connected"; online = true
                val backend = reply.getJSONObject("backend")
                val wallet = when {
                    !backend.optBoolean("wallet_open") -> "no wallet open"
                    !backend.optBoolean("connected") -> "wallet offline"
                    !backend.optBoolean("synchronized") -> "wallet synchronizing"
                    else -> "wallet synchronized"
                }
                detail = "Basecamp · ${backend.optString("network", "unknown network")} · $wallet"
            }
            else -> { phase = "Backend unavailable"; detail = reply.optString("message", "Merchant request failed") }
        }
        mutable.value = mutable.value.copy(phase = phase, detail = detail, online = online, lastChecked = System.currentTimeMillis())
    }
    fun disconnect() {
        desired = false
        val previous = active
        previous?.cancel()
        mutable.value = mutable.value.copy(busy = true, online = false, manuallyDisconnected = true, phase = "Disconnected", detail = "Pairing and current order saved. Tap Reconnect to check Monero Merchant in Basecamp.")
        active = viewModelScope.launch {
            try { previous?.join(); connection.disconnect() }
            catch (e: CancellationException) { throw e }
            catch (e: Exception) { mutable.value = mutable.value.copy(detail = e.message ?: "Connection shutdown failed") }
            finally { mutable.value = mutable.value.copy(busy = false) }
        }
    }
    fun forget() {
        desired = false; active?.cancel()
        active = viewModelScope.launch {
            try { connection.forget(); mutable.value = ConnectionUiState(busy = false) }
            catch (e: Exception) { mutable.value = mutable.value.copy(busy = false, detail = e.message ?: "Could not forget pairing") }
        }
    }
}
