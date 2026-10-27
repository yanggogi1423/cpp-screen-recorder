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
        radius: 8
        color: "#151515"
        border.color: control.activeFocus ? "#bbbbbb" : "#3b3b3b"
    }
}
