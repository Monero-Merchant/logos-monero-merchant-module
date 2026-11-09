import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

import "." as MerchantStyle
Pane {
    id: root
    padding: 0
    background: Rectangle { color: MerchantStyle.MerchantTheme.background }
    // Explicit roles also keep Basic controls dark under a light host theme.
    palette.window: MerchantStyle.MerchantTheme.background
    palette.windowText: MerchantStyle.MerchantTheme.ink
    palette.base: MerchantStyle.MerchantTheme.background
    palette.alternateBase: MerchantStyle.MerchantTheme.surface
    palette.text: MerchantStyle.MerchantTheme.ink
    palette.button: MerchantStyle.MerchantTheme.raised
    palette.buttonText: MerchantStyle.MerchantTheme.ink
    palette.highlight: MerchantStyle.MerchantTheme.accent
    palette.highlightedText: MerchantStyle.MerchantTheme.background
    palette.placeholderText: MerchantStyle.MerchantTheme.muted
    palette.mid: MerchantStyle.MerchantTheme.secondary
    palette.dark: MerchantStyle.MerchantTheme.surface
    palette.light: MerchantStyle.MerchantTheme.raised
    property alias client: controller.client
    property alias ready: controller.ready
    property alias watchReply: controller.watchReply
    property alias invoiceController: controller
    readonly property int gutter: width < 720 ? 14 : 28
    property alias walletConnection: connection
    readonly property var selected: controller.selectedInvoice
    function label(invoice) {
        if (invoice.creation_state !== "ready") return "Address recovery required"
        return invoice.status === "paid" ? "Paid" : invoice.status === "accepted" ? "Accepted"
            : invoice.status === "partial" ? "Partially paid" : "Pending"
    }
    function tint(invoice) { return invoice.status === "paid" ? MerchantStyle.MerchantTheme.success : invoice.status === "accepted" ? "#7aa2f7" : MerchantStyle.MerchantTheme.warning }
    WalletConnection {
        id: connection
        client: root.client; ready: root.ready; watchReply: root.watchReply
    }
    InvoiceController {
        id: controller
        onWalletContextChanged: connection.reset()
    }
    Connections {
        target: root.client; ignoreUnknownSignals: true
        function onInvoicesChanged() { controller.apply(root.client.invoices) }
    }
    onClientChanged: if (client && client.invoices) controller.apply(client.invoices)
    ScrollView {
        id: scroll
        anchors.fill: parent; clip: true; contentWidth: availableWidth
        ColumnLayout {
            width: Math.max(0, Math.min(1200, scroll.availableWidth - root.gutter * 2))
            x: (scroll.availableWidth - width) / 2; spacing: 16
            Item { Layout.preferredHeight: 12 }
            RowLayout {
                Layout.fillWidth: true
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 5
                    Text { font.family: MerchantStyle.MerchantTheme.uiFont; text: "Monero Merchant"; color: MerchantStyle.MerchantTheme.ink; font.pixelSize: 28; font.weight: Font.DemiBold }
                    Text { font.family: MerchantStyle.MerchantTheme.uiFont;
                        Layout.fillWidth: true; color: MerchantStyle.MerchantTheme.secondary; font.pixelSize: 12; wrapMode: Text.WordWrap; textFormat: Text.PlainText
                        text: "Wallet connection, invoices and payment tracking."
                    }
                }
                Rectangle {
                    implicitWidth: networkLabel.implicitWidth + 24; implicitHeight: 30; radius: 15
                    color: "#24ff6a2b"; border.color: "#40ff6a2b"
                    Text { id: networkLabel; anchors.centerIn: parent; text: "STAGENET"; color: MerchantStyle.MerchantTheme.accent; font.family: MerchantStyle.MerchantTheme.monoFont; font.pixelSize: 10; font.letterSpacing: 1 }
                }
            }
            GridLayout {
                Layout.fillWidth: true; columns: root.width < 960 ? 1 : 2; columnSpacing: 16; rowSpacing: 16
                Panel {
                    objectName: "walletConnectionCard"
                    Layout.fillWidth: true; Layout.preferredWidth: 800; Layout.alignment: Qt.AlignTop
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 5
                            Text { text: "WALLET CONNECTION"; font.family: MerchantStyle.MerchantTheme.uiFont; font.pixelSize: 10; font.letterSpacing: 1.5; color: MerchantStyle.MerchantTheme.secondary }
                            RowLayout {
                                Layout.fillWidth: true; spacing: 8
                                Rectangle { width: 7; height: 7; radius: 4; color: connection.canReceive ? MerchantStyle.MerchantTheme.success : connection.backendConnected ? MerchantStyle.MerchantTheme.warning : MerchantStyle.MerchantTheme.danger }
                                Text { objectName: "walletConnectionSummary"; text: connection.summary; font.family: MerchantStyle.MerchantTheme.uiFont; font.pixelSize: 16; font.weight: Font.DemiBold; color: MerchantStyle.MerchantTheme.ink; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                            }
                        }
                        ActionButton { objectName: "checkConnection"; text: connection.checking ? "Checking…" : "Check connection"; enabled: root.ready && !connection.checking; onClicked: connection.refresh() }
                    }
                    GridLayout {
                        columns: root.width < 620 ? 2 : 4; columnSpacing: 20; rowSpacing: 14; Layout.fillWidth: true
                        Repeater {
                            model: [
                                {label: "Wallet backend", value: connection.backendConnected ? "Connected" : connection.checking ? "Checking…" : "Unavailable", good: connection.backendConnected},
                                {label: "Open wallet", value: connection.status && connection.walletOpen ? connection.status.wallet : "No wallet open", good: connection.walletOpen},
                                {label: "Node", value: !connection.walletOpen ? "—" : connection.nodeConnected ? "Connected" : "Disconnected", good: connection.nodeConnected},
                                {label: "Synchronization", value: !connection.walletOpen ? "—" : connection.synced ? "Synchronized" : "Syncing…", good: connection.synced}
                            ]
                            ColumnLayout {
                                required property var modelData
                                Layout.fillWidth: true; Layout.preferredWidth: 170; spacing: 5
                                Text { text: modelData.label; font.family: MerchantStyle.MerchantTheme.uiFont; color: MerchantStyle.MerchantTheme.secondary; font.pixelSize: 11 }
                                Text { text: modelData.value; font.family: MerchantStyle.MerchantTheme.uiFont; color: modelData.good ? MerchantStyle.MerchantTheme.ink : MerchantStyle.MerchantTheme.muted; font.pixelSize: 13; textFormat: Text.PlainText; Layout.fillWidth: true; elide: Text.ElideRight }
                            }
                        }
                    }
                    Text { objectName: "walletConnectionError"; visible: text.length > 0; text: connection.error; font.family: MerchantStyle.MerchantTheme.uiFont; color: MerchantStyle.MerchantTheme.danger; font.pixelSize: 12; textFormat: Text.PlainText; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    Text { visible: connection.backendConnected && !connection.walletOpen; text: "Open your Stagenet wallet in the Monero wallet app. Its connection will appear here automatically."; font.family: MerchantStyle.MerchantTheme.uiFont; color: MerchantStyle.MerchantTheme.secondary; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                }
                Panel {
                    objectName: "confirmationPolicyCard"
                    Layout.fillWidth: true; Layout.preferredWidth: 250; Layout.alignment: Qt.AlignTop
                    Text { text: "CONFIRMATION POLICY"; font.family: MerchantStyle.MerchantTheme.uiFont; font.pixelSize: 10; font.letterSpacing: 1.2; color: MerchantStyle.MerchantTheme.secondary }
                    PolicySelector {
                        objectName: "confirmationPolicySelector"
                        Layout.fillWidth: true
                        savedPolicy: controller.confirmationPolicy
                        saving: controller.savingPolicy
                        enabled: root.ready && controller.storageAvailable && controller.policyLoaded && !controller.savingPolicy && !controller.mutating
                        onPolicySelected: function(value) { controller.setPolicy(value) }
                    }
                    Text { text: "New invoices only. Existing invoices keep their policy."; font.family: MerchantStyle.MerchantTheme.uiFont; color: MerchantStyle.MerchantTheme.secondary; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    Text { objectName: "confirmationPolicyError"; visible: text.length > 0; text: controller.policyError; font.family: MerchantStyle.MerchantTheme.uiFont; color: MerchantStyle.MerchantTheme.danger; font.pixelSize: 11; textFormat: Text.PlainText; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                }
            }
            PairingCard { Layout.fillWidth: true; client: root.client; ready: root.ready; watchReply: root.watchReply }
            Text { font.family: MerchantStyle.MerchantTheme.uiFont; visible: controller.stale && controller.invoices.length > 0; text: "Showing saved invoices · waiting for a current, synchronized wallet snapshot"; color: MerchantStyle.MerchantTheme.warning; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            Text { font.family: MerchantStyle.MerchantTheme.uiFont; objectName: "invoiceError"; visible: text.length > 0; text: controller.error; color: MerchantStyle.MerchantTheme.danger; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText }
            Text { font.family: MerchantStyle.MerchantTheme.uiFont; visible: text.length > 0; text: controller.notice; color: MerchantStyle.MerchantTheme.success; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            GridLayout {
                Layout.fillWidth: true; columns: root.width < 880 ? 1 : 2; columnSpacing: 18; rowSpacing: 18
                Rectangle {
                    Layout.fillWidth: true; Layout.preferredWidth: 380; Layout.preferredHeight: root.width < 880 ? 270 : 620
                    Layout.alignment: Qt.AlignTop
                    color: MerchantStyle.MerchantTheme.surface; radius: 18; border.color: MerchantStyle.MerchantTheme.line
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 12; spacing: 12
                        RowLayout {
                            Layout.fillWidth: true
                            Text { font.family: MerchantStyle.MerchantTheme.uiFont; text: "INVOICES"; color: MerchantStyle.MerchantTheme.secondary; font.pixelSize: 10; font.letterSpacing: 1.5 }
                            Item { Layout.fillWidth: true }
                            Text { font.family: MerchantStyle.MerchantTheme.monoFont; text: String(controller.invoices.length); color: MerchantStyle.MerchantTheme.secondary; font.pixelSize: 12 }
                            ActionButton { objectName: "refreshInvoices"; text: controller.reading ? "Refreshing…" : "Refresh"; implicitHeight: 32; leftPadding: 10; rightPadding: 10; enabled: root.ready && !controller.reading; onClicked: { controller.read("refreshInvoices"); connection.refresh() } }
                        }
                        ListView {
                            objectName: "invoiceList"; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 5
                            model: controller.invoices; boundsBehavior: Flickable.StopAtBounds; ScrollBar.vertical: ScrollBar {}
                            delegate: ItemDelegate {
                                required property var modelData
                                required property int index
                                objectName: "invoiceRow" + index; width: ListView.view.width; height: 104
                                onClicked: controller.select(modelData.id)
                                background: Rectangle { color: controller.selectedId === modelData.id ? "#24ff6a2b" : parent.hovered ? MerchantStyle.MerchantTheme.raised : "transparent"; radius: 8 }
                                contentItem: ColumnLayout {
                                    spacing: 6
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Text { font.family: MerchantStyle.MerchantTheme.uiFont; Layout.fillWidth: true; text: modelData.title || "Invoice " + modelData.id.slice(-8); textFormat: Text.PlainText; elide: Text.ElideRight; color: MerchantStyle.MerchantTheme.ink; font.pixelSize: 13; font.weight: Font.DemiBold }
                                        Text { font.family: MerchantStyle.MerchantTheme.uiFont; text: root.label(modelData); color: root.tint(modelData); font.pixelSize: 11 }
                                    }
                                    Text { font.family: MerchantStyle.MerchantTheme.monoFont; text: modelData.amount_due_xmr + " XMR"; color: MerchantStyle.MerchantTheme.ink; font.pixelSize: 17; font.weight: Font.Medium }
                                    Text { font.family: MerchantStyle.MerchantTheme.uiFont; Layout.fillWidth: true; elide: Text.ElideRight; textFormat: Text.PlainText; text: modelData.wallet_name + " · " + (modelData.subaddress_index === null ? "Address not assigned" : "Subaddress " + modelData.subaddress_index) + (modelData.requires_review ? " · Review required" : ""); color: modelData.requires_review ? MerchantStyle.MerchantTheme.warning : MerchantStyle.MerchantTheme.secondary; font.pixelSize: 11 }
                                }
                            }
                            Text { font.family: MerchantStyle.MerchantTheme.uiFont;
                                objectName: "invoiceEmptyState"
                                visible: controller.invoices.length === 0; anchors.centerIn: parent; width: parent.width - 24
                                text: !controller.wallet ? "Open a Stagenet wallet to view its saved invoices."
                                    : controller.state === "loading" ? "Loading this wallet’s invoices…"
                                    : "No invoices for this wallet. Create an order on your paired POS to track its payments."
                                color: MerchantStyle.MerchantTheme.secondary; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter; font.pixelSize: 13
                            }
                        }
                    }
                }
                Panel {
                    Layout.fillWidth: true; Layout.preferredWidth: 500; Layout.alignment: Qt.AlignTop
                    Text { font.family: MerchantStyle.MerchantTheme.uiFont; text: "PAYMENT DETAILS"; color: MerchantStyle.MerchantTheme.secondary; font.pixelSize: 10; font.letterSpacing: 1.5 }
                    Text { font.family: MerchantStyle.MerchantTheme.uiFont; objectName: "selectedInvoiceStatus"; text: root.selected ? root.label(root.selected) : "Select an invoice"; color: root.selected ? root.tint(root.selected) : MerchantStyle.MerchantTheme.secondary; font.pixelSize: 23; font.weight: Font.DemiBold; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    Text { font.family: MerchantStyle.MerchantTheme.uiFont; visible: !!root.selected; text: root.selected ? root.selected.title || root.selected.id : ""; textFormat: Text.PlainText; color: MerchantStyle.MerchantTheme.secondary; font.pixelSize: 13; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    Text { font.family: MerchantStyle.MerchantTheme.monoFont; visible: !!root.selected; objectName: "selectedInvoiceAmount"; text: root.selected ? root.selected.amount_due_xmr + " XMR" : ""; color: MerchantStyle.MerchantTheme.accent; font.pixelSize: 25; font.weight: Font.Medium; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
                    Text { font.family: MerchantStyle.MerchantTheme.uiFont; visible: !!root.selected && root.selected.stale; text: "Last observed status · this invoice is awaiting reconciliation with its wallet"; color: MerchantStyle.MerchantTheme.warning; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    GridLayout {
                        visible: !!root.selected; columns: 2; Layout.fillWidth: true; columnSpacing: 18; rowSpacing: 12
                        ColumnLayout { Layout.fillWidth: true; spacing: 4
                            Text { font.family: MerchantStyle.MerchantTheme.uiFont; text: "Received"; color: MerchantStyle.MerchantTheme.muted; font.pixelSize: 11 }
                            Text { font.family: MerchantStyle.MerchantTheme.monoFont; objectName: "invoiceReceived"; text: root.selected ? root.selected.received_xmr + " XMR" : ""; color: MerchantStyle.MerchantTheme.ink; font.pixelSize: 12 }
                        }
                        ColumnLayout { Layout.fillWidth: true; spacing: 4
                            Text { font.family: MerchantStyle.MerchantTheme.uiFont; text: root.selected ? root.selected.required_confirmations === 0 ? "Detected (0 confirmations)" : "At " + root.selected.required_confirmations + "+ confirmations" : "Meeting policy"; color: MerchantStyle.MerchantTheme.muted; font.pixelSize: 11 }
                            Text { font.family: MerchantStyle.MerchantTheme.monoFont; objectName: "invoiceConfirmed"; text: root.selected ? root.selected.confirmed_xmr + " XMR" : ""; color: MerchantStyle.MerchantTheme.ink; font.pixelSize: 12 }
                        }
                        ColumnLayout { Layout.fillWidth: true; spacing: 4
                            Text { font.family: MerchantStyle.MerchantTheme.uiFont; text: "Remaining"; color: MerchantStyle.MerchantTheme.muted; font.pixelSize: 11 }
                            Text { font.family: MerchantStyle.MerchantTheme.monoFont; text: root.selected ? root.selected.remaining_xmr + " XMR" : ""; color: MerchantStyle.MerchantTheme.ink; font.pixelSize: 12 }
                        }
                        ColumnLayout { Layout.fillWidth: true; spacing: 4
                            Text { font.family: MerchantStyle.MerchantTheme.uiFont; text: "Account / subaddress"; color: MerchantStyle.MerchantTheme.muted; font.pixelSize: 11 }
                            Text { font.family: MerchantStyle.MerchantTheme.uiFont; text: root.selected ? root.selected.account_index + " / " + (root.selected.subaddress_index === null ? "unassigned" : root.selected.subaddress_index) : ""; color: MerchantStyle.MerchantTheme.ink; font.pixelSize: 12 }
                        }
                    }
                    Text { font.family: MerchantStyle.MerchantTheme.monoFont; visible: !!root.selected && root.selected.overpaid_xmr !== "0.000000000000"; text: root.selected ? "Overpayment: " + root.selected.overpaid_xmr + " XMR" : ""; color: MerchantStyle.MerchantTheme.warning; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    Text { font.family: MerchantStyle.MerchantTheme.uiFont; visible: !!root.selected && root.selected.creation_error.length > 0; text: root.selected ? root.selected.creation_error : ""; color: MerchantStyle.MerchantTheme.danger; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.PlainText }
                    ActionButton { visible: !!root.selected && root.selected.creation_state !== "ready"; text: "Recover receiving address"; enabled: root.ready && !controller.mutating; onClicked: controller.recover() }
                    Text { font.family: MerchantStyle.MerchantTheme.uiFont; visible: !!root.selected && root.selected.requires_review; text: "Review required: a receipt disappeared or its confirmation count decreased. Saved payment history is retained."; color: MerchantStyle.MerchantTheme.warning; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    ActionButton { visible: !!root.selected && root.selected.requires_review; text: "Mark reviewed"; enabled: root.ready && !controller.mutating; onClicked: controller.acknowledge() }
                    Rectangle {
                        id: qrBox
                        objectName: "invoiceQr"
                        readonly property int quiet: 4
                        readonly property int cell: controller.qr ? Math.max(1, Math.floor(220 / (controller.qr.size + quiet * 2))) : 0
                        visible: root.ready && !!controller.qr && !!root.selected && root.selected.creation_state === "ready" && root.selected.payment_uri.length > 0
                        Layout.preferredWidth: controller.qr ? cell * (controller.qr.size + quiet * 2) : 0
                        Layout.preferredHeight: Layout.preferredWidth
                        color: "#ffffff"; Layout.alignment: Qt.AlignHCenter
                        Repeater {
                            model: controller.qr ? controller.qr.runs : []
                            Rectangle {
                                required property var modelData
                                x: (modelData[0] + qrBox.quiet) * qrBox.cell; y: (modelData[1] + qrBox.quiet) * qrBox.cell
                                width: modelData[2] * qrBox.cell; height: qrBox.cell; color: "#000000"
                            }
                        }
                    }
                    Text { font.family: MerchantStyle.MerchantTheme.uiFont; visible: qrBox.visible; text: "Scan to pay the remaining amount · Stagenet"; color: MerchantStyle.MerchantTheme.secondary; font.pixelSize: 11; Layout.alignment: Qt.AlignHCenter }
                    Text { font.family: MerchantStyle.MerchantTheme.uiFont; visible: !!root.selected && root.selected.creation_state === "ready" && root.selected.status === "accepted"; text: root.selected ? "Full amount detected. Waiting for enough funds to reach " + root.selected.required_confirmations + (root.selected.required_confirmations === 1 ? " confirmation." : " confirmations.") : ""; color: "#7aa2f7"; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    Text { font.family: MerchantStyle.MerchantTheme.uiFont; visible: !!root.selected && root.selected.creation_state === "ready"; text: "Receiving address"; color: MerchantStyle.MerchantTheme.muted; font.pixelSize: 11 }
                    TextEdit { id: addressText; objectName: "invoiceAddress"; visible: !!root.selected && root.selected.creation_state === "ready"; Layout.fillWidth: true; readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere; text: root.selected && root.selected.receiving_address ? root.selected.receiving_address : ""; color: MerchantStyle.MerchantTheme.ink; selectionColor: "#804120"; font.family: MerchantStyle.MerchantTheme.monoFont; font.pixelSize: 12 }
                    ActionButton { visible: !!root.selected && root.selected.creation_state === "ready"; text: "Copy address"; onClicked: { addressText.selectAll(); addressText.copy(); addressText.deselect(); controller.notice = "Receiving address copied." } }
                    TextEdit { visible: !!root.selected && root.selected.payment_uri.length > 0; Layout.fillWidth: true; readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere; text: root.selected ? root.selected.payment_uri : ""; color: MerchantStyle.MerchantTheme.secondary; selectionColor: "#804120"; font.family: MerchantStyle.MerchantTheme.monoFont; font.pixelSize: 10 }
                    Text { font.family: MerchantStyle.MerchantTheme.uiFont; visible: !!root.selected; text: "PAYMENT RECORDS"; color: MerchantStyle.MerchantTheme.secondary; font.pixelSize: 10; font.letterSpacing: 1.5; Layout.topMargin: 6 }
                    Text { font.family: MerchantStyle.MerchantTheme.uiFont; visible: !!root.selected && root.selected.payments.length === 0; text: "No matching payments yet."; color: MerchantStyle.MerchantTheme.secondary; font.pixelSize: 12 }
                    Repeater {
                        model: root.selected ? root.selected.payments : []
                        ColumnLayout {
                            required property var modelData
                            Layout.fillWidth: true; spacing: 5
                            Text { font.family: MerchantStyle.MerchantTheme.monoFont; text: modelData.amount_xmr + " XMR · " + modelData.confirmations + " confirmations" + (modelData.present ? "" : " · Not currently reported"); color: modelData.present ? MerchantStyle.MerchantTheme.ink : MerchantStyle.MerchantTheme.warning; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                            TextEdit { text: modelData.txid; readOnly: true; selectByMouse: true; color: MerchantStyle.MerchantTheme.secondary; font.family: MerchantStyle.MerchantTheme.monoFont; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: TextEdit.WrapAnywhere }
                        }
                    }
                    Text { font.family: MerchantStyle.MerchantTheme.uiFont; visible: !!root.selected; text: root.selected ? "Invoice " + root.selected.id + " · " + new Date(root.selected.created_at).toLocaleString() : ""; color: MerchantStyle.MerchantTheme.muted; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText }
                    Text { font.family: MerchantStyle.MerchantTheme.uiFont; visible: !!root.selected && !!root.selected.accepted_at; text: root.selected && root.selected.accepted_at ? "First fully received: " + new Date(root.selected.accepted_at).toLocaleString() : ""; color: MerchantStyle.MerchantTheme.muted; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    Text { font.family: MerchantStyle.MerchantTheme.uiFont; visible: !!root.selected && !!root.selected.paid_at; text: root.selected && root.selected.paid_at ? "First met paid policy: " + new Date(root.selected.paid_at).toLocaleString() : ""; color: MerchantStyle.MerchantTheme.muted; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    Text { font.family: MerchantStyle.MerchantTheme.uiFont; visible: !!root.selected; text: "Paid means the invoice has met its confirmation policy. It does not indicate whether funds are unlocked."; color: MerchantStyle.MerchantTheme.muted; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                }
            }
            Item { Layout.preferredHeight: 12 }
        }
    }
}
