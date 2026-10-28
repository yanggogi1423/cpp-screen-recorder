import QtQuick
import QtQuick.Controls

TextField {
    id: control
    implicitHeight: 40
    implicitWidth: 72
    color: "#eeeeee"
    selectByMouse: true
    selectionColor: "#555555"
    font.pixelSize: 14
    leftPadding: 12
    background: Rectangle {
        radius: Theme.radius
        color: Theme.field
        border.color: control.activeFocus ? "#777777" : Theme.border
    }
}
