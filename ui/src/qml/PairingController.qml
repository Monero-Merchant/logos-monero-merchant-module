import QtQuick
QtObject {
    id: root
    property var client: null
    property bool ready: false
    property var watchReply: null
    property bool online: false
    property bool busy: false
    property string error: ""
    property string notice: ""
    property var merchant: null
    property var devices: []
    property string invitation: ""
    property var qr: null
    property double expiresAt: 0
    property double now: Date.now() / 1000
    property int generation: 0
    readonly property int remaining: Math.max(0, Math.ceil(expiresAt - now))
    readonly property bool supported: client !== null && typeof client.getPosPairing === "function"
    property Timer refreshTimer: Timer {
        interval: 3000; repeat: true; running: root.ready && root.supported
        onTriggered: root.refresh()
    }
    property Timer expiryTimer: Timer {
        interval: 1000; repeat: true; running: root.invitation.length > 0
        onTriggered: {
            root.now = Date.now() / 1000
            if (root.remaining === 0) { root.clearInvitation(); root.notice = "Invitation expired. Generate a new QR." }
        }
    }
    property Timer timeout: Timer {
        interval: 10000
        onTriggered: {
            ++root.generation; root.busy = false; root.online = false; root.clearInvitation()
            root.error = "Pairing reply unavailable. Refresh to check the current terminal status."
        }
    }
    onReadyChanged: {
        ++generation; busy = false; timeout.stop(); online = false; clearInvitation()
        if (ready) refresh()
    }
    onClientChanged: { online = false; clearInvitation(); if (ready) refresh() }
    function clearInvitation() { invitation = ""; qr = null; expiresAt = 0 }
    function run(method, args, done) {
        if (!ready || !supported || busy || !watchReply) return
        busy = true; error = ""; timeout.restart()
        const token = ++generation
        function failed(message) {
            if (token !== root.generation) return
            root.timeout.stop(); root.busy = false; root.online = false; root.clearInvitation()
            root.error = String(message)
        }
        try {
            watchReply(client[method].apply(client, args), function(raw) {
                if (token !== root.generation || !root.ready) return
                root.timeout.stop(); root.busy = false
                try {
                    const reply = JSON.parse(String(raw))
                    if (reply.ok !== true) throw new Error(reply.error || "Pairing request failed")
                    done(reply)
                } catch (e) { failed(e) }
            }, failed)
        } catch (e) { failed(e) }
    }
    function applyDevices(rows) {
        if (!Array.isArray(rows) || !rows.every(function(row) {
            return row && typeof row.peer_id === "string" && typeof row.name === "string"
                && ["pending", "approved", "revoked"].indexOf(row.status) >= 0
        })) throw new Error("Invalid terminal records")
        if (invitation && rows.some(function(row) { return !root.devices.some(function(old) { return old.peer_id === row.peer_id }) })) {
            clearInvitation(); notice = "Terminal enrolled. Compare its identity and approve it below."
        }
        devices = rows
    }
    function refresh() {
        run("getPosPairing", [], function(reply) {
            root.applyDevices(reply.devices); root.merchant = reply.merchant; root.online = reply.ready !== false
            if (!root.online) { root.clearInvitation(); root.error = reply.error || "Logos Delivery is connecting. Refresh shortly." }
        })
    }
    function create() {
        clearInvitation(); notice = ""
        run("createPosInvitation", [], function(reply) {
            const value = reply.invitation
            if (!value || value.version !== 2 || value.protocol !== "/monero-merchant/delivery/2.0.0" || typeof value.expires_at !== "number") throw new Error("Invalid invitation")
            root.invitation = JSON.stringify(value); root.expiresAt = value.expires_at; root.now = Date.now() / 1000
            if (root.remaining <= 0) { root.clearInvitation(); throw new Error("Invitation already expired") }
            root.run("pairingQr", [root.invitation], function(code) {
                if (!Number.isInteger(code.size) || code.size < 21 || !Array.isArray(code.runs)) throw new Error("Invalid pairing QR")
                root.qr = code
            })
        })
    }
    function authorize(peer, approve) {
        notice = ""
        run(approve ? "approvePosDevice" : "revokePosDevice", [peer], function(reply) {
            root.applyDevices(reply.devices); root.notice = approve ? "Terminal approved." : "Terminal access revoked."
        })
    }
}
