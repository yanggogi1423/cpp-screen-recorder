import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: route
    required property var sourceData
    property bool allowSelection: true
    property bool allowMix: true
    property var mix: recorder.mixStates[sourceData.id] || ({
            gain: 1,
            muted: false
        })
    implicitHeight: 92
    color: Theme.panel
    border.color: sourceData.selected ? "#515151" : Theme.border
    radius: Theme.radius
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 3
        RowLayout {
            CheckBox {
                id: selected
                implicitWidth: 26
                implicitHeight: 28
                checked: route.sourceData.selected
                Accessible.name: route.sourceData.name
                enabled: route.allowSelection
                onToggled: recorder.selectAudio(route.sourceData.id, checked)
                indicator: Rectangle {
                    x: 3
                    y: 6
                    width: 15
                    height: 15
                    radius: 4
                    color: selected.checked ? "#626262" : "transparent"
                    border.color: "#777777"
                    Text {
                        anchors.centerIn: parent
                        text: selected.checked ? "✓" : ""
                        color: Theme.text
                        font.pixelSize: 12
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    Layout.fillWidth: true
                    text: route.sourceData.name
                    color: "#e8e8e8"
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    visible: route.sourceData.kind === 2
                    text: "출력 장치 전체"
                    color: "#999999"
                    font.pixelSize: 10
                }
            }
            IconButton {
                text: route.sourceData.name + (route.mix.muted ? " 음소거 해제" : " 음소거")
                symbol: route.mix.muted ? "muted" : "speaker"
                implicitWidth: 28
                implicitHeight: 28
                enabled: route.sourceData.selected && route.allowMix
                onClicked: recorder.setAudioMuted(route.sourceData.id, !route.mix.muted)
            }
        }
        RowLayout {
            Slider {
                id: gainSlider
                Accessible.name: route.sourceData.name + " 녹화 음량"
                implicitHeight: 24
                Layout.fillWidth: true
                from: 0
                to: 2
                value: route.mix.gain
                enabled: route.sourceData.selected && route.allowMix
                onMoved: recorder.setAudioGain(route.sourceData.id, value)
                opacity: enabled ? 1 : 0.35
                background: Rectangle {
                    x: gainSlider.leftPadding
                    y: gainSlider.topPadding + gainSlider.availableHeight / 2 - height / 2
                    width: gainSlider.availableWidth
                    height: 3
                    radius: 2
                    color: "#3c3c3c"
                    Rectangle {
                        width: gainSlider.visualPosition * parent.width
                        height: parent.height
                        radius: 2
                        color: "#888888"
                    }
                }
                handle: Rectangle {
                    x: gainSlider.leftPadding + gainSlider.visualPosition * (gainSlider.availableWidth - width)
                    y: gainSlider.topPadding + gainSlider.availableHeight / 2 - height / 2
                    width: 12
                    height: 12
                    radius: 6
                    color: gainSlider.pressed ? "#eeeeee" : "#bbbbbb"
                }
            }
            Text {
                Layout.preferredWidth: 35
                text: Math.round(route.mix.gain * 100) + "%"
                color: "#aaaaaa"
                font.pixelSize: 10
                horizontalAlignment: Text.AlignRight
            }
        }
        Meter {
            Layout.fillWidth: true
            level: recorder.audioLevels[route.sourceData.id] || 0
        }
    }
}
