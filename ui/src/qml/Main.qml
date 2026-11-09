import QtQuick

MerchantApp {
    id: root
    property var replica: null

    client: replica
    watchReply: function(reply, success, failure) { logos.watch(reply, success, failure) }

    function reconnect() {
        replica = logos.module("monero_merchant_ui")
        ready = replica !== null && logos.isViewModuleReady("monero_merchant_ui")
    }
    Connections {
        target: logos
        function onViewModuleReadyChanged(name, isReady) {
            if (name === "monero_merchant_ui") root.reconnect()
        }
    }
    Component.onCompleted: reconnect()
}
