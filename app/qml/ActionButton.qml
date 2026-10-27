import QtQuick
import QtQuick.Controls

Button {
    id: control
    property bool primary: false
    property bool danger: false
    property string glyph: ""
    property url asset: ""
    implicitHeight: 42
    implicitWidth: Math.max(80, contentItem.implicitWidth + 32)
    font.pixelSize: 13
    font.weight: Font.DemiBold
    opacity: enabled ? 1 : 0.4
    background: Rectangle {
        radius: 9
        color: control.danger ? (control.hovered ? "#ed5656" : "#d94747") : control.primary ? (control.hovered ? "#ffffff" : "#e6e6e6") : (control.hovered ? "#303030" : "#222222")
        border.color: control.primary || control.danger ? "transparent" : "#383838"
        Behavior on color {
            ColorAnimation {
                duration: 120
            }
        }
    }
    contentItem: Text {
        text: (control.glyph ? control.glyph + "  " : "") + control.text
        color: control.primary ? "#141414" : "#e8e8e8"
        font: control.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
