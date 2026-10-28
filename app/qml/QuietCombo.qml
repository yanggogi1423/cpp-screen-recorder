import QtQuick
import QtQuick.Controls

ComboBox {
    id: control
    implicitHeight: 38
    implicitWidth: 120
    leftPadding: 12
    rightPadding: 30
    hoverEnabled: true
    font.pixelSize: 12
    background: Rectangle {
        radius: Theme.radius
        color: control.hovered ? "#2c2c2c" : Theme.field
        border.color: control.activeFocus ? "#777777" : Theme.border
    }
    contentItem: Text {
        text: control.displayText
        font: control.font
        color: control.enabled ? Theme.text : "#707070"
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Text {
        x: control.width - 24
        y: (control.height - height) / 2
        text: "⌄"
        color: Theme.muted
        font.pixelSize: 16
    }
    popup: Popup {
        y: control.height + 5
        width: Math.max(control.width, 120)
        padding: 6
        implicitHeight: Math.min(contentItem.implicitHeight + 12, 300)
        background: Rectangle {
            color: "#252525"
            radius: Theme.popupRadius
            border.color: "#414141"
        }
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
    }
    delegate: ItemDelegate {
        id: row
        required property var modelData
        required property int index
        width: control.popup.width - 12
        height: 34
        text: control.textRole ? modelData[control.textRole] : modelData
        highlighted: control.highlightedIndex === index
        contentItem: Text {
            text: row.text
            color: Theme.text
            font: control.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: 6
            color: row.highlighted || row.hovered ? Theme.hover : "transparent"
        }
    }
}
