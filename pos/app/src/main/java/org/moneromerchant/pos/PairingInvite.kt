package org.moneromerchant.pos

import org.json.JSONObject
import java.net.URI

const val POS_PROTOCOL = "/monero-merchant/delivery/2.0.0"

data class MerchantEndpoint(val peerId: String, val topic: String, val name: String, val publicKey: String = "", val network: String = "logos.dev")
data class PairingInvite(val endpoint: MerchantEndpoint, val secret: String, val expiresAt: Long) {
    companion object {
        fun parse(text: String, nowSeconds: Long = System.currentTimeMillis() / 1000): PairingInvite {
            require(text.length <= 8192) { "Invitation is too large" }
            val raw = text.trim().let {
                if (it.startsWith("moneropos://pair?")) {
                    val uri = URI(it)
                    require(uri.rawQuery.startsWith("data=") && !uri.rawQuery.contains('&')) { "Invalid invitation link" }
                    String(java.util.Base64.getUrlDecoder().decode(uri.rawQuery.removePrefix("data=")), Charsets.UTF_8)
                } else it
            }
            val json = JSONObject(raw)
            require(json.getInt("version") == 2 && json.getString("protocol") == POS_PROTOCOL) { "Unsupported pairing protocol" }
            val peerId = json.getString("peer_id")
            val publicKey = json.getString("public_key")
            require(DeliveryCrypto.identity(DeliveryCrypto.decode(publicKey)) == peerId) { "Invalid merchant identity pin" }
            val network = json.getString("network")
            require(network == "logos.dev") { "Unsupported Delivery network" }
            val topic = json.getString("topic")
            require(topic == DeliveryCrypto.requestTopic(peerId)) { "Invalid merchant topic" }
            val expiry = json.getLong("expires_at")
            require(expiry > nowSeconds && expiry <= nowSeconds + 600) { "Invitation expired or device clock is incorrect" }
            val secret = json.getString("secret")
            require(secret.matches(Regex("[A-Za-z0-9_-]{43}"))) { "Invalid invitation secret" }
            val name = json.getString("merchant_name")
            require(name.isNotBlank() && name.length <= 80) { "Invalid merchant name" }
            return PairingInvite(MerchantEndpoint(peerId, topic, name, publicKey, network), secret, expiry)
        }
    }
}
