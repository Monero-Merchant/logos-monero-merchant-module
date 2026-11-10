import QtQuick
import QtTest
import "../src/qml" as Merchant

TestCase {
    id: test
    name: "InvoiceFocusedDashboard"
    when: windowShown
    width: 1280
    height: 900
    visible: true
    property double clock: Date.now()
    property bool stale: false
    property bool openWallet: true
    property var records: []
    property var wallet: ({id: "stagenet:wallet-a", name: "Demo merchant", network: "stagenet", address: "5" + "A".repeat(94), connected: true, synchronized: true})
    function row(id, status, due, received, remaining) {
        return {id: id, title: id === "coffee" ? "Coffee and pastries" : id === "lunch" ? "Lunch order" : "Takeaway order", wallet_id: wallet.id, wallet_name: wallet.name, amount_due_xmr: due, received_xmr: received, confirmed_xmr: status === "paid" ? due : "0.000000000000", remaining_xmr: remaining, overpaid_xmr: "0.000000000000", payment_uri: remaining === "0.000000000000" ? "" : "monero:" + wallet.address + "?tx_amount=" + remaining, creation_error: "", creation_state: "ready", status: status, payments: status === "pending" ? [] : [{amount_xmr: received, confirmations: status === "paid" ? 6 : 2, present: true, txid: "a".repeat(64)}], receiving_address: wallet.address, account_index: 0, subaddress_index: 1, required_confirmations: 5, stale: stale, requires_review: false, created_at: clock - 600000, accepted_at: status === "paid" ? clock - 300000 : null, paid_at: status === "paid" ? clock - 120000 : null}
    }
    function snapshot() { return JSON.stringify({ok: true, confirmation_policy: 5, state: "ready", wallet: openWallet ? wallet : null, stale: stale, monitor_error: null, invoices: records, updated_at: ++clock}) }
    QtObject {
        id: fake
        property string invoices: ""
        property int policy: 5
        property int pairingReads: 0
        function getInvoices() { return test.snapshot() }
        function getWalletStatus() { return JSON.stringify({ok: true, wallet: test.openWallet ? "Demo merchant" : "", network: "stagenet", connected: true, synchronized: true}) }
        function refreshInvoices() { Qt.callLater(function(){fake.invoices = test.snapshot()}); return JSON.stringify({ok:true}) }
        function getPosPairing() { pairingReads++; return JSON.stringify({ok:true, ready:true, merchant:{backend_source:"basecamp"}, devices:[{peer_id:"a".repeat(64), name:"Counter till", status:"approved"}]}) }
        function paymentQr(uri) { let runs=[]; for(let y=0;y<29;y++) for(let x=0;x<29;x++) if((x+y)%3===0) runs.push([x,y,1]); return JSON.stringify({ok:true,size:29,runs:runs}) }
        function setConfirmationPolicy(value) { policy=value; return JSON.stringify({ok:true,confirmation_policy:value}) }
    }
    Merchant.Invoices {
        id: dashboard
        anchors.fill: parent
        client: fake
        ready: true
        watchReply: function(reply, success, failure) { Qt.callLater(function(){success(reply)}) }
    }
    TextEdit { id: clipboardSink; visible: false }
    function initTestCase() {
        records = [row("coffee","partial","0.015000000000","0.005000000000","0.010000000000"),row("lunch","paid","0.025000000000","0.025000000000","0.000000000000"),row("takeaway","pending","0.008000000000","0.000000000000","0.008000000000")]
        fake.invoices = snapshot()
        dashboard.invoiceController.apply(fake.invoices)
        dashboard.invoiceController.select("coffee")
        wait(400)
    }
    function init() {
        test.width = 1280
        test.stale = false
        test.openWallet = true
        dashboard.ready = true
        dashboard.settingsExpanded = false
        dashboard.terminalsExpanded = false
        button("invoiceSearch").text = ""
        button("invoiceFilter_all").clicked()
        records = [row("coffee","partial","0.015000000000","0.005000000000","0.010000000000"),row("lunch","paid","0.025000000000","0.025000000000","0.000000000000"),row("takeaway","pending","0.008000000000","0.000000000000","0.008000000000")]
        push()
        dashboard.invoiceController.select("coffee")
        button("dashboardScroll").contentItem.contentY = 0
        wait(80)
    }
    function push() { fake.invoices = snapshot(); wait(120) }
    function button(name) { const control = findChild(dashboard,name); verify(control !== null, name); return control }
    function test_invoice_dashboard_starts_with_management_collapsed() {
        compare(dashboard.invoiceController.invoices.length,3)
        compare(dashboard.invoiceController.selectedId,"coffee")
        verify(findChild(dashboard,"invoiceList") !== null)
        verify(!dashboard.settingsExpanded)
        verify(!dashboard.terminalsExpanded)
        verify(button("invoiceQr").visible)
    }
    function test_search_filters_and_paid_update_preserve_expected_selection() {
        button("invoiceFilter_paid").clicked()
        wait(80)
        compare(button("invoiceList").count,1)
        compare(dashboard.invoiceController.selectedId,"lunch")
        button("invoiceSearch").text = "coffee"
        wait(80)
        compare(button("invoiceList").count,0)
        button("invoiceSearch").text = ""
        button("invoiceFilter_active").clicked()
        wait(80)
        compare(button("invoiceList").count,2)
        dashboard.invoiceController.select("coffee")
        records = records.map(function(r) { let copy = Object.assign({},r); if(copy.id === "coffee") { copy.status="paid";copy.remaining_xmr="0.000000000000";copy.payment_uri="" }; return copy })
        push()
        compare(button("invoiceList").count,1)
        compare(dashboard.invoiceController.selectedId,"coffee")
        compare(button("selectedInvoiceStatus").text,"Paid")
        button("invoiceFilter_paid").clicked()
        wait(80)
        compare(button("invoiceList").count,2)
        verify(button("invoiceListCard").visibleInvoices.every(function(row) { return row.status === "paid" }))
        button("invoiceFilter_all").clicked()
        button("invoiceSearch").text = "lunch"
        wait(80)
        compare(button("invoiceList").count,1)
        compare(dashboard.invoiceController.selectedId,"lunch")
        button("invoiceSearch").text = "not-present"
        wait(80)
        compare(button("invoiceList").count,0)
        compare(dashboard.invoiceController.selectedId,"lunch")
        button("invoiceSearch").text = ""
        button("invoiceFilter_review").clicked()
        wait(80)
        compare(button("invoiceList").count,0)
        button("invoiceFilter_all").clicked()
        wait(80)
        records = [row("coffee","partial","0.015000000000","0.005000000000","0.010000000000"),row("lunch","paid","0.025000000000","0.025000000000","0.000000000000"),row("takeaway","pending","0.008000000000","0.000000000000","0.008000000000")]
        push()
        dashboard.invoiceController.select("coffee")
    }
    function test_management_sections_and_technical_details_expand() {
        dashboard.reveal("wallet")
        wait(180)
        verify(dashboard.settingsExpanded)
        const scroll = button("dashboardScroll")
        const headerY = button("toggleWalletSettings").mapToItem(dashboard,0,0).y
        verify(headerY < 120 || scroll.contentItem.contentY >= scroll.contentItem.contentHeight - scroll.availableHeight - 1)
        verify(button("confirmationPolicySelector").visible)
        button("toggleWalletSettings").clicked()
        verify(!dashboard.settingsExpanded)
        dashboard.reveal("terminals")
        wait(180)
        verify(dashboard.terminalsExpanded)
        verify(button("posPairingCard").visible)
        button("posPairingCard").controller.refresh()
        wait(100)
        verify(button("posPairingCard").controller.online)
        button("togglePosTerminals").clicked()
        verify(!dashboard.terminalsExpanded)
        button("dashboardScroll").contentItem.contentY = 0
        button("toggleInvoiceDetails").clicked()
        wait(100)
        verify(button("invoiceAddress").visible)
        button("toggleInvoiceDetails").clicked()
        verify(!button("invoiceAddress").visible)
    }
    function test_narrow_layout_keeps_cards_inside_viewport() {
        test.width=320
        button("dashboardScroll").contentItem.contentY = 0
        wait(250)
        const listCard = button("invoiceListCard")
        const detailCard = button("invoiceDetailsCard")
        verify(listCard.width <= dashboard.width)
        verify(detailCard.width <= dashboard.width)
        verify(detailCard.y > listCard.y)
        dashboard.reveal("wallet")
        wait(150)
        button("toggleWalletSettings").clicked()
        test.width=1280
        button("dashboardScroll").contentItem.contentY = 0
        wait(100)
    }
    function test_stale_snapshot_hides_payment_qr_and_shows_warning() {
        test.stale=true
        records=records.map(function(r){let copy=Object.assign({},r);copy.stale=true;return copy})
        push()
        verify(!button("invoiceQr").visible)
        verify(button("invoiceAttention").visible)
    }
    function findButtonByText(item, text) {
        if (item.text === text && typeof item.clicked === "function") return item
        for (let i = 0; i < item.children.length; ++i) {
            const found = findButtonByText(item.children[i], text)
            if (found) return found
        }
        return null
    }
    function test_copy_address_works_with_technical_details_collapsed() {
        const copy = findButtonByText(button("invoiceDetailsCard"), "Copy address")
        verify(copy !== null)
        verify(!button("invoiceAddress").visible)
        copy.clicked()
        clipboardSink.text = ""
        clipboardSink.paste()
        compare(clipboardSink.text, dashboard.invoiceController.selectedInvoice.receiving_address)
    }
    function test_empty_wallet_offline_and_recovery_states() {
        records = []
        test.openWallet = false
        push()
        dashboard.walletConnection.reset()
        wait(100)
        compare(button("invoiceList").count,0)
        verify(button("invoiceEmptyState").text.indexOf("Open a Stagenet wallet") >= 0)
        compare(dashboard.invoiceController.selectedId,"")
        verify(!button("invoiceQr").visible)
        test.openWallet = true
        let recovering = row("recovering","pending","0.010000000000","0.000000000000","0.010000000000")
        recovering.creation_state = "recovery_required"
        recovering.creation_error = "Address allocation interrupted"
        recovering.receiving_address = ""
        recovering.payment_uri = ""
        records = [recovering]
        push()
        compare(button("selectedInvoiceStatus").text,"Address recovery needed")
        verify(!button("invoiceQr").visible)
        let confirming = row("confirming","accepted","0.010000000000","0.010000000000","0.000000000000")
        records = [confirming]
        push()
        compare(button("selectedInvoiceStatus").text,"Confirming payment")
        verify(!button("invoiceQr").visible)
        dashboard.ready = false
        wait(100)
        verify(button("walletConnectionSummary").text.indexOf("unavailable") >= 0)
        verify(!button("invoiceQr").visible)
    }

}
