import QtQuick
import QtQuick.Controls
Button {
    id: control
    property bool primary: false
    property bool danger: false
    property string glyph: ""
    implicitHeight: 42
    implicitWidth: Math.max(80, contentItem.implicitWidth + 32)
    font.pixelSize: 13
    font.weight: Font.DemiBold
    opacity: enabled ? 1 : 0.4
    background: Rectangle {
        radius: 9
        color: control.danger ? (control.hovered ? "#ea5c77" : "#ce405e") : control.primary ? (control.hovered ? "#a48bff" : "#9270fa") : (control.hovered ? "#323443" : "#252735")
        border.color: control.primary || control.danger ? "transparent" : "#3b3d4e"
        Behavior on color { ColorAnimation { duration: 120 } }
    }
    contentItem: Text {
        text: (control.glyph ? control.glyph + "  " : "") + control.text
        color: control.primary ? "#13101e" : "#ececf4"
        font: control.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
