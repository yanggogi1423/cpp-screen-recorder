import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: route
    required property var sourceData
    property bool allowSelection: true
    property var mix: recorder.mixStates[sourceData.id] || ({
            gain: 1,
            muted: false
        })
    implicitHeight: 103
    color: sourceData.selected ? "#252525" : "#191919"
    border.color: sourceData.selected ? "#494949" : "#2b2b2b"
    radius: 8
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 3
        RowLayout {
            CheckBox {
                checked: route.sourceData.selected
                enabled: route.allowSelection
                onToggled: recorder.selectAudio(route.sourceData.id, checked)
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
                    text: route.sourceData.kind === 0 ? "프로그램" : route.sourceData.kind === 1 ? "마이크" : "시스템 전체"
                    color: "#999999"
                    font.pixelSize: 10
                }
            }
            ActionButton {
                text: route.mix.muted ? "음소거됨" : "음소거"
                implicitWidth: 68
                implicitHeight: 28
                font.pixelSize: 10
                enabled: route.sourceData.selected
                onClicked: recorder.setAudioMuted(route.sourceData.id, !route.mix.muted)
            }
        }
        RowLayout {
            Slider {
                Layout.fillWidth: true
                from: 0
                to: 2
                value: route.mix.gain
                enabled: route.sourceData.selected
                onMoved: recorder.setAudioGain(route.sourceData.id, value)
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
