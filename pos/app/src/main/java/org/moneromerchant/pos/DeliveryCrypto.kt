package org.moneromerchant.pos

import org.bouncycastle.crypto.AsymmetricCipherKeyPair
import org.bouncycastle.crypto.hpke.HPKE
import org.bouncycastle.crypto.params.X25519PrivateKeyParameters
import org.bouncycastle.crypto.params.X25519PublicKeyParameters
import org.json.JSONObject
import java.security.MessageDigest
import java.security.SecureRandom
import java.util.Base64
import java.util.UUID

/** RFC 9180 AUTH mode: X25519/HKDF-SHA256/AES-128-GCM, interoperable with pyhpke. */
object DeliveryCrypto {
    private val info = "monero-merchant/delivery/2/hpke-auth".toByteArray()
    private fun suite() = HPKE(HPKE.mode_auth, HPKE.kem_X25519_SHA256, HPKE.kdf_HKDF_SHA256, HPKE.aead_AES_GCM128)
    fun b64(bytes: ByteArray): String = Base64.getUrlEncoder().withoutPadding().encodeToString(bytes)
    fun decode(text: String): ByteArray = Base64.getUrlDecoder().decode(text)
    fun generateKey(): ByteArray = X25519PrivateKeyParameters(SecureRandom()).encoded
    fun publicKey(key: ByteArray): ByteArray = X25519PrivateKeyParameters(key).generatePublicKey().encoded
    fun identity(public: ByteArray): String {
        require(public.size == 32) { "Invalid identity key" }
        return MessageDigest.getInstance("SHA-256").digest(public).joinToString("") { "%02x".format(it) }
    }
    fun requestTopic(merchant: String) = "/monero-merchant/2/$merchant/json"
    fun replyTopic(merchant: String, terminal: String): String = "/monero-pos/2/${MessageDigest.getInstance("SHA-256").digest((merchant+terminal).toByteArray()).joinToString("") { "%02x".format(it) }}/json"
    private fun keys(key: ByteArray): AsymmetricCipherKeyPair {
        val private = X25519PrivateKeyParameters(key)
        return AsymmetricCipherKeyPair(private.generatePublicKey(), private)
    }
    private fun aad(direction: String, topic: String, sender: String, recipient: String, id: String, issued: Long): ByteArray {
        UUID.fromString(id)
        require(!topic.contains('\n') && topic.length <= 200)
        return listOf("2",direction,topic,sender,recipient,id,issued.toString()).joinToString("\n").toByteArray()
    }
    fun seal(key: ByteArray, recipientPublic: ByteArray, topic: String, direction: String, id: String, payload: JSONObject, now: Long = System.currentTimeMillis()/1000): ByteArray {
        val sender = b64(publicKey(key)); val recipient = b64(recipientPublic)
        val context = suite().setupAuthS(X25519PublicKeyParameters(recipientPublic), info, keys(key))
        val ciphertext = context.seal(aad(direction,topic,sender,recipient,id,now), payload.toString().toByteArray())
        return JSONObject().put("v",2).put("id",id).put("sender",sender).put("recipient",recipient).put("issued_at",now)
            .put("enc",b64(context.encapsulation)).put("ciphertext",b64(ciphertext)).toString().toByteArray()
    }
    fun open(key: ByteArray, senderPublic: ByteArray, topic: String, direction: String, id: String, raw: ByteArray, now: Long = System.currentTimeMillis()/1000): JSONObject {
        require(raw.size <= 49152) { "Message too large" }
        val envelope = JSONObject(String(raw,Charsets.UTF_8))
        require(envelope.getInt("v") == 2 && envelope.getString("id") == id) { "Mismatched Delivery reply" }
        require(envelope.getString("sender") == b64(senderPublic) && envelope.getString("recipient") == b64(publicKey(key))) { "Merchant identity mismatch" }
        val issued = envelope.getLong("issued_at")
        require(issued in (now-120)..(now+120)) { "Reply expired or clock incorrect" }
        val context = suite().setupAuthR(decode(envelope.getString("enc")),keys(key), info,X25519PublicKeyParameters(senderPublic))
        return JSONObject(String(context.open(aad(direction,topic,envelope.getString("sender"),envelope.getString("recipient"),id,issued),decode(envelope.getString("ciphertext"))),Charsets.UTF_8))
    }
}
