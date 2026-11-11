package org.moneromerchant.pos

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.util.Base64
import java.util.UUID
import android.util.Log
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive

/** One Delivery node, pinned merchant identity and encrypted persistent terminal key. */
class MerchantConnection(private val store: SecureStore) : CheckoutGateway {
    private val lock = Mutex()
    private var handle = 0L
    private var record: JSONObject? = null
    private fun diagnostic(message: String) {
        if (BuildConfig.DEBUG) Log.d("MerchantDelivery", message)
    }

    suspend fun restore(): MerchantEndpoint? = withContext(Dispatchers.IO) {
        lock.withLock {
            record = store.load()
            endpoint()
        }
    }
    private fun endpoint(): MerchantEndpoint? = record?.optJSONObject("merchant")?.let {
        check(it.optInt("transport_version") == 2) { "Delivery upgrade requires re-pairing. Forget the previous connection and scan a new Basecamp invitation." }
        MerchantEndpoint(it.getString("peer_id"), it.getString("topic"), it.getString("name"), it.getString("public_key"), it.getString("network"))
    }
    private fun privateKey(): ByteArray {
        val saved = record ?: JSONObject().also { record = it }
        if (!saved.has("delivery_private_key")) {
            val key = DeliveryCrypto.generateKey()
            try { saved.put("delivery_private_key", Base64.getEncoder().encodeToString(key)); store.save(saved) }
            finally { key.fill(0) }
        }
        return Base64.getDecoder().decode(saved.getString("delivery_private_key"))
    }
    private fun terminalIdentity(): String {
        val key = privateKey()
        return try { DeliveryCrypto.identity(DeliveryCrypto.publicKey(key)) } finally { key.fill(0) }
    }
    private suspend fun ensureNode() {
        if (handle != 0L) return
        currentCoroutineContext().ensureActive()
        val merchant = requireNotNull(endpoint()) { "Pair with Basecamp first" }
        val config = JSONObject().put("mode","Edge").put("preset",merchant.network)
            .put("messagingOverrides",JSONObject().put("anonymityLevel","None").put("nat","none").put("localStoragePath",store.deliveryPath())
                .put("tcp-port",0).put("discv5-udp-port",0))
        handle = NativeLib.open(config.toString())
        try {
            currentCoroutineContext().ensureActive()
            NativeLib.subscribe(handle,DeliveryCrypto.replyTopic(merchant.peerId,terminalIdentity()))
            currentCoroutineContext().ensureActive()
        }
        catch (error: Exception) { val old=handle;handle=0;NativeLib.close(old);throw error }
    }
    suspend fun pair(invite: PairingInvite, deviceName: String): Pair<MerchantEndpoint, JSONObject> = withContext(Dispatchers.IO) {
        lock.withLock {
            require(endpoint() == null) { "Forget the current merchant before pairing another" }
            val merchant = invite.endpoint
            val saved = record ?: JSONObject().also { record = it }
            // Save identity pin BEFORE network submission: a lost pairing response
            // can be recovered through Session with the same persistent peer key.
            saved.put("merchant", JSONObject().put("peer_id", merchant.peerId)
                .put("topic", merchant.topic).put("name", merchant.name).put("public_key",merchant.publicKey)
                .put("network",merchant.network).put("transport_version",2))
            store.save(saved)
            ensureNode()
            merchant to exchange("Pair", JSONObject().put("secret", invite.secret).put("device_name", deviceName))
        }
    }
    suspend fun session(): JSONObject = withContext(Dispatchers.IO) {
        lock.withLock { ensureNode(); exchange("Session") }
    }
    suspend fun status(): JSONObject = withContext(Dispatchers.IO) {
        lock.withLock { ensureNode(); exchange("GetBackendStatus") }
    }
    suspend fun localPeer(): String = withContext(Dispatchers.IO) { lock.withLock { ensureNode(); terminalIdentity() } }
    private suspend fun validateScope(scope: Pair<String, String>?) {
        if (scope == null) return
        check(endpoint()?.peerId == scope.first) { "Merchant pairing changed. Reopen checkout." }
        ensureNode()
        check(terminalIdentity() == scope.second) { "Terminal identity changed. Reopen checkout." }
    }
    fun checkoutFor(merchantPeer: String, terminalPeer: String): CheckoutGateway {
        val scope = merchantPeer to terminalPeer
        return object : CheckoutGateway {
            override suspend fun savedOrder() = readOrder(scope)
            override suspend fun saveOrder(order: OrderIntent?) = writeOrder(order, scope)
            override suspend fun createInvoice(order: OrderIntent) = PosInvoice.parse(invoiceRequest("CreateInvoice", JSONObject().put("order_id", order.id).put("amount_xmr", order.amount), scope).getJSONObject("invoice"))
            override suspend fun getInvoice(orderId: String) = PosInvoice.parse(invoiceRequest("GetInvoice", JSONObject().put("order_id", orderId), scope).getJSONObject("invoice"))
            override suspend fun getInvoices(offset: Int) = invoicePage(offset, scope)
        }
    }
    override suspend fun savedOrder(): OrderIntent? = readOrder(null)
    private suspend fun readOrder(scope: Pair<String, String>?): OrderIntent? = withContext(Dispatchers.IO) {
        lock.withLock {
            if (record == null) record = store.load()
            validateScope(scope)
            record?.optJSONObject("active_order")?.let { OrderIntent(it.getString("id"), it.getString("amount")) }
        }
    }
    override suspend fun saveOrder(order: OrderIntent?) = writeOrder(order, null)
    private suspend fun writeOrder(order: OrderIntent?, scope: Pair<String, String>?) = withContext(Dispatchers.IO) {
        lock.withLock {
            validateScope(scope)
            val saved = record ?: store.load() ?: error("Pair with a merchant first")
            if (order == null) saved.remove("active_order")
            else saved.put("active_order", JSONObject().put("id", order.id).put("amount", order.amount))
            store.save(saved); record = saved
        }
    }
    private suspend fun invoiceRequest(type: String, args: JSONObject, scope: Pair<String, String>? = null): JSONObject = withContext(Dispatchers.IO) {
        lock.withLock {
            validateScope(scope)
            ensureNode()
            val reply = exchange(type, args)
            check(reply.optBoolean("ok")) { reply.optString("message", "Invoice request failed") }
            reply
        }
    }
    override suspend fun createInvoice(order: OrderIntent): PosInvoice = PosInvoice.parse(invoiceRequest("CreateInvoice", JSONObject().put("order_id", order.id).put("amount_xmr", order.amount)).getJSONObject("invoice"))
    override suspend fun getInvoice(orderId: String): PosInvoice = PosInvoice.parse(invoiceRequest("GetInvoice", JSONObject().put("order_id", orderId)).getJSONObject("invoice"))
    override suspend fun getInvoices(offset: Int): InvoicePage {
        return invoicePage(offset, null)
    }
    private suspend fun invoicePage(offset: Int, scope: Pair<String, String>?): InvoicePage {
        val reply = invoiceRequest("GetInvoices", JSONObject().put("offset", offset), scope)
        val rows = reply.getJSONArray("invoices")
        return InvoicePage((0 until rows.length()).map { PosInvoice.parse(rows.getJSONObject(it)) }, if (reply.isNull("next_offset")) null else reply.getInt("next_offset"))
    }
    private suspend fun exchange(type: String, args: JSONObject = JSONObject()): JSONObject {
        val merchant = requireNotNull(endpoint()) { "Pair with a merchant first" }
        val id = UUID.randomUUID().toString()
        val request = JSONObject().put("version", 1).put("request_id", id).put("type", type).put("args", args)
        val key = privateKey()
        val started = System.nanoTime()
        var sends = 0
        var live = 0; var history = 0; var unknownSource = 0
        var otherTopic = 0; var malformed = 0; var rejected = 0
        var outcome = "interrupted or failed"
        try {
            val terminal = terminalIdentity()
            val replyTopic = DeliveryCrypto.replyTopic(merchant.peerId,terminal)
            val deadline = System.nanoTime()+60_000_000_000L
            var nextSend = 0L
            while (System.nanoTime()<deadline) {
                currentCoroutineContext().ensureActive()
                if (System.nanoTime() >= nextSend) {
                    // Delivery subscriptions can recover after the first send.
                    // Repeat the same application request with fresh HPKE
                    // encapsulation; the backend durably deduplicates its UUID.
                    val sealed = DeliveryCrypto.seal(key,DeliveryCrypto.decode(merchant.publicKey),merchant.topic,"request",id,request)
                    val message = JSONObject().put("contentTopic",merchant.topic)
                        .put("payload",Base64.getEncoder().encodeToString(sealed)).put("ephemeral",false)
                    NativeLib.send(handle,message.toString())
                    if (BuildConfig.DEBUG) sends++
                    nextSend = System.nanoTime()+5_000_000_000L
                }
                val event = NativeLib.receive(handle,500) ?: continue
                val reply = try {
                    val parsed = JSONObject(event)
                    if (BuildConfig.DEBUG) when (parsed.optString("source")) {
                        "live" -> live++
                        "history" -> history++
                        else -> unknownSource++
                    }
                    val data = parsed.optJSONObject("message")
                    if (data == null) { if (BuildConfig.DEBUG) malformed++; continue }
                    if (data.optString("contentTopic") != replyTopic) { if (BuildConfig.DEBUG) otherTopic++; continue }
                    val raw = Base64.getDecoder().decode(data.getString("payload"))
                    DeliveryCrypto.open(key,DeliveryCrypto.decode(merchant.publicKey),replyTopic,"reply",id,raw)
                } catch (_: Exception) { if (BuildConfig.DEBUG) rejected++; continue } // Includes malformed, untrusted and stale traffic.
                require(reply.getInt("version") == 1 && reply.getString("request_id") == id) { "Invalid merchant reply" }
                outcome = "authenticated reply"
                return reply
            }
            outcome = "reply timed out"
            error("Basecamp did not reply over Delivery. Check its connection and retry with the saved order.")
        } finally {
            key.fill(0)
            if (BuildConfig.DEBUG) {
                val elapsedMs = (System.nanoTime() - started) / 1_000_000
                diagnostic("exchange $outcome: elapsed_ms=$elapsedMs sends=$sends received_live=$live received_history=$history received_source_unknown=$unknownSource other_topic=$otherTopic missing_message=$malformed rejected_or_malformed=$rejected")
                try { NativeLib.logQueueStats(handle) } catch (_: Exception) { diagnostic("node queue snapshot unavailable") }
            }
        }
    }
    suspend fun disconnect() = withContext(Dispatchers.IO) {
        lock.withLock { if (handle != 0L) { val previous = handle; handle = 0; NativeLib.close(previous) } }
    }
    suspend fun forget() = withContext(Dispatchers.IO) {
        lock.withLock {
            if (handle != 0L) { val previous = handle; handle = 0; NativeLib.close(previous) }
            store.clear(); record = null
        }
    }
}
