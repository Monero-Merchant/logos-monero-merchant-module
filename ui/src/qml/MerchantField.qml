import QtQuick
import QtQuick.Controls.Basic

import "." as MerchantStyle
TextField {
    id: control
    implicitHeight: 44
    color: MerchantStyle.MerchantTheme.ink; placeholderTextColor: MerchantStyle.MerchantTheme.muted; selectionColor: "#804120"
    font.family: MerchantStyle.MerchantTheme.uiFont
    font.pixelSize: 14; padding: 12; selectByMouse: true
    background: Rectangle { color: MerchantStyle.MerchantTheme.raised; radius: 10; border.color: control.activeFocus ? MerchantStyle.MerchantTheme.accent : MerchantStyle.MerchantTheme.line }
}
