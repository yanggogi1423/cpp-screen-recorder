import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtMultimedia

ApplicationWindow {
    id: root
    width: 1380
    height: 920
    minimumWidth: 1120
    minimumHeight: 800
    visible: true
    title: "Luma Recorder · 0.1.0 alpha"
    color: "#101117"
    font.family: Qt.platform.os === "osx" ? "Apple SD Gothic Neo" : "Malgun Gothic"
    property bool idle: recorder.state === "idle"
    property int mode: 0
    property string displayId: displayCombo.currentValue || ""
    property bool closeAfterSave: false
    property int captureTestTick: -1
    palette.text: "#e5e4f0"
    palette.window: "#191b24"
    palette.button: "#252735"
    palette.base: "#13151d"
    palette.buttonText: "#eeeefa"
    palette.highlight: "#9270fa"
    palette.windowText: "#e5e4f0"
    palette.highlightedText: "#171220"

    component Caption: Text {
        color: "#9496aa"
        font.pixelSize: 12
    }
    component Heading: Text {
        color: "#f1f0fa"
        font.pixelSize: 16
        font.weight: Font.DemiBold
    }
    component Select: ComboBox {
        id: select
        implicitHeight: 40
        implicitWidth: 120
        font.pixelSize: 12
        background: Rectangle {
            radius: 8
            color: "#12141c"
            border.color: select.activeFocus ? "#9270fa" : "#363847"
        }
        contentItem: Text {
            text: select.displayText
            color: "#ecebf8"
            font: select.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            leftPadding: 12
            rightPadding: 28
        }
        popup: Popup {
            y: select.height + 4
            width: select.width
            padding: 5
            implicitHeight: Math.min(contentItem.implicitHeight + 10, 320)
            background: Rectangle {
                color: "#232532"
                radius: 9
                border.color: "#494459"
            }
            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: select.popup.visible ? select.delegateModel : null
                currentIndex: select.highlightedIndex
                ScrollIndicator.vertical: ScrollIndicator {}
            }
        }
        delegate: ItemDelegate {
            required property var modelData
            required property int index
            width: select.width - 10
            text: select.textRole ? modelData[select.textRole] : modelData
            font: select.font
            highlighted: select.highlightedIndex === index
            palette.text: "#efedf9"
        }
    }
    Shortcut {
        sequence: "Ctrl+Shift+R"
        onActivated: recorder.toggleRecording()
    }
    Shortcut {
        sequence: "Escape"
        enabled: recorder.state === "countdown"
        onActivated: recorder.toggleRecording()
    }
    onClosing: function (close) {
        if (recorder.state !== "idle") {
            close.accepted = false;
            closeDialog.open();
        }
    }
    Connections {
        target: recorder
        function onEditorRequested() {
            editor.open();
        }
        function onRegionRequested(id, x, y, width, height, scale) {
            region.displayId = id;
            region.x = x;
            region.y = y;
            region.width = width;
            region.height = height;
            region.scaleFactor = scale;
            region.visible = true;
            region.requestActivate();
        }
        function onTrimFinished(file) {
            player.source = file;
            editor.open();
        }
        function onStateChanged() {
            if (root.closeAfterSave && recorder.state === "recording")
                recorder.toggleRecording();
            if (root.closeAfterSave && recorder.state === "idle")
                Qt.quit();
            if (recorder.state === "idle" && editor.opened && player.source.toString().length === 0)
                player.source = recorder.lastFile;
        }
    }
    FolderDialog {
        id: folderDialog
        title: "녹화 저장 폴더"
        onAccepted: recorder.setOutputFolder(selectedFolder)
    }
    FileDialog {
        id: recordingDialog
        title: "편집할 녹화 영상"
        nameFilters: ["영상 파일 (*.mkv *.mp4 *.mov)"]
        onAccepted: recorder.openRecording(selectedFile)
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 76
            color: "#16171f"
            Rectangle {
                width: 1
                anchors.right: parent.right
                height: parent.height
                color: "#292b37"
            }
            Column {
                anchors.horizontalCenter: parent.horizontalCenter
                y: 22
                spacing: 28
                Rectangle {
                    width: 40
                    height: 40
                    radius: 13
                    color: "#9270fa"
                    Text {
                        anchors.centerIn: parent
                        text: "L"
                        color: "#171220"
                        font.pixelSize: 26
                        font.weight: Font.Black
                    }
                }
                Rectangle {
                    width: 46
                    height: 46
                    radius: 12
                    color: "#2d2545"
                    Text {
                        anchors.centerIn: parent
                        text: "◉"
                        color: "#b79bff"
                        font.pixelSize: 25
                    }
                }
                ToolButton {
                    width: 46
                    height: 46
                    text: "▤"
                    font.pixelSize: 24
                    palette.buttonText: "#9395a8"
                    onClicked: recorder.openOutputFolder()
                    ToolTip.visible: hovered
                    ToolTip.text: "녹화 폴더 열기"
                }
            }
            Text {
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 24
                anchors.horizontalCenter: parent.horizontalCenter
                text: "α 0.1"
                color: "#65677b"
                font.pixelSize: 10
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 26
            spacing: 20
            RowLayout {
                Layout.fillWidth: true
                ColumnLayout {
                    spacing: 3
                    Text {
                        text: "Luma"
                        color: "#f5f3fc"
                        font.pixelSize: 28
                        font.weight: Font.DemiBold
                        font.letterSpacing: -0.7
                    }
                    Caption {
                        text: "내 화면, 내 소리, 내 방식으로."
                    }
                }
                Item {
                    Layout.fillWidth: true
                }
                Rectangle {
                    implicitWidth: platformLabel.implicitWidth + 22
                    height: 28
                    radius: 14
                    color: "#20222c"
                    Caption {
                        id: platformLabel
                        anchors.centerIn: parent
                        text: recorder.platformName + "  ·  Preview"
                        font.pixelSize: 11
                    }
                }
                ActionButton {
                    text: "녹화 폴더"
                    glyph: "↗"
                    onClicked: recorder.openOutputFolder()
                }
            }
            Rectangle {
                visible: recorder.message.length > 0
                Layout.fillWidth: true
                implicitHeight: alertText.implicitHeight + 28
                color: "#33262c"
                radius: 10
                border.color: "#67414a"
                Text {
                    id: alertText
                    anchors {
                        left: parent.left
                        right: closeAlert.left
                        verticalCenter: parent.verticalCenter
                        margins: 14
                    }
                    text: recorder.message
                    color: "#f0bdc6"
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                }
                ToolButton {
                    id: closeAlert
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: "×"
                    onClicked: recorder.clearMessage()
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 20
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 16
                    Panel {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 280
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 18
                            spacing: 14
                            RowLayout {
                                Heading {
                                    text: "미리보기"
                                }
                                Rectangle {
                                    width: 6
                                    height: 6
                                    radius: 3
                                    color: recorder.state === "recording" ? "#fb7185" : recorder.hasSource ? "#83d8b0" : "#676a80"
                                }
                                Caption {
                                    text: recorder.state === "recording" ? "녹화 중" : recorder.hasSource ? "연결됨" : "대기 중"
                                    font.pixelSize: 11
                                }
                                Item {
                                    Layout.fillWidth: true
                                }
                                Caption {
                                    text: "LIVE PREVIEW"
                                    font.pixelSize: 9
                                    font.letterSpacing: 2
                                }
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                color: "#0c0d13"
                                radius: 10
                                clip: true
                                Canvas {
                                    anchors.fill: parent
                                    onPaint: {
                                        var c = getContext("2d");
                                        c.strokeStyle = "#181a23";
                                        c.lineWidth = 1;
                                        for (var x = 0; x < width; x += 28) {
                                            c.beginPath();
                                            c.moveTo(x, 0);
                                            c.lineTo(x, height);
                                            c.stroke();
                                        }
                                        for (var y = 0; y < height; y += 28) {
                                            c.beginPath();
                                            c.moveTo(0, y);
                                            c.lineTo(width, y);
                                            c.stroke();
                                        }
                                    }
                                }
                                Image {
                                    anchors.fill: parent
                                    anchors.margins: 1
                                    visible: recorder.hasSource
                                    source: recorder.hasSource ? "image://preview/" + recorder.previewRevision : ""
                                    cache: false
                                    fillMode: Image.PreserveAspectFit
                                    asynchronous: false
                                }
                                Rectangle {
                                    visible: root.captureTestTick >= 0
                                    width: 100
                                    height: 45
                                    x: (root.captureTestTick * 7) % Math.max(1, parent.width - 100)
                                    y: 35
                                    color: root.captureTestTick % 2 ? "#9270fa" : "#7ecdb1"
                                    Text {
                                        anchors.centerIn: parent
                                        text: root.captureTestTick
                                        color: "#14121b"
                                        font.pixelSize: 20
                                    }
                                }
                                Column {
                                    visible: !recorder.hasSource
                                    anchors.centerIn: parent
                                    spacing: 16
                                    Rectangle {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        width: 80
                                        height: 58
                                        color: "#242035"
                                        radius: 12
                                        border.color: "#5a477e"
                                        Rectangle {
                                            anchors.centerIn: parent
                                            width: 34
                                            height: 24
                                            color: "transparent"
                                            radius: 4
                                            border.width: 2
                                            border.color: "#bc9fff"
                                            Rectangle {
                                                width: 12
                                                height: 2
                                                color: "#bc9fff"
                                                anchors.horizontalCenter: parent.horizontalCenter
                                                y: 29
                                            }
                                        }
                                    }
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: "무엇을 담아볼까요?"
                                        color: "#e3deef"
                                        font.pixelSize: 20
                                        font.weight: Font.DemiBold
                                    }
                                    Caption {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: "아래에서 화면을 선택하고 연결해 주세요."
                                    }
                                }
                                Rectangle {
                                    anchors.fill: parent
                                    visible: recorder.state === "countdown"
                                    color: "#cc0c0d13"
                                    Text {
                                        anchors.centerIn: parent
                                        text: recorder.countdown
                                        color: "#be9fff"
                                        font.pixelSize: 100
                                        font.weight: Font.Light
                                    }
                                }
                            }
                            RowLayout {
                                Caption {
                                    Layout.fillWidth: true
                                    text: recorder.hasSource ? recorder.sourceName : "아직 연결된 화면이 없어요"
                                    elide: Text.ElideRight
                                }
                                Caption {
                                    text: recorder.settings.width + " × " + recorder.settings.height + "  /  " + recorder.settings.fps + " fps"
                                    color: "#b8a0eb"
                                    font.pixelSize: 11
                                }
                            }
                        }
                    }
                    Panel {
                        Layout.fillWidth: true
                        implicitHeight: 166
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 18
                            spacing: 14
                            RowLayout {
                                Heading {
                                    text: "녹화할 화면"
                                }
                                Item {
                                    Layout.fillWidth: true
                                }
                                ActionButton {
                                    text: "새로고침"
                                    implicitHeight: 30
                                    font.pixelSize: 11
                                    enabled: root.idle
                                    onClicked: recorder.refresh()
                                }
                            }
                            RowLayout {
                                spacing: 6
                                Repeater {
                                    model: ["전체 화면", "특정 창", "영역 지정"]
                                    delegate: ActionButton {
                                        required property string modelData
                                        required property int index
                                        text: modelData
                                        primary: root.mode === index
                                        implicitHeight: 32
                                        enabled: root.idle
                                        onClicked: root.mode = index
                                    }
                                }
                            }
                            RowLayout {
                                Select {
                                    id: displayCombo
                                    visible: root.mode !== 1
                                    Layout.fillWidth: true
                                    model: recorder.displays
                                    textRole: "name"
                                    valueRole: "id"
                                    enabled: root.idle
                                }
                                Select {
                                    id: windowCombo
                                    visible: root.mode === 1
                                    Layout.fillWidth: true
                                    model: recorder.windows
                                    textRole: "name"
                                    valueRole: "id"
                                    enabled: root.idle
                                }
                                ActionButton {
                                    text: root.mode === 2 ? "영역 선택" : "소스 연결"
                                    enabled: root.idle && (root.mode === 1 ? windowCombo.count > 0 : displayCombo.count > 0)
                                    onClicked: root.mode === 2 ? recorder.beginRegion(displayCombo.currentValue) : recorder.connectSource(root.mode === 1 ? windowCombo.currentValue : displayCombo.currentValue, root.mode === 1)
                                }
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Caption {
                            text: "Ctrl + Shift + R"
                            color: "#bbb1d3"
                            font.pixelSize: 11
                        }
                        Caption {
                            text: "시작 / 종료  ·  앱에 포커스가 있을 때"
                            font.pixelSize: 11
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                        ActionButton {
                            text: "영상 열기"
                            implicitHeight: 32
                            enabled: root.idle
                            onClicked: recordingDialog.open()
                        }
                        ActionButton {
                            text: "최근 영상 편집"
                            implicitHeight: 32
                            enabled: root.idle && recorder.lastFile.toString().length > 0
                            onClicked: editor.open()
                        }
                    }
                }
                ColumnLayout {
                    Layout.preferredWidth: 350
                    Layout.minimumWidth: 330
                    Layout.maximumWidth: 350
                    Layout.fillHeight: true
                    spacing: 16
                    Panel {
                        Layout.fillWidth: true
                        implicitHeight: 292
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 18
                            spacing: 12
                            Heading {
                                text: "영상 설정"
                            }
                            RowLayout {
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Caption {
                                        text: "출력 해상도"
                                    }
                                    RowLayout {
                                        Field {
                                            Layout.fillWidth: true
                                            text: String(recorder.settings.width)
                                            validator: IntValidator {
                                                bottom: 64
                                                top: 16384
                                            }
                                            enabled: root.idle
                                            onEditingFinished: recorder.setSetting("width", text)
                                        }
                                        Caption {
                                            text: "×"
                                        }
                                        Field {
                                            Layout.fillWidth: true
                                            text: String(recorder.settings.height)
                                            validator: IntValidator {
                                                bottom: 64
                                                top: 16384
                                            }
                                            enabled: root.idle
                                            onEditingFinished: recorder.setSetting("height", text)
                                        }
                                    }
                                }
                                ColumnLayout {
                                    Layout.preferredWidth: 70
                                    Caption {
                                        text: "FPS"
                                    }
                                    Field {
                                        Layout.fillWidth: true
                                        text: String(recorder.settings.fps)
                                        validator: IntValidator {
                                            bottom: 1
                                            top: 240
                                        }
                                        enabled: root.idle
                                        onEditingFinished: recorder.setSetting("fps", text)
                                    }
                                }
                            }
                            RowLayout {
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Caption {
                                        text: "인코더"
                                    }
                                    Select {
                                        Layout.fillWidth: true
                                        model: Qt.platform.os === "osx" ? ["CPU · H.264", "Apple · VideoToolbox"] : ["CPU · H.264", "NVIDIA · NVENC", "Intel · QSV", "AMD · AMF"]
                                        property var ids: Qt.platform.os === "osx" ? ["libx264", "h264_videotoolbox"] : ["libx264", "h264_nvenc", "h264_qsv", "h264_amf"]
                                        currentIndex: Math.max(0, ids.indexOf(recorder.settings.encoder))
                                        enabled: root.idle
                                        onActivated: recorder.setSetting("encoder", ids[currentIndex])
                                    }
                                }
                                ColumnLayout {
                                    Layout.preferredWidth: 76
                                    Caption {
                                        text: "파일"
                                    }
                                    Select {
                                        Layout.fillWidth: true
                                        model: ["MKV", "MP4"]
                                        currentIndex: recorder.settings.container === "mp4" ? 1 : 0
                                        enabled: root.idle
                                        onActivated: recorder.setSetting("container", currentText.toLowerCase())
                                    }
                                }
                            }
                            RowLayout {
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Caption {
                                        text: "시작 지연 (초) · 0이면 즉시"
                                    }
                                    Field {
                                        Layout.fillWidth: true
                                        text: String(recorder.settings.delay)
                                        validator: IntValidator {
                                            bottom: 0
                                            top: 60
                                        }
                                        enabled: root.idle
                                        onEditingFinished: recorder.setSetting("delay", text)
                                    }
                                }
                                CheckBox {
                                    text: "커서 포함"
                                    checked: recorder.settings.cursor
                                    enabled: root.idle
                                    font.pixelSize: 11
                                    onToggled: recorder.setSetting("cursor", checked)
                                }
                            }
                            Caption {
                                text: "성능은 해상도·FPS·하드웨어에 따라 달라집니다."
                                font.pixelSize: 10
                            }
                        }
                    }
                    Panel {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 175
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 18
                            spacing: 10
                            RowLayout {
                                Heading {
                                    text: "녹화용 오디오 믹서"
                                }
                                Item {
                                    Layout.fillWidth: true
                                }
                                Caption {
                                    text: "실시간 조절"
                                    color: "#b79aef"
                                    font.pixelSize: 10
                                }
                            }
                            Caption {
                                Layout.fillWidth: true
                                text: "앱·마이크별 독립 트랙 · 녹화 중 음량·뮤트 조절\n운영체제의 재생·통화 음량은 바꾸지 않습니다."
                                wrapMode: Text.Wrap
                                font.pixelSize: 11
                                lineHeight: 1.3
                            }
                            ListView {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                spacing: 7
                                model: recorder.audio
                                ScrollBar.vertical: ScrollBar {}
                                delegate: Rectangle {
                                    required property var modelData
                                    width: ListView.view.width
                                    height: 104
                                    radius: 9
                                    color: modelData.selected ? "#292239" : "#14161e"
                                    border.color: modelData.selected ? "#72559c" : "#2a2c39"
                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 8
                                        RowLayout {
                                            CheckBox {
                                                checked: modelData.selected
                                                enabled: root.idle
                                                onToggled: recorder.selectAudio(modelData.id, checked)
                                            }
                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 3
                                                Text {
                                                    Layout.fillWidth: true
                                                    text: modelData.name
                                                    color: "#e0dce9"
                                                    font.pixelSize: 12
                                                    elide: Text.ElideRight
                                                }
                                                Caption {
                                                    Layout.fillWidth: true
                                                    text: modelData.detail
                                                    elide: Text.ElideRight
                                                    font.pixelSize: 9
                                                }
                                            }
                                        }
                                        RowLayout {
                                            Slider {
                                                id: gain
                                                Layout.fillWidth: true
                                                from: 0
                                                to: 2
                                                value: modelData.gain
                                                enabled: modelData.selected
                                                onMoved: recorder.setAudioGain(modelData.id, value)
                                            }
                                            Caption {
                                                text: Math.round(gain.value * 100) + "%"
                                                Layout.preferredWidth: 33
                                                font.pixelSize: 10
                                            }
                                            ActionButton {
                                                id: mute
                                                property bool muted: modelData.muted
                                                text: muted ? "뮤트됨" : "뮤트"
                                                danger: muted
                                                implicitWidth: 55
                                                implicitHeight: 28
                                                font.pixelSize: 10
                                                enabled: modelData.selected
                                                onClicked: {
                                                    muted = !muted;
                                                    recorder.setAudioMuted(modelData.id, muted);
                                                }
                                            }
                                        }
                                        Rectangle {
                                            Layout.fillWidth: true
                                            height: 3
                                            radius: 2
                                            color: "#34303f"
                                            Rectangle {
                                                height: 3
                                                radius: 2
                                                width: parent.width * Math.min(1, recorder.audioLevels[modelData.id] || 0)
                                                color: width > parent.width * 0.95 ? "#f47b88" : "#a6dabb"
                                            }
                                        }
                                    }
                                }
                                Text {
                                    visible: parent.count === 0
                                    width: parent.width
                                    anchors.centerIn: parent
                                    text: "소스를 새로고침하면\n오디오 장치와 앱이 표시됩니다."
                                    horizontalAlignment: Text.AlignHCenter
                                    color: "#6e7186"
                                    font.pixelSize: 11
                                    lineHeight: 1.4
                                }
                            }
                            Caption {
                                Layout.fillWidth: true
                                text: "중복 방지: 전체 소리 또는 앱별 소리 중 하나를 선택합니다."
                                font.pixelSize: 9
                                color: "#9295a9"
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                    Panel {
                        Layout.fillWidth: true
                        implicitHeight: 144
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 16
                            spacing: 10
                            RowLayout {
                                Caption {
                                    text: "저장 위치"
                                }
                                Item {
                                    Layout.fillWidth: true
                                }
                                Text {
                                    Layout.maximumWidth: 180
                                    text: recorder.settings.output
                                    color: "#b5accb"
                                    font.pixelSize: 10
                                    elide: Text.ElideLeft
                                }
                                ToolButton {
                                    text: "⋯"
                                    enabled: root.idle
                                    onClicked: folderDialog.open()
                                }
                            }
                            RowLayout {
                                Text {
                                    text: recorder.elapsed
                                    color: recorder.state === "recording" ? "#f492a3" : "#eee8fa"
                                    font.family: "Consolas"
                                    font.pixelSize: 23
                                }
                                Item {
                                    Layout.fillWidth: true
                                }
                                ActionButton {
                                    Layout.fillWidth: true
                                    primary: recorder.state !== "recording"
                                    danger: recorder.state === "recording"
                                    text: recorder.state === "recording" ? "녹화 종료" : recorder.state === "countdown" ? "시작 취소" : recorder.state === "starting" ? "준비 중…" : recorder.state === "saving" ? "저장 중…" : recorder.state === "editing" ? "편집 중…" : "녹화 시작"
                                    glyph: recorder.state === "recording" ? "■" : "●"
                                    enabled: ["idle", "recording", "countdown"].indexOf(recorder.state) >= 0
                                    onClicked: recorder.toggleRecording()
                                }
                            }
                            Caption {
                                text: "녹화 후 미리보기 · 자르기 · 분리 오디오 저장"
                                font.pixelSize: 10
                            }
                        }
                    }
                }
            }
        }
    }

    Window {
        id: region
        flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool
        color: "#773a3055"
        property string displayId: ""
        property real scaleFactor: 1
        property real startX: 0
        property real startY: 0
        property real endX: 0
        property real endY: 0
        onVisibleChanged: {
            if (visible) {
                startX = 0;
                startY = 0;
                endX = 0;
                endY = 0;
            }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 40
            text: "드래그해서 녹화 영역 선택  ·  Esc 취소"
            color: "white"
            font.pixelSize: 22
            font.weight: Font.DemiBold
        }
        Rectangle {
            x: Math.min(region.startX, region.endX)
            y: Math.min(region.startY, region.endY)
            width: Math.abs(region.endX - region.startX)
            height: Math.abs(region.endY - region.startY)
            color: "#11333333"
            border.width: 2
            border.color: "#d7b9ff"
            Text {
                x: 8
                y: 8
                text: Math.round(parent.width * region.scaleFactor) + " × " + Math.round(parent.height * region.scaleFactor)
                color: "white"
            }
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.CrossCursor
            onPressed: function (mouse) {
                region.startX = mouse.x;
                region.startY = mouse.y;
                region.endX = mouse.x;
                region.endY = mouse.y;
            }
            onPositionChanged: function (mouse) {
                if (pressed) {
                    region.endX = mouse.x;
                    region.endY = mouse.y;
                }
            }
            onReleased: {
                region.visible = false;
                recorder.selectRegion(region.displayId, Math.min(region.startX, region.endX), Math.min(region.startY, region.endY), Math.abs(region.endX - region.startX), Math.abs(region.endY - region.startY), region.scaleFactor);
            }
        }
        Shortcut {
            sequence: "Escape"
            enabled: region.visible
            onActivated: region.visible = false
        }
    }

    Dialog {
        id: editor
        anchors.centerIn: parent
        width: Math.min(root.width - 80, 1000)
        height: Math.min(root.height - 60, 800)
        modal: true
        title: "녹화 완료 · 간단 편집"
        background: Rectangle {
            color: "#191b24"
            radius: 16
            border.color: "#454053"
        }
        closePolicy: recorder.state === "editing" ? Popup.NoAutoClose : Popup.CloseOnEscape
        onOpened: {
            player.source = recorder.lastFile;
            inSlider.value = 0;
            outSlider.value = 1;
        }
        onClosed: {
            player.stop();
            player.source = "";
        }
        MediaPlayer {
            id: player
            objectName: "editorPlayer"
            videoOutput: videoOutput
            property bool warmingPreview: false
            property bool previewWarmed: false
            audioOutput: AudioOutput {
                volume: 0.7
                muted: player.warmingPreview
            }
            onSourceChanged: {
                previewWarmed = false;
                warmingPreview = false;
                previewWarmup.stop();
            }
            onMediaStatusChanged: if (mediaStatus === MediaPlayer.LoadedMedia && !previewWarmed) {
                previewWarmed = true;
                warmingPreview = true;
                play();
                previewWarmup.restart();
            }
        }
        Timer {
            id: previewWarmup
            interval: 180
            onTriggered: {
                player.pause();
                player.setPosition(0);
                player.warmingPreview = false;
            }
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: 14
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#090a10"
                radius: 10
                VideoOutput {
                    id: videoOutput
                    anchors.fill: parent
                    fillMode: VideoOutput.PreserveAspectFit
                }
            }
            RowLayout {
                ActionButton {
                    text: player.playbackState === MediaPlayer.PlayingState ? "일시정지" : "재생"
                    onClicked: player.playbackState === MediaPlayer.PlayingState ? player.pause() : player.play()
                }
                Slider {
                    Layout.fillWidth: true
                    from: 0
                    to: Math.max(1, player.duration)
                    value: player.position
                    onMoved: player.setPosition(value)
                }
                Caption {
                    text: (player.position / 1000).toFixed(1) + " / " + (player.duration / 1000).toFixed(1) + "초"
                }
            }
            RowLayout {
                Caption {
                    text: "시작"
                }
                Slider {
                    id: inSlider
                    objectName: "trimIn"
                    Layout.fillWidth: true
                    from: 0
                    to: 1
                    value: 0
                    onMoved: {
                        if (value >= outSlider.value)
                            value = Math.max(0, outSlider.value - 0.001);
                        player.setPosition(value * player.duration);
                    }
                }
                Field {
                    implicitWidth: 90
                    text: (inSlider.value * player.duration / 1000).toFixed(2)
                    validator: DoubleValidator {
                        bottom: 0
                    }
                    onEditingFinished: inSlider.value = Math.max(0, Math.min(Number(text) * 1000 / Math.max(1, player.duration), outSlider.value - 0.001))
                }
                Caption {
                    text: "초"
                }
            }
            RowLayout {
                Caption {
                    text: "종료"
                }
                Slider {
                    id: outSlider
                    objectName: "trimOut"
                    Layout.fillWidth: true
                    from: 0
                    to: 1
                    value: 1
                    onMoved: {
                        if (value <= inSlider.value)
                            value = Math.min(1, inSlider.value + 0.001);
                        player.setPosition(value * player.duration);
                    }
                }
                Field {
                    implicitWidth: 90
                    text: (outSlider.value * player.duration / 1000).toFixed(2)
                    validator: DoubleValidator {
                        bottom: 0
                    }
                    onEditingFinished: outSlider.value = Math.min(1, Math.max(Number(text) * 1000 / Math.max(1, player.duration), inSlider.value + 0.001))
                }
                Caption {
                    text: "초"
                }
            }
            RowLayout {
                Caption {
                    Layout.fillWidth: true
                    text: "선택 구간 " + ((outSlider.value - inSlider.value) * player.duration / 1000).toFixed(2) + "초  ·  모든 오디오 트랙 유지"
                }
                ActionButton {
                    text: "닫기"
                    enabled: root.idle
                    onClicked: editor.close()
                }
                ActionButton {
                    text: recorder.state === "editing" ? "저장 중…" : "자르고 저장"
                    primary: true
                    enabled: root.idle && player.duration > 0
                    onClicked: saveChoice.open()
                }
            }
        }
    }
    Dialog {
        id: saveChoice
        objectName: "saveChoice"
        anchors.centerIn: parent
        modal: true
        width: 510
        title: "편집본을 어떻게 저장할까요?"
        property real startSeconds: 0
        property real endSeconds: 0
        onOpened: {
            startSeconds = inSlider.value * player.duration / 1000;
            endSeconds = outSlider.value * player.duration / 1000;
            saveNewButton.forceActiveFocus();
        }
        background: Rectangle {
            color: "#22212d"
            radius: 14
            border.color: "#5c4d75"
        }
        contentItem: ColumnLayout {
            spacing: 20
            Text {
                Layout.fillWidth: true
                text: "원본 덮어쓰기를 선택하면 녹화 영상이 편집본으로 교체됩니다.\n분리된 WAV와 video.mkv 복구 원본은 유지됩니다."
                color: "#c7bfd7"
                wrapMode: Text.Wrap
                font.pixelSize: 13
            }
            RowLayout {
                ActionButton {
                    text: "취소"
                    onClicked: saveChoice.close()
                }
                Item {
                    Layout.fillWidth: true
                }
                ActionButton {
                    objectName: "saveOverwrite"
                    text: "원본 덮어쓰기"
                    danger: true
                    onClicked: {
                        player.stop();
                        player.source = "";
                        saveChoice.close();
                        recorder.exportTrim(saveChoice.startSeconds, saveChoice.endSeconds, true);
                    }
                }
                ActionButton {
                    id: saveNewButton
                    objectName: "saveNew"
                    text: "새 파일로 저장"
                    primary: true
                    focus: true
                    onClicked: {
                        player.stop();
                        saveChoice.close();
                        recorder.exportTrim(saveChoice.startSeconds, saveChoice.endSeconds, false);
                    }
                }
            }
        }
    }
    Dialog {
        id: closeDialog
        anchors.centerIn: parent
        modal: true
        title: "작업을 마친 뒤 종료할까요?"
        width: 460
        background: Rectangle {
            color: "#22212d"
            radius: 14
            border.color: "#5c4d75"
        }
        contentItem: ColumnLayout {
            spacing: 20
            Caption {
                text: "녹화 중이면 종료하고 파일 저장을 마친 뒤 앱을 닫습니다."
            }
            RowLayout {
                ActionButton {
                    text: "계속 사용"
                    onClicked: closeDialog.close()
                }
                ActionButton {
                    text: "저장 후 종료"
                    primary: true
                    onClicked: {
                        root.closeAfterSave = true;
                        if (recorder.state === "recording" || recorder.state === "countdown")
                            recorder.toggleRecording();
                        closeDialog.close();
                    }
                }
            }
        }
    }
}
