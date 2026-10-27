import QtQuick

Rectangle {
    property real level: 0
    color: "#303030"
    radius: 2
    implicitHeight: 4
    Rectangle {
        height: parent.height
        width: parent.width * Math.min(1, Math.max(0, parent.level))
        radius: 2
        color: parent.level > 0.95 ? "#f16b64" : "#a1c5ad"
        Behavior on width {
            NumberAnimation {
                duration: 75
            }
        }
    }
}
