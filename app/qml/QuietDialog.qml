import QtQuick
import QtQuick.Controls

Dialog {
    id: dialog
    modal: true
    padding: 16
    header: Label {
        text: dialog.title
        visible: text.length > 0
        color: Theme.text
        font.pixelSize: 14
        font.weight: Font.DemiBold
        leftPadding: 18
        rightPadding: 18
        topPadding: 16
        bottomPadding: 12
        background: Item {}
    }
    background: Rectangle {
        radius: Theme.windowRadius
        color: "#252525"
        border.color: "#414141"
    }
}
