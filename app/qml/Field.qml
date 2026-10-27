import QtQuick
import QtQuick.Controls
TextField {
    id: control
    implicitHeight: 40
    implicitWidth: 72
    color: "#f2f1fa"
    selectByMouse: true
    selectionColor: "#7255c3"
    font.pixelSize: 14
    leftPadding: 12
    background: Rectangle { radius: 8; color: "#12141c"; border.color: control.activeFocus ? "#a48bff" : "#363847" }
}
