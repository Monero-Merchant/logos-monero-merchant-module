package org.moneromerchant.pos

/** Blocking liblogosdelivery calls. MerchantConnection serializes them on Dispatchers.IO. */
object NativeLib {
    init { System.loadLibrary("merchant_delivery") }
    external fun open(config: String): Long
    /** Current Delivery node status; does not establish merchant reachability. */
    external fun connectionStatus(handle: Long): String
    /** Next raw Delivery status-change JSON, or null after timeout; oldest events may be dropped. */
    external fun receiveConnectionStatusChange(handle: Long, timeoutMs: Int): String?
    external fun subscribe(handle: Long, topic: String)
    external fun send(handle: Long, message: String)
    external fun receive(handle: Long, timeoutMs: Int): String?
    /** Writes a cumulative native queue snapshot in debug builds. */
    external fun logQueueStats(handle: Long)
    external fun close(handle: Long)
}
