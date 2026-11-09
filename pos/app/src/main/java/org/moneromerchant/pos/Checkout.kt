package org.moneromerchant.pos

import org.json.JSONObject
import java.math.BigDecimal

data class OrderIntent(val id: String, val amount: String)
data class PosInvoice(
    val id: String, val orderId: String, val amount: String, val received: String,
    val remaining: String, val status: String, val creationState: String, val paymentUri: String,
    val confirmations: Int, val stale: Boolean, val review: Boolean, val createdAt: Long,
) {
    val accepted: Boolean get() = !stale && status in setOf("accepted", "paid")
    companion object {
        fun parse(value: JSONObject) = PosInvoice(
            value.getString("id"), value.getString("order_id"), value.getString("amount_xmr"),
            value.getString("received_xmr"), value.getString("remaining_xmr"), value.getString("status"),
            value.getString("creation_state"), value.optString("payment_uri"), value.getInt("required_confirmations"),
            value.optBoolean("stale", true), value.optBoolean("requires_review"), value.optLong("created_at"),
        )
    }
}
data class InvoicePage(val invoices: List<PosInvoice>, val next: Int?)
interface CheckoutGateway {
    suspend fun savedOrder(): OrderIntent?
    suspend fun saveOrder(order: OrderIntent?)
    suspend fun createInvoice(order: OrderIntent): PosInvoice
    suspend fun getInvoice(orderId: String): PosInvoice
    suspend fun getInvoices(offset: Int): InvoicePage
}
object XmrInput {
    fun key(current: String, key: String): String = when (key) {
        "clear" -> "0"
        "delete" -> current.dropLast(1).ifEmpty { "0" }
        "." -> if ('.' in current) current else "$current."
        else -> {
            if (!key.matches(Regex("[0-9]"))) current
            else if ('.' in current && current.substringAfter('.').length >= 12) current
            else if ('.' !in current && current != "0" && current.length >= 8) current
            else if (current == "0") key else current + key
        }
    }
    fun valid(value: String): Boolean = runCatching {
        value.matches(Regex("[0-9]{1,8}(\\.[0-9]{0,12})?")) &&
            BigDecimal(value).signum() > 0 && BigDecimal(value) <= BigDecimal("18446744.073709551615")
    }.getOrDefault(false)
    fun canonical(value: String): String {
        require(valid(value)) { "Enter a positive XMR amount (up to 12 decimal places)" }
        return BigDecimal(value).setScale(12).toPlainString()
    }
    fun display(value: String): String = runCatching { BigDecimal(value).stripTrailingZeros().toPlainString() }.getOrDefault(value)
}
