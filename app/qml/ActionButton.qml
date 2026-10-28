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
        radius: Theme.radius
        color: control.danger ? (control.hovered ? "#be433d" : "#a13834") : control.down ? Theme.pressed : control.hovered ? Theme.hover : control.primary ? "#353535" : Theme.field
        border.color: control.primary ? "#525252" : Theme.border
        Behavior on color {
            ColorAnimation {
                duration: 120
            }
        }
    }
    contentItem: Text {
        text: (control.glyph ? control.glyph + "  " : "") + control.text
        color: Theme.text
        font: control.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
