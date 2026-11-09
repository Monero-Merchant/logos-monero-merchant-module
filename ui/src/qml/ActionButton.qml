import QtQuick
import QtQuick.Controls.Basic

import "." as MerchantStyle
Button {
    id: control
    property bool primary: false
    implicitHeight: 44
    leftPadding: 16
    rightPadding: 16
    hoverEnabled: true
    font.pixelSize: 13
    font.family: MerchantStyle.MerchantTheme.uiFont
    font.weight: Font.DemiBold
    Accessible.name: text
    contentItem: Text {
        text: control.text
        font: control.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        color: !control.enabled ? MerchantStyle.MerchantTheme.muted : control.primary ? MerchantStyle.MerchantTheme.background : MerchantStyle.MerchantTheme.ink
    }
    background: Rectangle {
        radius: 10
        color: !control.enabled ? MerchantStyle.MerchantTheme.raised : control.primary
            ? (control.down ? "#df5420" : control.hovered ? MerchantStyle.MerchantTheme.accentHover : MerchantStyle.MerchantTheme.accent)
            : (control.down ? "#292932" : control.hovered ? "#22222c" : MerchantStyle.MerchantTheme.raised)
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? MerchantStyle.MerchantTheme.accent : control.primary && control.enabled ? MerchantStyle.MerchantTheme.accent : MerchantStyle.MerchantTheme.line
    }
}
