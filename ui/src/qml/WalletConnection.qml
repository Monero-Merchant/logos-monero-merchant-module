import QtQuick

QtObject {
    id: connection
    property var client: null
    property bool ready: false
    property var watchReply: null
    property var status: null
    property string error: ""
    property bool checking: false
    property int generation: 0
    readonly property bool backendConnected: ready && status !== null && status.ok === true
    readonly property bool walletOpen: !!status && backendConnected && typeof status.wallet === "string" && status.wallet.length > 0
    readonly property bool nodeConnected: walletOpen && !!status && status.connected === true
    readonly property bool synced: walletOpen && !!status && status.synchronized === true
    readonly property bool canReceive: !!status && nodeConnected && synced && status.network === "stagenet"
    readonly property string summary: !ready ? "Merchant backend unavailable"
        : checking && !status ? "Checking wallet backend…"
        : !backendConnected || !status ? "Wallet backend unavailable"
        : !walletOpen ? "Open a wallet to get started"
        : status.network !== "stagenet" ? "Select a Stagenet wallet"
        : !nodeConnected ? "Wallet node disconnected"
        : !synced ? "Wallet is synchronizing"
        : "Ready to receive payments"
    property Timer timeout: Timer {
        interval: 15000
        onTriggered: {
            ++connection.generation; connection.checking = false; connection.status = null
            connection.error = "Wallet backend did not respond. Check its module and try again."
        }
    }
    property Timer recovery: Timer { interval: 15000; repeat: true; running: connection.ready; onTriggered: connection.refresh() }
    onReadyChanged: {
        ++generation; checking = false; timeout.stop(); status = null; error = ""
        if (ready) refresh()
    }
    function reset() {
        ++generation; checking = false; timeout.stop(); status = null; error = ""
        refresh()
    }
    function refresh() {
        if (!ready || !client || !watchReply || checking) return
        checking = true; timeout.restart()
        const token = ++generation
        function failed(reason) {
            if (token !== connection.generation || !connection.ready) return
            connection.status = null; connection.checking = false; connection.timeout.stop()
            connection.error = String(reason)
        }
        try {
            watchReply(client.getWalletStatus(), function(raw) {
                if (token !== connection.generation || !connection.ready) return
                try {
                    const value = JSON.parse(String(raw))
                    if (value.ok !== true) throw new Error(value.error || "Wallet backend unavailable.")
                    connection.status = value; connection.error = ""; connection.checking = false; connection.timeout.stop()
                } catch (e) { failed("Could not check wallet backend: " + String(e)) }
            }, failed)
        } catch (e) { failed(e) }
    }
}
