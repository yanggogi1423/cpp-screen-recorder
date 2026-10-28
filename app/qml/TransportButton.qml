import QtQuick
import QtQuick.Controls

Button {
    id: control
    property string symbol: "record"
    property bool large: false
    implicitWidth: large ? 76 : 48
    implicitHeight: implicitWidth
    opacity: enabled ? 1 : 0.35
    Accessible.name: text
    ToolTip.visible: hovered
    ToolTip.text: text
    background: Rectangle {
        radius: control.large ? width / 2 : 10
        color: control.down ? "#363636" : control.hovered ? "#2d2d2d" : "#202020"
        border.color: control.activeFocus ? "#aaaaaa" : "#494949"
        border.width: 1
    }
    contentItem: Image {
        source: "qrc:/assets/" + control.symbol + ".png"
        sourceSize: Qt.size(control.large ? 96 : 48, control.large ? 96 : 48)
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
    }
}
