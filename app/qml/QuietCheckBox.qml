import QtQuick
import QtQuick.Controls

CheckBox {
    id: control
    implicitHeight: 30
    leftPadding: 30
    rightPadding: 6
    hoverEnabled: true
    font.pixelSize: 12
    indicator: Rectangle {
        x: 6
        y: (control.height - height) / 2
        width: 16
        height: 16
        radius: 4
        color: control.checked ? "#626262" : "transparent"
        border.color: "#777777"
        Text {
            anchors.centerIn: parent
            text: control.checked ? "✓" : ""
            color: Theme.text
            font.pixelSize: 12
        }
    }
    contentItem: Text {
        text: control.text
        font: control.font
        color: Theme.text
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        radius: 6
        color: control.hovered ? Theme.hover : "transparent"
    }
}
