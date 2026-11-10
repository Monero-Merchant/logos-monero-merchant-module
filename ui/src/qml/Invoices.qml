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
    property bool settingsExpanded: false
    property bool terminalsExpanded: false
    readonly property int pendingTerminals: pairing.controller.devices.filter(function(row) { return row.status === "pending" }).length
    readonly property int approvedTerminals: pairing.controller.devices.filter(function(row) { return row.status === "approved" }).length
    function label(invoice) {
        if (invoice.creation_state !== "ready") return "Address recovery needed"
        return invoice.status === "paid" ? "Paid" : invoice.status === "accepted" ? "Confirming payment"
            : invoice.status === "partial" ? "Partially paid" : "Awaiting payment"
    }
    function tint(invoice) {
        return invoice.creation_state !== "ready" ? MerchantStyle.MerchantTheme.warning
            : invoice.status === "paid" ? MerchantStyle.MerchantTheme.success
            : invoice.status === "accepted" ? "#7aa2f7" : MerchantStyle.MerchantTheme.warning
    }
    function reveal(section) {
        if (section === "terminals") terminalsExpanded = true
        else settingsExpanded = true
        revealTimer.section = section
        revealTimer.restart()
    }
    Timer {
        id: revealTimer
        property string section: "wallet"
        // Wait for the expanded sections and ScrollView to complete layout.
        interval: 50
        onTriggered: {
            const item = section === "terminals" ? terminalSection : walletSection
            scroll.contentItem.contentY = Math.max(0, Math.min(item.y - 16, scroll.contentItem.contentHeight - scroll.availableHeight))
        }
    }
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
        objectName: "dashboardScroll"
        anchors.fill: parent; clip: true; contentWidth: availableWidth
        ColumnLayout {
            width: Math.max(0, Math.min(1200, scroll.availableWidth - root.gutter * 2))
            x: (scroll.availableWidth - width) / 2; spacing: 16
            Item { Layout.preferredHeight: 8 }
            RowLayout {
                Layout.fillWidth: true
                MerchantText {
                    text: "Monero Merchant"; color: MerchantStyle.MerchantTheme.ink
                    font.pixelSize: root.width < 500 ? 23 : 28; font.weight: Font.DemiBold
                    Layout.fillWidth: true
                }
                Rectangle {
                    implicitWidth: networkLabel.implicitWidth + 24; implicitHeight: 30; radius: 15
                    color: "#24ff6a2b"; border.color: "#40ff6a2b"
                    MerchantText { id: networkLabel; anchors.centerIn: parent; text: "STAGENET"; color: MerchantStyle.MerchantTheme.accent; font.family: MerchantStyle.MerchantTheme.monoFont; font.pixelSize: 10; font.letterSpacing: 1 }
                }
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 10
                Rectangle { width: 7; height: 7; radius: 4; color: connection.canReceive ? MerchantStyle.MerchantTheme.success : MerchantStyle.MerchantTheme.warning }
                MerchantText {
                    objectName: "walletConnectionSummary"
                    text: connection.summary + (connection.walletOpen && connection.status ? " · " + connection.status.wallet : "")
                    Layout.fillWidth: true
                }
            }
            Rectangle {
                visible: controller.stale && controller.invoices.length > 0
                Layout.fillWidth: true; implicitHeight: staleText.implicitHeight + 24
                color: "#18ffc23d"; radius: 10
                MerchantText {
                    id: staleText
                    anchors.fill: parent; anchors.margins: 12
                    text: "Showing saved payment information. Waiting for a connected, synchronized wallet."
                    color: MerchantStyle.MerchantTheme.warning
                }
            }
            MerchantText { objectName: "invoiceError"; visible: text.length > 0; text: controller.error; color: MerchantStyle.MerchantTheme.danger; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
            MerchantText { visible: text.length > 0; text: controller.notice; color: MerchantStyle.MerchantTheme.success; Layout.fillWidth: true }
            RowLayout {
                visible: root.pendingTerminals > 0
                Layout.fillWidth: true
                MerchantText { text: root.pendingTerminals + (root.pendingTerminals === 1 ? " terminal is awaiting approval" : " terminals are awaiting approval"); color: MerchantStyle.MerchantTheme.warning; Layout.fillWidth: true }
                ActionButton { text: "Review"; onClicked: root.reveal("terminals") }
            }
            MerchantText {
                Layout.fillWidth: true
                text: "Track payments"; color: MerchantStyle.MerchantTheme.ink
                font.pixelSize: 20; font.weight: Font.DemiBold
            }
            GridLayout {
                Layout.fillWidth: true
                columns: root.width < 880 ? 1 : 2
                columnSpacing: 18; rowSpacing: 18
                InvoiceList {
                    objectName: "invoiceListCard"
                    Layout.fillWidth: true; Layout.preferredWidth: 430; Layout.alignment: Qt.AlignTop
                    compact: root.width < 880
                    controller: root.invoiceController
                    statusLabel: root.label; statusColor: root.tint
                    onConnectTerminal: root.reveal("terminals")
                }
                InvoiceDetails {
                    objectName: "invoiceDetailsCard"
                    Layout.fillWidth: true; Layout.preferredWidth: 600; Layout.alignment: Qt.AlignTop
                    controller: root.invoiceController
                    statusLabel: root.label; statusColor: root.tint
                }
            }
            ColumnLayout {
                id: walletSection
                Layout.fillWidth: true; spacing: 12
                Panel {
                    Layout.fillWidth: true
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 4
                            MerchantText { text: "Wallet & settings"; color: MerchantStyle.MerchantTheme.ink; font.pixelSize: 16; font.weight: Font.DemiBold }
                            MerchantText {
                                text: controller.policyLoaded ? "New invoices require " + controller.confirmationPolicy + (controller.confirmationPolicy === 1 ? " confirmation" : " confirmations") : "Wallet connection and payment confirmation policy"
                                Layout.fillWidth: true; font.pixelSize: 12
                            }
                        }
                        ActionButton {
                            objectName: "toggleWalletSettings"
                            text: root.settingsExpanded ? "Hide ▴" : "Manage ▾"
                            onClicked: root.settingsExpanded = !root.settingsExpanded
                        }
                    }
                }
                WalletSettings {
                    visible: root.settingsExpanded
                    Layout.fillWidth: true
                    controller: root.invoiceController; connection: root.walletConnection
                }
            }
            ColumnLayout {
                id: terminalSection
                Layout.fillWidth: true; spacing: 12
                Panel {
                    Layout.fillWidth: true
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 4
                            MerchantText { text: "POS terminals"; color: MerchantStyle.MerchantTheme.ink; font.pixelSize: 16; font.weight: Font.DemiBold }
                            MerchantText {
                                text: root.pendingTerminals > 0 ? root.pendingTerminals + " awaiting approval"
                                    : !pairing.controller.online ? "Pairing service unavailable"
                                    : root.approvedTerminals > 0 ? root.approvedTerminals + (root.approvedTerminals === 1 ? " approved terminal" : " approved terminals")
                                    : "Connect your Android POS to create invoices"
                                color: root.pendingTerminals > 0 ? MerchantStyle.MerchantTheme.warning : MerchantStyle.MerchantTheme.secondary
                                Layout.fillWidth: true; font.pixelSize: 12
                            }
                        }
                        ActionButton {
                            objectName: "togglePosTerminals"
                            text: root.terminalsExpanded ? "Hide ▴" : "Manage ▾"
                            onClicked: root.terminalsExpanded = !root.terminalsExpanded
                        }
                    }
                }
                PairingCard {
                    id: pairing
                    visible: root.terminalsExpanded
                    Layout.fillWidth: true
                    client: root.client; ready: root.ready; watchReply: root.watchReply
                }
            }
            Item { Layout.preferredHeight: 12 }
        }
    }
}
