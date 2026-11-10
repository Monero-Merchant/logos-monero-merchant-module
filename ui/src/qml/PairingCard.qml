import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "." as MerchantStyle
Panel {
    id: root
    objectName: "posPairingCard"
    property alias client: pairing.client
    property alias ready: pairing.ready
    property alias watchReply: pairing.watchReply
    property alias controller: pairing
    property var confirmation: null
    PairingController { id: pairing }
    RowLayout {
        Layout.fillWidth: true
        ColumnLayout {
            Layout.fillWidth: true
            Text { text: "PAIR POS"; color: MerchantStyle.MerchantTheme.secondary; font.family: MerchantStyle.MerchantTheme.uiFont; font.pixelSize: 10; font.letterSpacing: 1.5 }
            Text { text: "Connect your Android till"; color: MerchantStyle.MerchantTheme.ink; font.family: MerchantStyle.MerchantTheme.uiFont; font.pixelSize: 18; font.weight: Font.DemiBold; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        }
        ActionButton { objectName: "refreshPosPairing"; text: pairing.busy ? "Checking…" : "Refresh"; enabled: root.ready && pairing.supported && !pairing.busy; onClicked: pairing.refresh() }
    }
    Text {
        objectName: "posServiceStatus"; Layout.fillWidth: true; wrapMode: Text.WordWrap
        text: pairing.online ? "Pairing service connected" : !pairing.supported ? "Pairing controls will be available when the updated merchant UI is loaded." : "Pairing service offline"
        font.family: MerchantStyle.MerchantTheme.uiFont; font.pixelSize: 12
        color: pairing.online ? MerchantStyle.MerchantTheme.success : MerchantStyle.MerchantTheme.warning
    }
    Text { visible: pairing.error.length > 0; text: pairing.error; textFormat: Text.PlainText; color: MerchantStyle.MerchantTheme.danger; font.family: MerchantStyle.MerchantTheme.uiFont; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
    Text {
        visible: text.length > 0
        text: pairing.merchant && pairing.merchant.backend_source === "basecamp" ? "Merchant connection: Monero Merchant in Basecamp" : ""
        color: MerchantStyle.MerchantTheme.secondary; font.family: MerchantStyle.MerchantTheme.uiFont; font.pixelSize: 12
        Layout.fillWidth: true; wrapMode: Text.WordWrap
    }
    GridLayout {
        columns: root.width < 740 ? 1 : 2; columnSpacing: 24; rowSpacing: 18; Layout.fillWidth: true
        ColumnLayout {
            id: invitationColumn
            Layout.fillWidth: true; Layout.preferredWidth: 500; Layout.alignment: Qt.AlignTop; spacing: 10
            Text { text: "1 · Generate an invitation"; color: MerchantStyle.MerchantTheme.ink; font.family: MerchantStyle.MerchantTheme.uiFont; font.pixelSize: 14 }
            Text { text: "Pair securely over Logos Delivery. The terminal can use another Wi-Fi network or mobile data."; color: MerchantStyle.MerchantTheme.secondary; font.family: MerchantStyle.MerchantTheme.uiFont; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            ActionButton { objectName: "createPosInvitation"; text: "Generate pairing QR"; primary: true; enabled: pairing.online && !pairing.busy; onClicked: pairing.create() }
            Rectangle {
                id: qrBox; objectName: "posPairingQr"
                visible: pairing.qr !== null && pairing.remaining > 0
                readonly property int quiet: 4
                readonly property int cell: Math.max(1, Math.min(4, Math.floor(Math.max(100, Math.min(320, invitationColumn.width)) / ((pairing.qr ? pairing.qr.size : 21) + 8))))
                Layout.preferredWidth: pairing.qr ? (pairing.qr.size + quiet * 2) * cell : 0
                Layout.preferredHeight: Layout.preferredWidth
                color: "white"
                Repeater {
                    model: pairing.qr ? pairing.qr.runs : []
                    Rectangle {
                        required property var modelData
                        x: (modelData[0] + qrBox.quiet) * qrBox.cell; y: (modelData[1] + qrBox.quiet) * qrBox.cell
                        width: modelData[2] * qrBox.cell; height: qrBox.cell; color: "black"
                    }
                }
            }
            Text { visible: pairing.invitation.length > 0; text: "Scan in the POS app · expires in " + pairing.remaining + " seconds · single use"; color: MerchantStyle.MerchantTheme.secondary; font.family: MerchantStyle.MerchantTheme.uiFont; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            TextEdit { id: invitationText; visible: false; text: pairing.invitation; readOnly: true }
            ActionButton { text: "Copy invitation instead"; visible: pairing.invitation.length > 0; enabled: pairing.remaining > 0; onClicked: { invitationText.selectAll(); invitationText.copy(); invitationText.deselect() } }
        }
        ColumnLayout {
            Layout.fillWidth: true; Layout.preferredWidth: 500; Layout.alignment: Qt.AlignTop; spacing: 12
            Text { text: "2 · Approve your terminal"; color: MerchantStyle.MerchantTheme.ink; font.family: MerchantStyle.MerchantTheme.uiFont; font.pixelSize: 14 }
            Text { text: "After scanning, compare the full terminal identity with the identity shown on the phone before approving. Terminal names alone do not prove identity."; color: MerchantStyle.MerchantTheme.secondary; font.family: MerchantStyle.MerchantTheme.uiFont; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            Text { visible: pairing.devices.length === 0; text: "No terminals enrolled yet."; color: MerchantStyle.MerchantTheme.muted; font.family: MerchantStyle.MerchantTheme.uiFont; font.pixelSize: 12 }
            Repeater {
                model: pairing.devices
                ColumnLayout {
                    required property var modelData
                    Layout.fillWidth: true; spacing: 7
                    Text { text: modelData.name + " · " + modelData.status; textFormat: Text.PlainText; color: modelData.status === "approved" ? MerchantStyle.MerchantTheme.success : MerchantStyle.MerchantTheme.ink; font.family: MerchantStyle.MerchantTheme.uiFont; font.pixelSize: 13; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    TextEdit { text: modelData.peer_id; textFormat: TextEdit.PlainText; readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere; color: MerchantStyle.MerchantTheme.secondary; font.family: MerchantStyle.MerchantTheme.monoFont; font.pixelSize: 11; Layout.fillWidth: true }
                    RowLayout {
                        ActionButton { text: "Approve"; visible: modelData.status === "pending"; enabled: pairing.online && !pairing.busy; onClicked: { root.confirmation = {peer: modelData.peer_id, name: modelData.name, approve: true}; confirmDialog.open() } }
                        ActionButton { text: "Revoke"; visible: modelData.status !== "revoked"; enabled: pairing.online && !pairing.busy; onClicked: { root.confirmation = {peer: modelData.peer_id, name: modelData.name, approve: false}; confirmDialog.open() } }
                    }
                }
            }
        }
    }
    Text { visible: pairing.notice.length > 0; text: pairing.notice; textFormat: Text.PlainText; color: MerchantStyle.MerchantTheme.success; font.family: MerchantStyle.MerchantTheme.uiFont; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
    Dialog {
        id: confirmDialog
        modal: true; width: Math.min(500, root.width - 16); anchors.centerIn: parent
        title: root.confirmation && root.confirmation.approve ? "Approve terminal?" : "Revoke terminal?"
        background: Rectangle { color: MerchantStyle.MerchantTheme.surface; radius: 12; border.color: MerchantStyle.MerchantTheme.secondary }
        contentItem: ColumnLayout {
            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.PlainText; color: MerchantStyle.MerchantTheme.ink; text: root.confirmation ? root.confirmation.name : "" }
            Text { Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText; color: MerchantStyle.MerchantTheme.secondary; font.family: MerchantStyle.MerchantTheme.monoFont; text: root.confirmation ? root.confirmation.peer : "" }
            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: MerchantStyle.MerchantTheme.ink; text: root.confirmation && root.confirmation.approve ? "Confirm this full identity matches the terminal shown on your phone." : "This terminal will lose access on its next request." }
        }
        footer: RowLayout {
            Item { Layout.fillWidth: true }
            ActionButton { text: "Cancel"; onClicked: confirmDialog.close() }
            ActionButton { text: root.confirmation && root.confirmation.approve ? "Approve terminal" : "Revoke access"; enabled: pairing.online && !pairing.busy; onClicked: { const selected = root.confirmation; confirmDialog.close(); if (selected) pairing.authorize(selected.peer, selected.approve) } }
        }
    }
}
