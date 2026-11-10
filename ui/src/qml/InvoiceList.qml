import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "." as MerchantStyle

Rectangle {
    id: root
    property var controller
    property var statusLabel
    property var statusColor
    property bool compact: false
    property string filter: "all"
    readonly property var visibleInvoices: controller.invoices.filter(function(row) {
        const matchesFilter = root.filter === "all"
            || (root.filter === "paid" && row.status === "paid")
            || (root.filter === "active" && row.status !== "paid")
            || (root.filter === "review" && (row.requires_review || row.creation_state !== "ready"))
        const query = search.text.trim().toLowerCase()
        return matchesFilter && (!query || [row.title || "", row.id, row.amount_due_xmr].join(" ").toLowerCase().indexOf(query) >= 0)
    })
    signal connectTerminal()
    color: MerchantStyle.MerchantTheme.surface
    border.color: MerchantStyle.MerchantTheme.line
    radius: 18
    implicitHeight: compact ? 390 : 650
    // A background payment update may remove a row from this filter. Keep its
    // detail visible so the operator can still see that it became paid.
    onFilterChanged: Qt.callLater(ensureSelection)
    function ensureSelection() {
        if (visibleInvoices.length && !visibleInvoices.some(function(row) { return row.id === root.controller.selectedId }))
            controller.select(visibleInvoices[0].id)
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            MerchantText { text: "Invoices"; color: MerchantStyle.MerchantTheme.ink; font.pixelSize: 18; font.weight: Font.DemiBold }
            Item { Layout.fillWidth: true }
            MerchantText { text: String(root.controller.invoices.length); font.family: MerchantStyle.MerchantTheme.monoFont }
        }
        MerchantField {
            id: search
            objectName: "invoiceSearch"
            Layout.fillWidth: true
            placeholderText: "Search invoices"
            Accessible.name: "Search by invoice title, ID or amount"
            onTextChanged: Qt.callLater(root.ensureSelection)
        }
        Flow {
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                model: [{key: "all", label: "All"}, {key: "paid", label: "Paid"}, {key: "active", label: "In progress"}, {key: "review", label: "Needs review"}]
                ActionButton {
                    required property var modelData
                    objectName: "invoiceFilter_" + modelData.key
                    text: modelData.label
                    implicitHeight: 34; leftPadding: 10; rightPadding: 10
                    primary: root.filter === modelData.key
                    Accessible.checkable: true
                    Accessible.checked: root.filter === modelData.key
                    onClicked: root.filter = modelData.key
                }
            }
        }
        ListView {
            id: list
            objectName: "invoiceList"
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true; spacing: 6
            model: root.visibleInvoices
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                id: row
                required property var modelData
                required property int index
                objectName: "invoiceRow" + index
                width: ListView.view.width; height: 112
                padding: 12
                Accessible.name: (modelData.title || "Invoice") + ", " + modelData.amount_due_xmr + " XMR, " + root.statusLabel(modelData)
                onClicked: root.controller.select(modelData.id)
                background: Rectangle {
                    radius: 10
                    color: root.controller.selectedId === row.modelData.id ? "#24ff6a2b" : row.hovered ? MerchantStyle.MerchantTheme.raised : "transparent"
                    border.color: row.activeFocus ? MerchantStyle.MerchantTheme.accent : "transparent"
                }
                contentItem: ColumnLayout {
                    spacing: 6
                    MerchantText {
                        Layout.fillWidth: true
                        text: row.modelData.title || "Invoice " + row.modelData.id.slice(-8)
                        color: MerchantStyle.MerchantTheme.ink; font.weight: Font.DemiBold
                        elide: Text.ElideRight; maximumLineCount: 1
                    }
                    MerchantText {
                        Layout.fillWidth: true
                        text: row.modelData.amount_due_xmr + " XMR"
                        font.family: MerchantStyle.MerchantTheme.monoFont; font.pixelSize: 16
                        color: MerchantStyle.MerchantTheme.ink; wrapMode: Text.WrapAnywhere
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        MerchantText {
                            Layout.fillWidth: true
                            text: root.statusLabel(row.modelData)
                            color: root.statusColor(row.modelData); font.pixelSize: 12
                        }
                        MerchantText {
                            text: row.modelData.requires_review ? "Review required" : new Date(row.modelData.created_at).toLocaleDateString()
                            color: row.modelData.requires_review ? MerchantStyle.MerchantTheme.warning : MerchantStyle.MerchantTheme.muted
                            font.pixelSize: 11
                        }
                    }
                }
            }
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.max(0, parent.width - 24)
                visible: list.count === 0
                spacing: 12
                MerchantText {
                    objectName: "invoiceEmptyState"
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: !root.controller.wallet ? "Open a Stagenet wallet in the Monero wallet app to see its invoices."
                        : root.controller.state === "loading" ? "Loading this wallet’s invoices…"
                        : root.controller.invoices.length > 0 ? "No invoices match. Try another search or filter."
                        : "Your invoices will appear here when you create an order on your Android POS."
                }
                ActionButton {
                    visible: !!root.controller.wallet && root.controller.invoices.length === 0 && root.controller.state !== "loading"
                    Layout.alignment: Qt.AlignHCenter
                    text: "Connect a terminal"
                    onClicked: root.connectTerminal()
                }
            }
        }
    }
}
