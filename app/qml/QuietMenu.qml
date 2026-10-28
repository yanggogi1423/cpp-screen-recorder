import QtQuick
import QtQuick.Controls

Menu {
    id: menu
    padding: 6
    implicitWidth: Math.max(234, contentItem.implicitWidth + 12)
    overlap: 0
    popupType: Popup.Item
    topMargin: 6
    delegate: MenuItem {
        id: item
        implicitHeight: 34
        implicitWidth: 224
        leftPadding: 10
        rightPadding: 10
        font.pixelSize: 12
        contentItem: Text {
            text: item.text
            color: item.enabled ? Theme.text : "#666666"
            font: item.font
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 6
            color: item.highlighted ? Theme.hover : "transparent"
        }
    }
    background: Rectangle {
        radius: Theme.popupRadius
        color: "#252525"
        border.color: "#414141"
    }
}
