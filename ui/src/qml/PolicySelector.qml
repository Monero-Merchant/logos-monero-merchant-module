import QtQuick
import QtQuick.Controls.Basic
import "." as MerchantStyle

ComboBox {
    id: control
    property int savedPolicy: 5
    property bool saving: false
    signal policySelected(int confirmations)
    model: [1, 5, 10]
    function label(value) { return value + " (~" + (value * 2) + " min)" }
    currentIndex: model.indexOf(savedPolicy)
    displayText: saving ? "Saving…" : label(savedPolicy)
    implicitHeight: 44
    leftPadding: 12; rightPadding: 32
    font.family: MerchantStyle.MerchantTheme.uiFont; font.pixelSize: 14
    Accessible.name: "Confirmation Policy for new invoices"
    onActivated: policySelected(model[currentIndex])
    contentItem: Text {
        text: control.displayText; font: control.font
        color: control.enabled ? MerchantStyle.MerchantTheme.ink : MerchantStyle.MerchantTheme.muted
        verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
    }
    indicator: Text {
        text: "▾"; font.family: MerchantStyle.MerchantTheme.uiFont; font.pixelSize: 16
        color: MerchantStyle.MerchantTheme.secondary
        x: control.width - width - 12; y: (control.height - height) / 2
    }
    background: Rectangle {
        radius: 10; color: MerchantStyle.MerchantTheme.raised
        border.color: control.activeFocus || control.popup.visible ? MerchantStyle.MerchantTheme.accent : MerchantStyle.MerchantTheme.line
    }
    delegate: ItemDelegate {
        required property var modelData
        required property int index
        width: control.width - 12; height: 38
        highlighted: control.highlightedIndex === index
        contentItem: Text { text: control.label(modelData); font: control.font; color: MerchantStyle.MerchantTheme.ink; verticalAlignment: Text.AlignVCenter }
        background: Rectangle { radius: 6; color: parent.highlighted ? "#24ff6a2b" : "transparent" }
    }
    popup: Popup {
        y: control.height + 6; width: control.width; padding: 6
        onOpened: control.currentIndex = control.model.indexOf(control.savedPolicy)
        contentItem: ListView {
            clip: true; implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
        }
        background: Rectangle { radius: 10; color: MerchantStyle.MerchantTheme.surface; border.color: MerchantStyle.MerchantTheme.line }
    }
}
