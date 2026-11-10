import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "." as MerchantStyle

Panel {
    id: root
    property var controller
    property var statusLabel
    property var statusColor
    readonly property var invoice: controller.selectedInvoice
    readonly property bool stale: controller.stale || (!!invoice && invoice.stale)
    property bool detailsExpanded: false
    property string detailsFor: ""
    onInvoiceChanged: {
        const id = invoice ? invoice.id : ""
        if (id !== detailsFor) { detailsExpanded = false; detailsFor = id }
    }
    MerchantText {
        text: root.invoice ? root.invoice.title || "Invoice " + root.invoice.id.slice(-8) : "Payment details"
        color: MerchantStyle.MerchantTheme.ink; font.pixelSize: 18; font.weight: Font.DemiBold
        Layout.fillWidth: true
    }
    MerchantText {
        objectName: "selectedInvoiceStatus"
        text: root.invoice ? root.statusLabel(root.invoice) : "Select an invoice to see its payment status."
        color: root.invoice ? root.statusColor(root.invoice) : MerchantStyle.MerchantTheme.secondary
        font.pixelSize: root.invoice ? 24 : 14; font.weight: Font.DemiBold
        Layout.fillWidth: true
    }
    Rectangle {
        visible: !!root.invoice && (root.stale || root.invoice.requires_review)
        Layout.fillWidth: true
        implicitHeight: attention.implicitHeight + 24
        radius: 10; color: "#18ffc23d"
        MerchantText {
            id: attention
            objectName: "invoiceAttention"
            anchors.fill: parent; anchors.margins: 12
            color: MerchantStyle.MerchantTheme.warning
            text: root.stale
                ? "Payment information is out of date. Reconnect the wallet and refresh before requesting another payment."
                : "Review required. A payment disappeared or its confirmation count decreased. Check the payment activity below."
        }
    }
    GridLayout {
        visible: !!root.invoice
        Layout.fillWidth: true
        columns: root.width < 460 ? 1 : 2
        columnSpacing: 24; rowSpacing: 16
        Repeater {
            model: root.invoice ? [
                {label: "Amount due", value: root.invoice.amount_due_xmr, name: "selectedInvoiceAmount", accent: false},
                {label: "Received", value: root.invoice.received_xmr, name: "invoiceReceived", accent: false},
                {label: "Remaining to pay", value: root.invoice.remaining_xmr, name: "invoiceRemaining", accent: true},
                {label: "Confirmed · " + root.invoice.required_confirmations + "+ confirmations", value: root.invoice.confirmed_xmr, name: "invoiceConfirmed", accent: false}
            ] : []
            ColumnLayout {
                required property var modelData
                Layout.fillWidth: true; spacing: 5
                MerchantText { text: modelData.label; font.pixelSize: 12; Layout.fillWidth: true }
                MerchantText {
                    objectName: modelData.name
                    text: modelData.value + " XMR"
                    font.family: MerchantStyle.MerchantTheme.monoFont; font.pixelSize: 16
                    color: modelData.accent ? MerchantStyle.MerchantTheme.accent : MerchantStyle.MerchantTheme.ink
                    Layout.fillWidth: true; wrapMode: Text.WrapAnywhere
                }
            }
        }
    }
    MerchantText {
        visible: !!root.invoice && root.invoice.overpaid_xmr !== "0.000000000000"
        text: root.invoice ? "Overpayment: " + root.invoice.overpaid_xmr + " XMR" : ""
        color: MerchantStyle.MerchantTheme.warning; Layout.fillWidth: true
    }
    MerchantText {
        visible: !!root.invoice && root.invoice.creation_error.length > 0
        text: root.invoice ? root.invoice.creation_error : ""
        color: MerchantStyle.MerchantTheme.danger; Layout.fillWidth: true
    }
    MerchantText {
        visible: !!root.invoice && root.invoice.creation_state !== "ready"
        text: "Address assignment was interrupted. Recovery checks the original wallet for this invoice’s address without creating another one."
        Layout.fillWidth: true
    }
    ActionButton {
        visible: !!root.invoice && root.invoice.creation_state !== "ready"
        text: "Recover receiving address"; enabled: root.controller.ready && !root.controller.mutating
        onClicked: root.controller.recover()
    }
    MerchantText {
        visible: !!root.invoice && root.invoice.requires_review && root.stale
        text: "This invoice also needs review: a payment disappeared or its confirmation count decreased."
        color: MerchantStyle.MerchantTheme.warning; Layout.fillWidth: true
    }
    ActionButton {
        visible: !!root.invoice && root.invoice.requires_review
        text: "Mark reviewed"; enabled: root.controller.ready && !root.controller.mutating
        onClicked: root.controller.acknowledge()
    }
    Rectangle {
        id: qrBox
        objectName: "invoiceQr"
        readonly property int quiet: 4
        readonly property int cell: root.controller.qr ? Math.max(1, Math.floor(Math.min(260, root.width - 40) / (root.controller.qr.size + quiet * 2))) : 0
        visible: root.controller.ready && !root.stale && !!root.controller.qr && !!root.invoice
            && root.invoice.creation_state === "ready" && root.invoice.payment_uri.length > 0
        Layout.preferredWidth: root.controller.qr ? cell * (root.controller.qr.size + quiet * 2) : 0
        Layout.preferredHeight: Layout.preferredWidth
        Layout.alignment: Qt.AlignHCenter; Layout.topMargin: 8
        color: "white"
        Repeater {
            model: root.controller.qr ? root.controller.qr.runs : []
            Rectangle {
                required property var modelData
                x: (modelData[0] + qrBox.quiet) * qrBox.cell; y: (modelData[1] + qrBox.quiet) * qrBox.cell
                width: modelData[2] * qrBox.cell; height: qrBox.cell; color: "black"
            }
        }
    }
    MerchantText {
        visible: qrBox.visible
        text: "Scan to pay the remaining amount · Stagenet"
        Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; font.pixelSize: 12
    }
    MerchantText {
        visible: !!root.invoice && root.invoice.creation_state === "ready" && root.invoice.status === "accepted"
        text: root.invoice ? "Full amount received. Waiting for payments to reach " + root.invoice.required_confirmations + (root.invoice.required_confirmations === 1 ? " confirmation." : " confirmations.") : ""
        Layout.fillWidth: true
    }
    MerchantText {
        visible: !!root.invoice && root.invoice.status === "paid"
        text: "The invoice has met its confirmation policy. Funds may still be locked in the wallet."
        Layout.fillWidth: true
    }
    Flow {
        visible: !!root.invoice && root.invoice.creation_state === "ready"
        Layout.fillWidth: true; spacing: 6
        ActionButton {
            text: "Copy address"
            onClicked: { addressText.selectAll(); addressText.copy(); addressText.deselect(); root.controller.notice = "Receiving address copied." }
        }
        ActionButton {
            text: "Copy payment URI"
            visible: !root.stale && !!root.invoice && root.invoice.payment_uri.length > 0
            onClicked: { uriText.selectAll(); uriText.copy(); uriText.deselect(); root.controller.notice = "Payment URI copied." }
        }
    }
    TextEdit { id: uriText; visible: false; readOnly: true; textFormat: TextEdit.PlainText; text: root.invoice ? root.invoice.payment_uri : "" }
    MerchantText {
        visible: !!root.invoice
        text: "Payment activity"; font.pixelSize: 16; font.weight: Font.DemiBold
        color: MerchantStyle.MerchantTheme.ink; Layout.topMargin: 12
    }
    MerchantText { visible: !!root.invoice && root.invoice.payments.length === 0; text: "No payments detected yet." }
    Repeater {
        model: root.invoice ? root.invoice.payments : []
        RowLayout {
            required property var modelData
            Layout.fillWidth: true; spacing: 10
            Rectangle { width: 7; height: 7; radius: 4; color: modelData.present ? MerchantStyle.MerchantTheme.success : MerchantStyle.MerchantTheme.warning }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 4
                MerchantText {
                    text: modelData.amount_xmr + " XMR"
                    font.family: MerchantStyle.MerchantTheme.monoFont
                    color: MerchantStyle.MerchantTheme.ink; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere
                }
                MerchantText {
                    text: modelData.present ? modelData.confirmations + (modelData.confirmations === 1 ? " confirmation" : " confirmations") : "Not currently reported by the wallet"
                    color: modelData.present ? MerchantStyle.MerchantTheme.secondary : MerchantStyle.MerchantTheme.warning
                    font.pixelSize: 12; Layout.fillWidth: true
                }
            }
        }
    }
    ActionButton {
        objectName: "toggleInvoiceDetails"
        visible: !!root.invoice
        text: root.detailsExpanded ? "Hide technical details ▴" : "Show technical details ▾"
        onClicked: root.detailsExpanded = !root.detailsExpanded
    }
    ColumnLayout {
        visible: !!root.invoice && root.detailsExpanded
        Layout.fillWidth: true; spacing: 10
        MerchantText { text: "Receiving address"; font.pixelSize: 12 }
        TextEdit {
            id: addressText; objectName: "invoiceAddress"
            Layout.fillWidth: true; readOnly: true; selectByMouse: true
            wrapMode: TextEdit.WrapAnywhere; textFormat: TextEdit.PlainText
            text: root.invoice ? root.invoice.receiving_address || "Not assigned" : ""
            color: MerchantStyle.MerchantTheme.ink; selectionColor: "#804120"
            font.family: MerchantStyle.MerchantTheme.monoFont; font.pixelSize: 12
        }
        MerchantText {
            text: root.invoice ? "Invoice " + root.invoice.id + "\nCreated " + new Date(root.invoice.created_at).toLocaleString()
                + "\nAccount / subaddress: " + root.invoice.account_index + " / " + (root.invoice.subaddress_index === null ? "unassigned" : root.invoice.subaddress_index) : ""
            Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; font.pixelSize: 12
        }
        MerchantText { visible: !!root.invoice && !!root.invoice.accepted_at; text: root.invoice && root.invoice.accepted_at ? "First fully received: " + new Date(root.invoice.accepted_at).toLocaleString() : ""; Layout.fillWidth: true; font.pixelSize: 12 }
        MerchantText { visible: !!root.invoice && !!root.invoice.paid_at; text: root.invoice && root.invoice.paid_at ? "First met paid policy: " + new Date(root.invoice.paid_at).toLocaleString() : ""; Layout.fillWidth: true; font.pixelSize: 12 }
        Repeater {
            model: root.invoice ? root.invoice.payments : []
            ColumnLayout {
                required property var modelData
                Layout.fillWidth: true; spacing: 4
                MerchantText { text: "Transaction · " + modelData.amount_xmr + " XMR"; font.pixelSize: 12 }
                TextEdit {
                    text: modelData.txid; readOnly: true; selectByMouse: true; textFormat: TextEdit.PlainText
                    color: MerchantStyle.MerchantTheme.secondary; selectionColor: "#804120"
                    font.family: MerchantStyle.MerchantTheme.monoFont; font.pixelSize: 11
                    Layout.fillWidth: true; wrapMode: TextEdit.WrapAnywhere
                }
            }
        }
    }
}
