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
    Panel {
        objectName: "priceSettingsCard"
        Layout.fillWidth: true; Layout.preferredWidth: 650; Layout.alignment: Qt.AlignTop
        Layout.columnSpan: root.columns
        MerchantText { text: "USD price feed"; color: MerchantStyle.MerchantTheme.ink; font.pixelSize: 18; font.weight: Font.DemiBold; Layout.fillWidth: true }
        MerchantText { text: "Configure the XMR/USD feed used for new USD orders. The RPC endpoint is shared with other apps using this chain."; Layout.fillWidth: true }
        Connections {
            target: root.controller
            function onPriceSettingsChanged() {
                const s = root.controller.priceSettings
                if (!s) return
                endpoint.text = s.endpoint || ""
                chain.text = String(s.chain_id)
                feed.text = s.feed || ""
                sequencer.text = s.sequencer || ""
                maxAge.text = String(s.max_age_seconds)
                grace.text = String(s.grace_seconds)
            }
        }
        MerchantText { text: "RPC endpoint"; Layout.fillWidth: true }
        MerchantField { id: endpoint; objectName: "priceEndpoint"; Layout.fillWidth: true; placeholderText: "https://arb1.arbitrum.io/rpc"; enabled: !root.controller.priceBusy }
        MerchantText { text: "Chain ID"; Layout.fillWidth: true }
        MerchantField { id: chain; objectName: "priceChain"; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly; enabled: !root.controller.priceBusy }
        MerchantText { text: "XMR/USD feed contract"; Layout.fillWidth: true }
        MerchantField { id: feed; objectName: "priceFeed"; Layout.fillWidth: true; enabled: !root.controller.priceBusy }
        MerchantText { text: "Sequencer uptime contract"; Layout.fillWidth: true }
        MerchantField { id: sequencer; objectName: "priceSequencer"; Layout.fillWidth: true; enabled: !root.controller.priceBusy }
        MerchantText { text: "Leave the sequencer contract empty only for a chain that does not require a sequencer check."; font.pixelSize: 12; Layout.fillWidth: true }
        MerchantText { text: "Maximum price age (seconds)"; Layout.fillWidth: true }
        MerchantField { id: maxAge; objectName: "priceMaxAge"; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly; enabled: !root.controller.priceBusy }
        MerchantText { text: "Sequencer recovery grace period (seconds)"; Layout.fillWidth: true }
        MerchantField { id: grace; objectName: "priceGrace"; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly; enabled: !root.controller.priceBusy }
        RowLayout {
            Layout.fillWidth: true
            ActionButton {
                objectName: "savePriceSettings"
                text: root.controller.priceBusy ? "Saving…" : "Save price settings"
                enabled: root.controller.ready && !root.controller.priceBusy && root.controller.priceSettings !== null
                onClicked: {
                    if (!/^\d+$/.test(chain.text) || !/^\d+$/.test(maxAge.text) || !/^\d+$/.test(grace.text)) {
                        root.controller.priceError = "Enter whole numbers for chain ID and time limits."; return
                    }
                    root.controller.savePriceSettings({endpoint: endpoint.text.trim(), chain_id: Number(chain.text),
                        feed: feed.text.trim(), sequencer: sequencer.text.trim(),
                        max_age_seconds: Number(maxAge.text), grace_seconds: Number(grace.text)})
                }
            }
            ActionButton { objectName: "reloadPriceSettings"; text: "Reload"; enabled: root.controller.ready && !root.controller.priceBusy; onClicked: root.controller.loadPriceSettings() }
        }
        MerchantText { visible: text.length > 0; text: root.controller.priceNotice; Layout.fillWidth: true }
        MerchantText { objectName: "priceSettingsError"; visible: text.length > 0; text: root.controller.priceError; color: MerchantStyle.MerchantTheme.danger; Layout.fillWidth: true }
    }

}
