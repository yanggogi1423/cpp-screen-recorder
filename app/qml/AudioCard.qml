import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: card
    property bool mic: false
    property bool allowSelection: true
    property bool allowMix: true
    property var routes: []
    readonly property var available: mic ? routes : routes.filter(a => a.kind === 2).concat(routes.filter(a => a.kind === 0))
    readonly property var selectedRoutes: available.filter(a => a.selected)
    readonly property string summary: selectedRoutes.length === 0 ? "사용 안 함" : selectedRoutes.length === 1 ? selectedRoutes[0].name : selectedRoutes[0].name + " 외 " + (selectedRoutes.length - 1) + "개"
    readonly property bool audible: selectedRoutes.some(a => !(recorder.mixStates[a.id] || {}).muted)
    implicitHeight: 92
    radius: Theme.radius
    color: Theme.panel
    border.color: Theme.border
    Connections {
        target: recorder
        function onStateChanged() {
            if (recorder.state !== "idle")
                choices.close();
        }
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 3
        RowLayout {
            Text {
                text: card.mic ? "마이크" : "컴퓨터 소리"
                color: Theme.muted
                font.pixelSize: 11
                Layout.fillWidth: true
            }
            IconButton {
                implicitWidth: 25
                implicitHeight: 24
                symbol: card.audible ? "speaker" : "muted"
                text: card.audible ? "선택한 소리 음소거" : "선택한 소리 음소거 해제"
                enabled: card.allowMix && card.selectedRoutes.length > 0
                onClicked: {
                    var chosen = card.selectedRoutes.slice();
                    var muted = card.audible;
                    chosen.forEach(a => recorder.setAudioMuted(a.id, muted));
                }
            }
        }
        Button {
            id: picker
            objectName: card.mic ? "microphonePicker" : "computerPicker"
            Layout.fillWidth: true
            implicitHeight: 30
            text: card.summary
            hoverEnabled: true
            Accessible.name: (card.mic ? "마이크: " : "컴퓨터 소리: ") + text
            ToolTip.visible: hovered && !choices.opened
            ToolTip.delay: 600
            ToolTip.text: card.selectedRoutes.map(a => a.name).join("\n") || "사용 안 함"
            background: Rectangle {
                radius: 6
                color: picker.hovered ? Theme.hover : "transparent"
            }
            contentItem: RowLayout {
                Text {
                    Layout.fillWidth: true
                    text: picker.text
                    color: Theme.text
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
                Text {
                    text: "⌄"
                    color: Theme.muted
                    font.pixelSize: 15
                }
            }
            onClicked: choices.opened ? choices.close() : choices.open()
        }
        Meter {
            Layout.fillWidth: true
            level: {
                var peak = 0;
                card.selectedRoutes.forEach(a => peak = Math.max(peak, recorder.audioLevels[a.id] || 0));
                return peak;
            }
        }
    }
    Popup {
        id: choices
        objectName: card.mic ? "microphoneChoices" : "computerChoices"
        y: card.height + 5
        width: Math.max(card.width, 310)
        padding: 8
        margins: 8
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        implicitHeight: Math.min(list.implicitHeight + 16, 320)
        background: Rectangle {
            radius: Theme.popupRadius
            color: "#252525"
            border.color: "#414141"
        }
        contentItem: ScrollView {
            id: scroll
            clip: true
            contentWidth: availableWidth
            Column {
                id: list
                width: scroll.availableWidth
                spacing: 2
                ItemDelegate {
                    id: off
                    text: "사용 안 함"
                    width: parent.width
                    height: 34
                    enabled: card.allowSelection
                    contentItem: Text {
                        text: off.text
                        color: Theme.text
                        verticalAlignment: Text.AlignVCenter
                        font.pixelSize: 12
                    }
                    background: Rectangle {
                        radius: 6
                        color: off.hovered ? Theme.hover : "transparent"
                    }
                    onClicked: {
                        var chosen = card.selectedRoutes.slice();
                        chosen.forEach(a => recorder.selectAudio(a.id, false));
                        choices.close();
                    }
                }
                Repeater {
                    model: card.available.length
                    delegate: Column {
                        required property int index
                        property var sourceData: card.available[index] || ({
                                id: "",
                                name: "",
                                kind: -1,
                                selected: false
                            })
                        width: list.width
                        Text {
                            visible: !card.mic && (index === 0 || card.available[index - 1].kind !== sourceData.kind)
                            width: parent.width
                            height: visible ? 28 : 0
                            leftPadding: 10
                            topPadding: 9
                            text: sourceData.kind === 2 ? "출력 장치 전체" : "앱별 소리"
                            color: Theme.muted
                            font.pixelSize: 10
                        }
                        CheckDelegate {
                            id: option
                            objectName: "source-option-" + sourceData.id
                            text: sourceData.name
                            width: list.width
                            height: 36
                            leftPadding: 34
                            rightPadding: 8
                            checked: sourceData.selected
                            enabled: card.allowSelection
                            onToggled: recorder.selectAudio(sourceData.id, checked)
                            contentItem: Text {
                                text: option.text
                                color: option.enabled ? Theme.text : Theme.muted
                                font.pixelSize: 12
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }
                            background: Rectangle {
                                radius: 6
                                color: option.hovered || option.highlighted ? Theme.hover : "transparent"
                            }
                            indicator: Rectangle {
                                x: 9
                                y: (option.height - height) / 2
                                width: 15
                                height: 15
                                radius: 4
                                color: option.checked ? "#626262" : "transparent"
                                border.color: "#797979"
                                Text {
                                    anchors.centerIn: parent
                                    text: option.checked ? "✓" : ""
                                    color: Theme.text
                                    font.pixelSize: 12
                                }
                            }
                        }
                    }
                }
                Text {
                    visible: card.available.length === 0
                    width: parent.width
                    height: visible ? 38 : 0
                    leftPadding: 10
                    topPadding: 10
                    text: recorder.discovering ? "찾는 중…" : card.mic ? "연결된 마이크 없음" : "사용 가능한 소리 없음"
                    color: Theme.muted
                    font.pixelSize: 11
                }
            }
        }
    }
}
