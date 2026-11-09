import QtQuick

QtObject {
    id: controller
    property var client: null
    property bool ready: false
    property var watchReply: null
    property var invoices: []
    property var wallet: null
    property string state: "starting"
    property string error: ""
    property string notice: ""
    property bool stale: true
    property bool storageAvailable: false
    property bool reading: false
    property bool mutating: false
    property int confirmationPolicy: 5
    property bool policyLoaded: false
    property bool savingPolicy: false
    property string policyError: ""
    property int policyRequest: 0
    property Timer policyTimeout: Timer {
        interval: 20000
        onTriggered: {
            ++controller.policyRequest; controller.savingPolicy = false; controller.policyLoaded = false
            controller.policyError = "The policy reply was lost. Refresh to check the saved setting."
            controller.read()
        }
    }
    property string selectedId: ""
    property double updatedAt: 0
    property int generation: 0
    property int mutationId: 0
    property var lastRequest: null
    property var qr: null
    property string qrForUri: ""
    signal invoiceCreated()
    signal walletContextChanged()
    readonly property var selectedInvoice: invoices.find(function(row) { return row.id === controller.selectedId }) || null
    readonly property bool canCreate: ready && storageAvailable && wallet !== null && state === "ready"
        && wallet.connected !== false && wallet.synchronized !== false && !mutating && policyLoaded && !savingPolicy
    property Timer readTimeout: Timer {
        interval: 20000
        onTriggered: { controller.reading = false; controller.error = "Invoice refresh is unavailable. Showing saved records."; controller.stale = true }
    }
    property Timer mutationTimeout: Timer {
        interval: 45000
        onTriggered: {
            ++controller.mutationId; controller.mutating = false
            controller.error = "The reply was lost. Refresh the saved invoices or retry the same invoice request to check its result."
        }
    }
    onReadyChanged: {
        ++generation
        ++policyRequest; savingPolicy = false; policyTimeout.stop(); policyLoaded = false; policyError = ""
        if (ready) { read(); loadQr() }
        else {
            reading = false; readTimeout.stop(); stale = true
            if (mutating) {
                ++mutationId; mutating = false; mutationTimeout.stop()
                error = "Connection lost during the operation. Check saved invoices before creating a different invoice."
            }
        }
    }
    onSelectedInvoiceChanged: loadQr()
    function select(id) { selectedId = id }
    function apply(raw) {
        if (!raw || raw === "{}") return
        let value
        try { value = JSON.parse(String(raw)) } catch (e) { error = "Invalid invoice response."; stale = true; return }
        if (typeof value.ok !== "boolean" || !Array.isArray(value.invoices)) {
            error = value.error || "Invoice storage is unavailable."
            storageAvailable = false; stale = true; reading = false; readTimeout.stop(); return
        }
        if (typeof value.updated_at === "number" && value.updated_at < updatedAt) return
        if (!value.invoices.every(function(row) {
            return row && typeof row.id === "string" && typeof row.amount_due_xmr === "string"
                && typeof row.received_xmr === "string" && typeof row.confirmed_xmr === "string"
                && typeof row.remaining_xmr === "string" && typeof row.overpaid_xmr === "string"
                && typeof row.payment_uri === "string" && typeof row.creation_error === "string"
                && Array.isArray(row.payments) && typeof row.creation_state === "string" && typeof row.status === "string"
        })) { error = "Invalid invoice records."; stale = true; return }
        const nextWallet = value.wallet || null
        const nextWalletId = nextWallet ? nextWallet.id : ""
        const walletChanged = (wallet ? wallet.id : "") !== nextWalletId
        const interrupted = walletChanged && mutating
        if (walletChanged) {
            // A read or creation reply from the old wallet cannot restore its
            // selection, address or QR after the user switches wallets.
            ++generation; ++mutationId
            mutating = false; mutationTimeout.stop()
            reading = false; readTimeout.stop()
            selectedId = ""; qr = null; qrForUri = ""
            lastRequest = null; notice = ""
        }
        wallet = nextWallet
        // Enforce the scope at the view boundary as well, including snapshots
        // received while the core/UI packages are being upgraded separately.
        invoices = nextWalletId ? value.invoices.filter(function(row) { return row.wallet_id === nextWalletId }) : []
        if ([1, 5, 10].indexOf(value.confirmation_policy) >= 0) {
            confirmationPolicy = value.confirmation_policy; policyLoaded = true
        } else { policyLoaded = false; policyError = "Confirmation policy is unavailable. Refresh to retry." }
        state = value.state || "starting"
        storageAvailable = value.ok === true
        stale = value.stale === true
        error = value.ok === false ? (value.error || "Invoice reconciliation failed.") : (value.monitor_error || "")
        if (interrupted) error = "Wallet changed during the operation. Any saved invoice remains with its original wallet."
        updatedAt = value.updated_at || updatedAt
        reading = false; readTimeout.stop()
        if (mutating && lastRequest && invoices.some(function(row) { return row.id === controller.lastRequest.id })) selectedId = lastRequest.id
        else if (!invoices.some(function(row) { return row.id === controller.selectedId })) selectedId = invoices.length ? invoices[0].id : ""
        if (walletChanged) walletContextChanged()
    }
    function read(method) {
        method = method || "getInvoices"
        if (!ready || !client || !watchReply || reading) return
        reading = true; readTimeout.restart()
        const token = generation
        try {
            watchReply(client[method](), function(raw) {
                if (!controller || token !== controller.generation || !controller.ready) return
                if (method === "getInvoices") { controller.reading = false; controller.readTimeout.stop(); controller.apply(raw) }
                else {
                    try { const reply = JSON.parse(String(raw)); if (reply.ok !== true) throw new Error(reply.error || "Refresh unavailable") }
                    catch (e) { controller.error = String(e); controller.reading = false; controller.readTimeout.stop() }
                }
            }, function(reason) {
                if (!controller || token !== controller.generation) return
                controller.error = String(reason); controller.stale = true; controller.reading = false; controller.readTimeout.stop()
            })
        } catch (e) { error = String(e); reading = false; readTimeout.stop() }
    }
    function setPolicy(confirmations) {
        if (!ready || !client || !watchReply || !storageAvailable || mutating || savingPolicy
            || [1, 5, 10].indexOf(confirmations) < 0 || confirmations === confirmationPolicy) return
        savingPolicy = true; policyError = ""; policyTimeout.restart()
        const token = ++policyRequest
        function failed(reason) {
            if (!controller || token !== controller.policyRequest || !controller.ready) return
            controller.savingPolicy = false; controller.policyTimeout.stop(); controller.policyLoaded = false
            controller.policyError = "Could not save confirmation policy: " + String(reason)
            controller.read()
        }
        try {
            watchReply(client.setConfirmationPolicy(confirmations), function(raw) {
                if (!controller || token !== controller.policyRequest || !controller.ready) return
                try {
                    const result = JSON.parse(String(raw))
                    if (result.ok !== true || [1, 5, 10].indexOf(result.confirmation_policy) < 0)
                        throw new Error(result.error || "Invalid policy reply")
                    controller.confirmationPolicy = result.confirmation_policy
                    controller.savingPolicy = false; controller.policyTimeout.stop()
                    controller.policyLoaded = false
                    controller.read()
                } catch (e) { failed(e) }
            }, failed)
        } catch (e) { failed(e) }
    }
    function validAmount(amount) { return /^\d+(\.\d{1,12})?$/.test(amount.trim()) && /[1-9]/.test(amount) }
    function create(title, amount) {
        if (!canCreate || !validAmount(amount)) return
        title = title.trim(); amount = amount.trim()
        if (!lastRequest || lastRequest.title !== title || lastRequest.amount !== amount) {
            lastRequest = {id: "inv-" + Date.now().toString(36) + "-" + Math.random().toString(36).slice(2) + Math.random().toString(36).slice(2), title: title, amount: amount}
        }
        const request = lastRequest
        mutate(function() { return client.createInvoice(request.id, request.title, request.amount) }, function(reply) {
            if (!reply.invoice || reply.invoice.id !== request.id || reply.invoice.creation_state !== "ready")
                throw new Error("The invoice result could not be verified. Check saved invoices before retrying.")
            controller.selectedId = reply.invoice.id
            controller.lastRequest = null
            controller.notice = "Invoice ready. Its receiving address is saved."
            controller.invoiceCreated()
            controller.read()
        })
    }
    function recover() {
        if (!selectedInvoice || !ready || mutating) return
        const id = selectedInvoice.id
        mutate(function() { return client.recoverInvoice(id) }, function(reply) {
            controller.selectedId = id; controller.lastRequest = null
            controller.notice = "Receiving address recovered. No new address was created."
            controller.read()
        })
    }
    function acknowledge() {
        if (!selectedInvoice || !ready || mutating) return
        const id = selectedInvoice.id
        mutate(function() { return client.acknowledgeInvoiceReview(id) }, function(reply) {
            controller.notice = "Review recorded. Payment status still follows the wallet."
            controller.read()
        })
    }
    function mutate(invoke, done) {
        if (mutating) return
        mutating = true; error = ""; notice = ""; mutationTimeout.restart()
        const token = ++mutationId
        try {
            watchReply(invoke(), function(raw) {
                if (!controller || token !== controller.mutationId || !controller.ready) return
                controller.mutating = false; controller.mutationTimeout.stop()
                try {
                    const reply = JSON.parse(String(raw))
                    if (reply.ok !== true) {
                        if (reply.invoice_id) controller.selectedId = reply.invoice_id
                        throw new Error(reply.error || "Invoice operation failed")
                    }
                    done(reply)
                } catch (e) { controller.error = String(e); controller.read() }
            }, function(reason) {
                if (!controller || token !== controller.mutationId) return
                controller.mutating = false; controller.mutationTimeout.stop()
                controller.error = "Reply unavailable: " + String(reason) + ". Refresh invoices or retry the same request."
            })
        } catch (e) { mutating = false; mutationTimeout.stop(); error = String(e) }
    }
    function loadQr() {
        const uri = selectedInvoice && selectedInvoice.creation_state === "ready" ? selectedInvoice.payment_uri : ""
        if (uri === qrForUri && qr !== null) return
        qr = null; qrForUri = uri
        if (!ready || !uri || !client || !watchReply) return
        try {
            watchReply(client.paymentQr(uri), function(raw) {
                if (!controller || uri !== controller.qrForUri || !controller.ready) return
                try {
                    const result = JSON.parse(String(raw))
                    if (result.ok !== true || !Number.isInteger(result.size) || result.size < 21 || result.size > 177
                        || !Array.isArray(result.runs) || !result.runs.every(function(run) {
                            return Array.isArray(run) && run.length === 3 && run.every(Number.isInteger)
                                && run[0] >= 0 && run[1] >= 0 && run[2] > 0
                                && run[0] + run[2] <= result.size && run[1] < result.size
                        })) throw new Error("QR unavailable. Copy the receiving address and remaining amount below.")
                    controller.qr = result
                } catch (e) { controller.error = String(e) }
            }, function(reason) { if (controller && uri === controller.qrForUri) controller.error = "QR unavailable: " + reason })
        } catch (e) { error = "QR unavailable: " + e }
    }
}
