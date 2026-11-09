import QtQuick
import QtQuick.Layouts

import "." as MerchantStyle
Rectangle {
    color: MerchantStyle.MerchantTheme.surface
    border.color: MerchantStyle.MerchantTheme.line
    radius: 18
    implicitHeight: panelContent.implicitHeight + 40
    default property alias content: panelContent.data
    ColumnLayout {
        id: panelContent
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 20
        spacing: 12
    }
}
