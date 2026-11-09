package org.moneromerchant.pos

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import org.json.JSONObject
import java.io.File
import java.security.KeyStore
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

/** Peer private key and pinned merchant identity are one atomic, encrypted, non-backed-up record. */
class SecureStore(context: Context) {
    private val file = File(context.noBackupFilesDir, "pairing.enc")
    private val deliveryDirectory = File(context.noBackupFilesDir, "delivery")
    fun deliveryPath(): String { check(deliveryDirectory.isDirectory || deliveryDirectory.mkdirs()) { "Could not create Delivery storage" }; return deliveryDirectory.absolutePath }
    private val alias = "merchant-pos-pairing-v1"
    private fun key(): SecretKey {
        val store = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
        (store.getKey(alias, null) as? SecretKey)?.let { return it }
        return KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore").run {
            init(KeyGenParameterSpec.Builder(alias, KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM).setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE).build())
            generateKey()
        }
    }
    fun load(): JSONObject? {
        if (!file.exists()) return null
        val bytes = file.readBytes()
        require(bytes.size > 28) { "Pairing storage is damaged. Forget this terminal and pair again." }
        val cipher = Cipher.getInstance("AES/GCM/NoPadding")
        cipher.init(Cipher.DECRYPT_MODE, key(), GCMParameterSpec(128, bytes.copyOfRange(0, 12)))
        return JSONObject(String(cipher.doFinal(bytes.copyOfRange(12, bytes.size)), Charsets.UTF_8))
    }
    fun save(record: JSONObject) {
        val cipher = Cipher.getInstance("AES/GCM/NoPadding")
        cipher.init(Cipher.ENCRYPT_MODE, key())
        val temporary = File(file.parentFile, "pairing.tmp")
        java.io.FileOutputStream(temporary).use { out ->
            out.write(cipher.iv + cipher.doFinal(record.toString().toByteArray(Charsets.UTF_8)))
            out.fd.sync()
        }
        check(temporary.renameTo(file)) { "Could not save pairing" }
    }
    fun clear() {
        check(!file.exists() || file.delete()) { "Could not remove pairing" }
        KeyStore.getInstance("AndroidKeyStore").apply { load(null); deleteEntry(alias) }
    }
}
