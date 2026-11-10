import QtQuick
import QtQuick.Layouts
import "." as MerchantStyle

GridLayout {
    id: root
    property var connection
    property var controller
    columns: width < 760 ? 1 : 2
    columnSpacing: 16; rowSpacing: 16
    Panel {
        objectName: "walletConnectionCard"
        Layout.fillWidth: true; Layout.preferredWidth: 650; Layout.alignment: Qt.AlignTop
        RowLayout {
            Layout.fillWidth: true
            MerchantText { text: "Wallet connection"; color: MerchantStyle.MerchantTheme.ink; font.pixelSize: 18; font.weight: Font.DemiBold; Layout.fillWidth: true }
            ActionButton {
                objectName: "checkConnection"
                text: root.connection.checking ? "Checking…" : "Check"
                enabled: root.controller.ready && !root.connection.checking
                onClicked: root.connection.refresh()
            }
        }
        MerchantText { text: "Open and manage your Stagenet wallet in the Monero wallet app."; Layout.fillWidth: true }
        Repeater {
            model: [
                {label: "Wallet backend", value: root.connection.backendConnected ? "Connected" : "Unavailable"},
                {label: "Open wallet", value: root.connection.walletOpen && root.connection.status ? root.connection.status.wallet : "No wallet open"},
                {label: "Node", value: !root.connection.walletOpen ? "—" : root.connection.nodeConnected ? "Connected" : "Disconnected"},
                {label: "Synchronization", value: !root.connection.walletOpen ? "—" : root.connection.synced ? "Synchronized" : "Syncing…"}
            ]
            RowLayout {
                required property var modelData
                Layout.fillWidth: true
                MerchantText { text: modelData.label; Layout.preferredWidth: 120 }
                MerchantText { text: modelData.value; color: MerchantStyle.MerchantTheme.ink; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
            }
        }
        MerchantText { objectName: "walletConnectionError"; visible: text.length > 0; text: root.connection.error; color: MerchantStyle.MerchantTheme.danger; Layout.fillWidth: true }
    }
    Panel {
        objectName: "confirmationPolicyCard"
        Layout.fillWidth: true; Layout.preferredWidth: 350; Layout.alignment: Qt.AlignTop
        MerchantText { text: "When an invoice is paid"; color: MerchantStyle.MerchantTheme.ink; font.pixelSize: 18; font.weight: Font.DemiBold; Layout.fillWidth: true }
        MerchantText { text: "Required confirmations for new invoices"; Layout.fillWidth: true }
        PolicySelector {
            objectName: "confirmationPolicySelector"
            Layout.fillWidth: true
            savedPolicy: root.controller.confirmationPolicy
            saving: root.controller.savingPolicy
            enabled: root.controller.ready && root.controller.storageAvailable && root.controller.policyLoaded && !root.controller.savingPolicy && !root.controller.mutating
            onPolicySelected: function(value) { root.controller.setPolicy(value) }
        }
        MerchantText { text: "Existing invoices keep their saved policy. Confirmation times are approximate."; font.pixelSize: 12; Layout.fillWidth: true }
        MerchantText { objectName: "confirmationPolicyError"; visible: text.length > 0; text: root.controller.policyError; color: MerchantStyle.MerchantTheme.danger; Layout.fillWidth: true }
    }
}
