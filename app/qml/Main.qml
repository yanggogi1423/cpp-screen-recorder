import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtMultimedia

ApplicationWindow {
    id: root
    property bool expanded: recorder.expanded
    property bool optimized: recorder.optimized
    property rect normalBounds: Qt.rect(0, 0, 780, 780)
    property bool restoreMaximized: false
    property int previewScreenWidth: 0
    property int previewScreenHeight: 0
    property int desktopWidth: previewScreenWidth || Screen.width
    property int desktopHeight: previewScreenHeight || Screen.height
    width: Math.min(expanded ? 1160 : 780, desktopWidth - 40)
    height: Math.min(780, desktopHeight - 72)
    minimumWidth: optimized ? 400 : Math.min(expanded ? 1080 : 700, desktopWidth - 40)
    minimumHeight: optimized ? 104 : Math.min(640, desktopHeight - 72)
    visible: true
    flags: Qt.platform.os === "windows" ? Qt.Window | Qt.FramelessWindowHint : Qt.Window
    title: "FARecorder"
    color: "#141414"
    font.family: "D2Coding"
    palette.window: "#1c1c1c"
    palette.base: "#151515"
    palette.text: "#eeeeee"
    palette.windowText: "#eeeeee"
    palette.buttonText: "#eeeeee"
    palette.button: "#292929"
    palette.highlight: "#bbbbbb"
    palette.highlightedText: "#111111"
    property bool idle: recorder.state === "idle"
    property bool canChoose: idle && !recorder.audioTestActive
    property bool activeRecording: recorder.state === "recording" || recorder.state === "paused"
    property bool closeAfterSave: false
    property int captureTestTick: -1
    property int mode: 0
    onExpandedChanged: {
        if (!optimized)
            width = Math.min(expanded ? 1160 : 780, desktopWidth - 40);
    }
    onOptimizedChanged: {
        if (optimized) {
            restoreMaximized = visibility === Window.Maximized;
            if (restoreMaximized)
                showNormal();
            normalBounds = Qt.rect(x, y, width, height);
            // Let the minimum-size bindings update before shrinking the window.
            Qt.callLater(function () {
                if (root.optimized) {
                    root.width = 400;
                    root.height = 104;
                }
            });
        } else {
            width = Math.min(normalBounds.width, desktopWidth - 40);
            height = Math.min(normalBounds.height, desktopHeight - 72);
            x = normalBounds.x;
            y = normalBounds.y;
            if (restoreMaximized)
                showMaximized();
        }
    }
    property var selectedRoutes: recorder.audio.filter(function (a) {
        return a.selected;
    })
    function kindRoutes(mic) {
        return recorder.audio.filter(function (a) {
            return mic ? a.kind === 1 : a.kind !== 1;
        });
    }
    function kindSelected(mic) {
        return selectedRoutes.filter(function (a) {
            return mic ? a.kind === 1 : a.kind !== 1;
        });
    }
    function setKind(mic, enabled) {
        var chosen = kindSelected(mic);
        if (activeRecording || recorder.audioTestActive) {
            for (var i = 0; i < chosen.length; i++)
                recorder.setAudioMuted(chosen[i].id, !enabled);
            return;
        }
        if (!enabled) {
            for (var j = 0; j < chosen.length; j++)
                recorder.selectAudio(chosen[j].id, false);
        } else {
            if (chosen.length) {
                for (var k = 0; k < chosen.length; k++)
                    recorder.setAudioMuted(chosen[k].id, false);
                return;
            }
            var all = kindRoutes(mic);
            var target = all.find(function (a) {
                return mic || a.kind === 2;
            });
            if (!target) {
                recorder.refresh();
                all = kindRoutes(mic);
                target = all.find(function (a) {
                    return mic || a.kind === 2;
                });
            }
            if (target) {
                recorder.selectAudio(target.id, true);
                recorder.setAudioMuted(target.id, false);
            }
        }
    }
    function kindAudible(mic) {
        return kindSelected(mic).some(function (a) {
            return !(recorder.mixStates[a.id] || {}).muted;
        });
    }
    function kindLevel(mic) {
        var result = 0;
        var a = kindSelected(mic);
        for (var i = 0; i < a.length; i++)
            result = Math.max(result, recorder.audioLevels[a[i].id] || 0);
        return result;
    }

    component Caption: Text {
        color: "#979797"
        font.pixelSize: 11
    }
    component Heading: Text {
        color: "#eeeeee"
        font.pixelSize: 14
        font.weight: Font.DemiBold
    }
    component Select: ComboBox {
        id: select
        implicitHeight: 36
        implicitWidth: 120
        font.pixelSize: 12
        leftPadding: 10
        rightPadding: 28
        background: Rectangle {
            radius: 7
            color: "#202020"
            border.color: select.activeFocus ? "#aaaaaa" : "#383838"
        }
        contentItem: Text {
            text: select.displayText
            color: "#e2e2e2"
            font: select.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            leftPadding: 0
            rightPadding: 0
        }
        popup: Popup {
            y: select.height + 4
            width: select.width
            padding: 5
            implicitHeight: Math.min(contentItem.implicitHeight + 10, 300)
            background: Rectangle {
                color: "#262626"
                radius: 8
                border.color: "#4b4b4b"
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
            palette.text: "#eeeeee"
        }
    }
    component AudioSwitch: Rectangle {
        id: audioSwitch
        property bool mic: false
        Layout.fillWidth: true
        implicitHeight: 63
        color: "#1d1d1d"
        radius: 8
        border.color: "#333333"
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 5
            RowLayout {
                Text {
                    text: audioSwitch.mic ? "마이크" : "컴퓨터 소리"
                    color: "#dddddd"
                    font.pixelSize: 12
                    Layout.fillWidth: true
                }
                Switch {
                    id: audioToggle
                    implicitHeight: 26
                    implicitWidth: 42
                    padding: 0
                    indicator: Rectangle {
                        width: 40
                        height: 22
                        radius: 11
                        y: 2
                        color: audioToggle.checked ? "#d4d4d4" : "#333333"
                        border.color: "#5a5a5a"
                        Rectangle {
                            width: 16
                            height: 16
                            radius: 8
                            y: 3
                            x: audioToggle.checked ? 21 : 3
                            color: audioToggle.checked ? "#202020" : "#8a8a8a"
                            Behavior on x {
                                NumberAnimation {
                                    duration: 100
                                }
                            }
                        }
                    }
                    checked: root.kindAudible(audioSwitch.mic)
                    enabled: root.idle || ((root.activeRecording || recorder.audioTestActive) && root.kindSelected(audioSwitch.mic).length > 0)
                    onToggled: root.setKind(audioSwitch.mic, checked)
                }
            }
            Meter {
                Layout.fillWidth: true
                level: root.kindLevel(audioSwitch.mic)
            }
        }
    }

    Shortcut {
        sequence: "Ctrl+Shift+R"
        enabled: !recorder.globalHotkeysAvailable
        onActivated: recorder.toggleRecording()
    }
    Shortcut {
        sequence: "Ctrl+Shift+P"
        enabled: !recorder.globalHotkeysAvailable
        onActivated: recorder.togglePause()
    }
    Shortcut {
        sequence: "Escape"
        enabled: recorder.state === "countdown" || recorder.state === "checking"
        onActivated: recorder.toggleRecording()
    }
    onClosing: function (close) {
        if (recorder.state !== "idle") {
            close.accepted = false;
            recorder.setOptimized(false);
            closeDialog.open();
        }
    }
    Connections {
        target: recorder
        function onEditorRequested() {
            testPlayer.stop();
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
            if (root.closeAfterSave && (recorder.state === "recording" || recorder.state === "paused"))
                recorder.toggleRecording();
            if (root.closeAfterSave && recorder.state === "idle")
                Qt.quit();
            if (recorder.state === "idle" && editor.opened && player.source.toString().length === 0)
                player.source = recorder.lastFile;
            if (recorder.state !== "idle")
                testPlayer.stop();
        }
    }
    FolderDialog {
        id: folderDialog
        title: "녹화 저장 폴더"
        currentFolder: recorder.outputFolder
        onAccepted: recorder.setOutputFolder(selectedFolder)
    }
    FileDialog {
        id: recordingDialog
        title: "편집할 영상"
        nameFilters: ["영상 파일 (*.mkv *.mp4 *.mov)"]
        onAccepted: recorder.openRecording(selectedFile)
    }
    MediaPlayer {
        id: testPlayer
        objectName: "audioCheckPlayer"
        source: recorder.audioSample
        audioOutput: AudioOutput {
            volume: 0.6
        }
    }

    Rectangle {
        id: titlebar
        objectName: "titlebar"
        visible: !root.optimized
        anchors.top: parent.top
        width: parent.width
        height: 48
        color: "#191919"
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: "#303030"
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 7
            spacing: 9
            Image {
                source: "qrc:/assets/app-icon.png"
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
                mipmap: true
            }
            Text {
                text: "FARecorder"
                color: "#ededed"
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                MouseArea {
                    anchors.fill: parent
                    onPressed: root.startSystemMove()
                    onDoubleClicked: root.visibility === Window.Maximized ? root.showNormal() : root.showMaximized()
                }
            }
            ActionButton {
                visible: root.activeRecording
                text: "최적 모드"
                implicitHeight: 29
                font.pixelSize: 11
                onClicked: recorder.setOptimized(true)
            }
            ActionButton {
                objectName: "modeButton"
                text: root.expanded ? "간소화" : "확장"
                implicitHeight: 29
                implicitWidth: 65
                font.pixelSize: 11
                onClicked: recorder.setExpanded(!root.expanded)
            }
            ToolButton {
                visible: Qt.platform.os === "windows"
                text: "-"
                implicitWidth: 32
                onClicked: root.showMinimized()
            }
            ToolButton {
                visible: Qt.platform.os === "windows"
                text: "□"
                implicitWidth: 32
                onClicked: root.visibility === Window.Maximized ? root.showNormal() : root.showMaximized()
            }
            ToolButton {
                visible: Qt.platform.os === "windows"
                text: "×"
                font.pixelSize: 20
                implicitWidth: 32
                onClicked: root.close()
            }
        }
    }
    MenuBar {
        id: mainMenu
        objectName: "mainMenu"
        anchors.top: titlebar.bottom
        width: parent.width
        visible: !root.optimized
        background: Rectangle {
            color: "#191919"
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: "#303030"
            }
        }
        Menu {
            title: "파일"
            Action {
                text: "녹화 폴더 지정…"
                enabled: root.idle
                onTriggered: folderDialog.open()
            }
            Action {
                text: "녹화 폴더 열기"
                onTriggered: recorder.openOutputFolder()
            }
            MenuSeparator {}
            Action {
                text: "종료"
                onTriggered: root.close()
            }
        }
        Menu {
            title: "편집"
            Action {
                text: "영상 열고 자르기…"
                enabled: root.idle
                onTriggered: recordingDialog.open()
            }
            Action {
                text: "최근 영상 편집"
                enabled: root.idle && recorder.lastFile.toString().length > 0
                onTriggered: editor.open()
            }
        }
        Menu {
            title: "도움말"
            Action {
                objectName: "aboutAction"
                text: "FARecorder 정보"
                onTriggered: aboutDialog.open()
            }
            Action {
                text: "GitHub 저장소"
                onTriggered: Qt.openUrlExternally(recorder.appInfo.repository)
            }
            Action {
                text: "문제 제보"
                onTriggered: Qt.openUrlExternally(recorder.appInfo.repository + "/issues")
            }
        }
    }
    Rectangle {
        objectName: "compactContent"
        visible: root.optimized
        anchors.fill: parent
        color: "#191919"
        border.color: "#3b3b3b"
        radius: Qt.platform.os === "windows" ? 10 : 0
        // Moving pixels in the self-capture fixture verify fresh WGC frames.
        Rectangle {
            visible: root.captureTestTick >= 0
            x: 4 + (root.captureTestTick * 3) % 100
            y: parent.height - 5
            width: 4
            height: 4
            color: root.captureTestTick % 2 ? "#e46660" : "#a1b9ae"
        }
        RowLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 5
                RowLayout {
                    Rectangle {
                        width: 6
                        height: 6
                        radius: 3
                        color: recorder.state === "paused" ? "#d1b57c" : "#ef5751"
                    }
                    Caption {
                        text: "FARecorder · " + (recorder.state === "paused" ? "일시정지" : "녹화 중")
                        font.pixelSize: 10
                    }
                }
                Text {
                    text: recorder.elapsed
                    color: "#f0f0f0"
                    font.family: root.font.family
                    font.pixelSize: 27
                }
                MouseArea {
                    anchors.fill: parent
                    onPressed: root.startSystemMove()
                }
            }
            TransportButton {
                objectName: "compactPause"
                symbol: recorder.state === "paused" ? "resume" : "pause"
                text: recorder.state === "paused" ? "녹화 재개" : "일시정지"
                onClicked: recorder.togglePause()
            }
            TransportButton {
                objectName: "compactStop"
                symbol: "stop"
                text: "녹화 종료 및 저장"
                onClicked: recorder.toggleRecording()
            }
            ToolButton {
                objectName: "compactRestore"
                text: "↗"
                font.pixelSize: 21
                implicitWidth: 30
                Accessible.name: "원래 화면으로 돌아가기"
                ToolTip.visible: hovered
                ToolTip.text: "원래 화면 · 녹화용 믹서"
                onClicked: recorder.setOptimized(false)
            }
        }
    }
    Dialog {
        id: aboutDialog
        objectName: "aboutDialog"
        anchors.centerIn: parent
        width: Math.min(420, root.width - 40)
        title: "FARecorder 정보"
        modal: true
        footer: DialogButtonBox {
            Button {
                text: "닫기"
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
        }
        background: Rectangle {
            color: "#202020"
            radius: 12
            border.color: "#454545"
        }
        ColumnLayout {
            width: parent.width
            spacing: 16
            Image {
                source: "qrc:/assets/app-icon.png"
                Layout.preferredWidth: 64
                Layout.preferredHeight: 64
                mipmap: true
            }
            Heading {
                text: recorder.appInfo.name
            }
            Caption {
                text: "버전  " + recorder.appInfo.version + "\n제작자  " + recorder.appInfo.author + "\n마지막 업데이트  " + recorder.appInfo.updated
                lineHeight: 1.7
                color: "#c5c5c5"
            }
            Caption {
                Layout.fillWidth: true
                text: "C++ · Qt · Windows / macOS\n녹화용 음량 조절은 운영체제 음량과 별도로 적용됩니다."
                wrapMode: Text.Wrap
                lineHeight: 1.5
            }
            ActionButton {
                text: "GitHub 저장소 열기"
                onClicked: Qt.openUrlExternally(recorder.appInfo.repository)
            }
        }
    }
    RowLayout {
        objectName: "normalContent"
        visible: !root.optimized
        anchors {
            top: mainMenu.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        spacing: 0
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 20
            spacing: 12
            RowLayout {
                Text {
                    text: "화면 녹화"
                    color: "#f2f2f2"
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                }
                Item {
                    Layout.fillWidth: true
                }
                Rectangle {
                    width: 7
                    height: 7
                    radius: 4
                    color: recorder.state === "recording" ? "#ef5751" : recorder.state === "paused" ? "#d1b57c" : "#777777"
                }
                Caption {
                    text: ({
                            idle: "대기",
                            checking: "인코더 확인",
                            preparing: "준비 중",
                            countdown: "시작 대기",
                            starting: "연결 중",
                            recording: "녹화 중",
                            paused: "일시정지",
                            saving: "저장 중",
                            editing: "편집 중"
                        })[recorder.state] || recorder.state
                    color: "#c0c0c0"
                }
            }
            Rectangle {
                visible: recorder.message.length > 0
                Layout.fillWidth: true
                implicitHeight: alertText.implicitHeight + 24
                color: "#302322"
                radius: 7
                border.color: "#6b4440"
                Text {
                    id: alertText
                    anchors {
                        left: parent.left
                        right: closeAlert.left
                        verticalCenter: parent.verticalCenter
                        margins: 12
                    }
                    text: recorder.message
                    color: "#efb4ad"
                    font.pixelSize: 11
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
                spacing: 6
                Repeater {
                    model: ["전체 화면", "특정 창", "영역"]
                    delegate: ActionButton {
                        required property int index
                        required property string modelData
                        text: modelData
                        primary: root.mode === index
                        implicitWidth: index === 2 ? 58 : 83
                        implicitHeight: 33
                        enabled: root.canChoose
                        onClicked: root.mode = index
                    }
                }
                Item {
                    Layout.fillWidth: true
                }
                ActionButton {
                    text: "새로고침"
                    implicitHeight: 33
                    implicitWidth: 82
                    font.pixelSize: 11
                    enabled: root.canChoose
                    onClicked: recorder.refresh()
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Select {
                    id: displayCombo
                    Layout.fillWidth: true
                    visible: root.mode !== 1
                    model: recorder.displays
                    textRole: "name"
                    valueRole: "id"
                    enabled: root.canChoose
                }
                Select {
                    id: windowCombo
                    Layout.fillWidth: true
                    visible: root.mode === 1
                    model: recorder.windows
                    textRole: "name"
                    valueRole: "id"
                    enabled: root.canChoose
                }
                ActionButton {
                    text: root.mode === 2 ? "영역 선택" : "연결"
                    implicitHeight: 36
                    implicitWidth: 80
                    enabled: root.canChoose && (root.mode === 1 ? windowCombo.count > 0 : displayCombo.count > 0)
                    onClicked: root.mode === 2 ? recorder.beginRegion(displayCombo.currentValue) : recorder.connectSource(root.mode === 1 ? windowCombo.currentValue : displayCombo.currentValue, root.mode === 1)
                }
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 80
                color: "#0a0a0a"
                radius: 9
                border.color: "#343434"
                clip: true
                Image {
                    anchors.fill: parent
                    anchors.margins: 1
                    visible: recorder.hasSource
                    source: recorder.hasSource ? "image://preview/" + recorder.previewRevision : ""
                    cache: false
                    fillMode: Image.PreserveAspectFit
                }
                Column {
                    visible: !recorder.hasSource
                    anchors.centerIn: parent
                    spacing: 12
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 44
                        height: 30
                        radius: 3
                        color: "transparent"
                        border.color: "#656565"
                        border.width: 2
                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: 35
                            width: 15
                            height: 2
                            color: "#656565"
                        }
                    }
                    Caption {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "녹화할 화면을 선택하세요"
                        font.pixelSize: 13
                        color: "#b0b0b0"
                    }
                }
                Rectangle {
                    visible: root.captureTestTick >= 0
                    width: 100
                    height: 42
                    x: (root.captureTestTick * 7) % Math.max(1, parent.width - 100)
                    y: 16
                    color: root.captureTestTick % 2 ? "#e46660" : "#a1b9ae"
                    Text {
                        anchors.centerIn: parent
                        text: root.captureTestTick
                        font.pixelSize: 20
                    }
                }
                Rectangle {
                    anchors.fill: parent
                    visible: recorder.state === "countdown"
                    color: "#dd101010"
                    Text {
                        anchors.centerIn: parent
                        text: recorder.countdown
                        color: "#eeeeee"
                        font.pixelSize: 76
                        font.weight: Font.Light
                    }
                }
                Rectangle {
                    visible: recorder.state === "paused"
                    anchors.centerIn: parent
                    width: 138
                    height: 38
                    radius: 19
                    color: "#dd282828"
                    Row {
                        anchors.centerIn: parent
                        spacing: 8
                        Image {
                            source: "qrc:/assets/pause.png"
                            width: 19
                            height: 19
                            mipmap: true
                        }
                        Text {
                            text: "일시정지"
                            color: "#eeeeee"
                            font.pixelSize: 13
                        }
                    }
                }
            }
            RowLayout {
                Caption {
                    Layout.fillWidth: true
                    text: recorder.hasSource ? recorder.sourceName : "선택한 소스 없음"
                    elide: Text.ElideRight
                }
                Caption {
                    text: recorder.settings.width + " × " + recorder.settings.height + " · " + recorder.settings.fps + " fps"
                }
            }
            RowLayout {
                visible: !root.expanded
                Layout.fillWidth: true
                Caption {
                    text: "화질"
                }
                Select {
                    objectName: "simpleQuality"
                    Layout.fillWidth: true
                    model: ["1080p", "1440p", "4K", "720p", "직접 설정…"]
                    enabled: root.idle
                    currentIndex: recorder.settings.width === 1920 && recorder.settings.height === 1080 ? 0 : recorder.settings.width === 2560 && recorder.settings.height === 1440 ? 1 : recorder.settings.width === 3840 && recorder.settings.height === 2160 ? 2 : recorder.settings.width === 1280 && recorder.settings.height === 720 ? 3 : 4
                    displayText: currentIndex === 4 ? recorder.settings.width + " × " + recorder.settings.height : currentText
                    onActivated: {
                        var widths = [1920, 2560, 3840, 1280];
                        var heights = [1080, 1440, 2160, 720];
                        if (currentIndex === 4) {
                            recorder.setExpanded(true);
                            return;
                        }
                        recorder.setSetting("width", widths[currentIndex]);
                        recorder.setSetting("height", heights[currentIndex]);
                    }
                }
                Select {
                    objectName: "simpleFps"
                    implicitWidth: 105
                    property var values: [30, 60, 120].indexOf(recorder.settings.fps) < 0 ? [30, 60, 120, recorder.settings.fps] : [30, 60, 120]
                    model: values.map(function (v) {
                        return v + " fps";
                    })
                    enabled: root.idle
                    currentIndex: values.indexOf(recorder.settings.fps)
                    onActivated: recorder.setSetting("fps", values[currentIndex])
                }
                Caption {
                    text: "시작"
                }
                Select {
                    objectName: "simpleDelay"
                    implicitWidth: 90
                    property var values: [0, 3, 5].indexOf(recorder.settings.delay) < 0 ? [0, 3, 5, recorder.settings.delay] : [0, 3, 5]
                    model: values.map(function (v) {
                        return v === 0 ? "즉시" : v + "초 후";
                    })
                    enabled: root.idle
                    currentIndex: values.indexOf(recorder.settings.delay)
                    onActivated: recorder.setSetting("delay", values[currentIndex])
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                AudioSwitch {
                    mic: true
                }
                AudioSwitch {
                    mic: false
                }
            }
            RowLayout {
                Layout.fillWidth: true
                ActionButton {
                    objectName: "audioTestButton"
                    text: recorder.audioTestActive ? "테스트 중지" : "소리 테스트"
                    implicitHeight: 32
                    font.pixelSize: 11
                    enabled: root.idle || recorder.audioTestActive
                    onClicked: {
                        testPlayer.stop();
                        recorder.toggleAudioTest();
                    }
                }
                ActionButton {
                    objectName: "samplePlayButton"
                    text: testPlayer.playbackState === MediaPlayer.PlayingState ? "듣기 중지" : "샘플 듣기"
                    implicitHeight: 32
                    font.pixelSize: 11
                    enabled: root.idle && !recorder.audioTestActive && recorder.audioSample.toString().length > 0
                    onClicked: testPlayer.playbackState === MediaPlayer.PlayingState ? testPlayer.stop() : testPlayer.play()
                }
                Caption {
                    Layout.fillWidth: true
                    text: recorder.audioTestActive ? "5초 테스트 · " + recorder.audioTestProgress + "%" : recorder.audioSample.toString().length ? recorder.audioTestStatus : selectedRoutes.length ? selectedRoutes.length + "개 소스 · 녹화 음량만 조절" : "마이크·컴퓨터 소리를 선택하세요"
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignRight
                }
            }
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: "#343434"
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 15
                ColumnLayout {
                    spacing: 4
                    Text {
                        text: recorder.elapsed
                        color: "#f0f0f0"
                        font.family: root.font.family
                        font.pixelSize: 30
                        font.letterSpacing: 1
                    }
                    Caption {
                        text: recorder.state === "recording" ? recorder.throughput : recorder.state === "paused" ? "정지한 구간은 파일에서 제외" : recorder.encoderStatus === "passed" ? "인코더 준비됨" : "녹화 시작 전 인코더 자동 검사"
                        font.pixelSize: 10
                    }
                    Caption {
                        text: recorder.globalHotkeysAvailable ? (Qt.platform.os === "osx" ? "⌘⇧R 녹화 · ⌘⇧P 일시정지" : "Ctrl+Shift+R 녹화 · Ctrl+Shift+P 일시정지") : "전역 단축키 등록 실패 · 확장 화면에서 확인"
                        font.pixelSize: 9
                        color: recorder.globalHotkeysAvailable ? "#777777" : "#cfb27d"
                    }
                }
                Item {
                    Layout.fillWidth: true
                }
                TransportButton {
                    visible: root.activeRecording
                    symbol: recorder.state === "paused" ? "resume" : "pause"
                    text: recorder.state === "paused" ? "녹화 재개" : "일시정지"
                    onClicked: recorder.togglePause()
                }
                ColumnLayout {
                    spacing: 3
                    TransportButton {
                        objectName: "recordButton"
                        Layout.alignment: Qt.AlignHCenter
                        large: true
                        symbol: root.activeRecording ? "stop" : "record"
                        text: root.activeRecording ? "녹화 종료" : "녹화 시작"
                        enabled: ["idle", "recording", "paused", "countdown", "checking", "preparing"].indexOf(recorder.state) >= 0
                        onClicked: {
                            testPlayer.stop();
                            recorder.toggleRecording();
                        }
                    }
                    Caption {
                        Layout.alignment: Qt.AlignHCenter
                        text: root.activeRecording ? "정지" : recorder.state === "countdown" || recorder.state === "checking" ? "취소" : "녹화"
                        color: "#cccccc"
                        font.pixelSize: 10
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Caption {
                    Layout.fillWidth: true
                    text: recorder.settings.output
                    elide: Text.ElideLeft
                    font.pixelSize: 10
                    ToolTip.visible: folderHover.hovered
                    ToolTip.text: recorder.settings.output + "\n" + recorder.diskSpace
                    HoverHandler {
                        id: folderHover
                    }
                }
                ActionButton {
                    objectName: "chooseFolderButton"
                    text: "저장 위치…"
                    implicitHeight: 28
                    font.pixelSize: 10
                    enabled: root.idle
                    onClicked: folderDialog.open()
                }
                ActionButton {
                    text: "녹화 폴더"
                    implicitHeight: 28
                    font.pixelSize: 10
                    onClicked: recorder.openOutputFolder()
                }
                ActionButton {
                    text: "영상 편집"
                    implicitHeight: 28
                    font.pixelSize: 10
                    enabled: root.idle
                    onClicked: recorder.lastFile.toString().length ? editor.open() : recordingDialog.open()
                }
            }
        }
        Rectangle {
            visible: root.expanded
            Layout.fillHeight: true
            Layout.preferredWidth: 350
            color: "#1a1a1a"
            Rectangle {
                width: 1
                height: parent.height
                color: "#343434"
            }
            ScrollView {
                anchors.fill: parent
                anchors.margins: 20
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: parent.width
                    spacing: 16
                    RowLayout {
                        Heading {
                            text: "상세 설정"
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                        Caption {
                            text: "확장 모드"
                        }
                    }
                    RowLayout {
                        ColumnLayout {
                            Layout.fillWidth: true
                            Caption {
                                text: "너비"
                            }
                            Field {
                                Layout.fillWidth: true
                                text: String(recorder.settings.width)
                                enabled: root.idle
                                validator: IntValidator {
                                    bottom: 64
                                    top: 16384
                                }
                                onEditingFinished: recorder.setSetting("width", text)
                            }
                        }
                        Caption {
                            text: "×"
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Caption {
                                text: "높이"
                            }
                            Field {
                                Layout.fillWidth: true
                                text: String(recorder.settings.height)
                                enabled: root.idle
                                validator: IntValidator {
                                    bottom: 64
                                    top: 16384
                                }
                                onEditingFinished: recorder.setSetting("height", text)
                            }
                        }
                        ColumnLayout {
                            Layout.preferredWidth: 62
                            Caption {
                                text: "FPS"
                            }
                            Field {
                                Layout.fillWidth: true
                                text: String(recorder.settings.fps)
                                enabled: root.idle
                                validator: IntValidator {
                                    bottom: 1
                                    top: 240
                                }
                                onEditingFinished: recorder.setSetting("fps", text)
                            }
                        }
                    }
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
                    RowLayout {
                        Rectangle {
                            width: 6
                            height: 6
                            radius: 3
                            color: recorder.encoderStatus === "passed" ? "#a0c7ac" : recorder.encoderStatus === "failed" ? "#e06e66" : "#777777"
                        }
                        Caption {
                            Layout.fillWidth: true
                            text: ({
                                    unchecked: "검사 전",
                                    checking: "검사 중",
                                    passed: "인코딩 검사 통과",
                                    failed: "사용 불가"
                                })[recorder.encoderStatus]
                            color: "#b7b7b7"
                        }
                        ActionButton {
                            objectName: "encoderCheckButton"
                            text: "검사"
                            implicitWidth: 65
                            implicitHeight: 29
                            enabled: root.idle
                            onClicked: recorder.checkEncoder()
                        }
                    }
                    RowLayout {
                        ColumnLayout {
                            Layout.fillWidth: true
                            Caption {
                                text: "형식"
                            }
                            Select {
                                Layout.fillWidth: true
                                model: ["MP4", "MKV"]
                                currentIndex: recorder.settings.container === "mkv" ? 1 : 0
                                enabled: root.idle
                                onActivated: recorder.setSetting("container", currentText.toLowerCase())
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Caption {
                                text: "시작 지연 (초)"
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
                    }
                    CheckBox {
                        text: "마우스 커서 포함"
                        checked: recorder.settings.cursor
                        enabled: root.idle
                        onToggled: recorder.setSetting("cursor", checked)
                        font.pixelSize: 12
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: "#383838"
                    }
                    RowLayout {
                        Heading {
                            text: "녹화용 믹서"
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                        Caption {
                            text: "앱 · 장치별 트랙"
                        }
                    }
                    Caption {
                        Layout.fillWidth: true
                        text: "운영체제 음량은 바꾸지 않습니다.\n앱별 소리와 시스템 전체 소리는 중복 선택하지 않습니다."
                        wrapMode: Text.Wrap
                        lineHeight: 1.4
                        font.pixelSize: 10
                    }
                    Repeater {
                        model: recorder.audio
                        delegate: AudioRoute {
                            required property var modelData
                            Layout.fillWidth: true
                            sourceData: modelData
                            allowSelection: root.canChoose
                            allowMix: root.idle || root.activeRecording || recorder.audioTestActive
                        }
                    }
                    Caption {
                        visible: recorder.audio.length === 0
                        Layout.fillWidth: true
                        text: "새로고침하면 마이크와 앱이 표시됩니다.\n앱에서 소리를 재생한 뒤 새로고침해 주세요."
                        wrapMode: Text.Wrap
                        lineHeight: 1.4
                    }
                    Caption {
                        Layout.fillWidth: true
                        text: recorder.audioTestStatus
                        wrapMode: Text.Wrap
                        color: "#b6b6b6"
                        font.pixelSize: 10
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: "#383838"
                    }
                    Caption {
                        Layout.fillWidth: true
                        text: recorder.hotkeyStatus
                        wrapMode: Text.Wrap
                        lineHeight: 1.5
                        font.pixelSize: 10
                    }
                    RowLayout {
                        Caption {
                            Layout.fillWidth: true
                            text: recorder.settings.output
                            elide: Text.ElideLeft
                            font.pixelSize: 10
                        }
                        ActionButton {
                            text: "변경"
                            implicitHeight: 28
                            implicitWidth: 58
                            font.pixelSize: 10
                            enabled: root.idle
                            onClicked: folderDialog.open()
                        }
                    }
                    Item {
                        height: 4
                    }
                }
            }
        }
    }
    Repeater {
        model: Qt.platform.os === "windows" && !root.optimized && root.visibility !== Window.Maximized ? [Qt.LeftEdge, Qt.RightEdge, Qt.TopEdge, Qt.BottomEdge] : []
        delegate: MouseArea {
            required property int modelData
            x: modelData === Qt.RightEdge ? root.width - 5 : 0
            y: modelData === Qt.BottomEdge ? root.height - 5 : 0
            width: modelData === Qt.LeftEdge || modelData === Qt.RightEdge ? 5 : root.width
            height: modelData === Qt.TopEdge || modelData === Qt.BottomEdge ? 5 : root.height
            cursorShape: modelData === Qt.LeftEdge || modelData === Qt.RightEdge ? Qt.SizeHorCursor : Qt.SizeVerCursor
            onPressed: root.startSystemResize(modelData)
        }
    }

    Window {
        id: region
        flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool
        color: "#77333333"
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
            border.color: "#eeeeee"
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
            color: "#191919"
            radius: 16
            border.color: "#444444"
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
                color: "#090909"
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
            color: "#222222"
            radius: 14
            border.color: "#555555"
        }
        contentItem: ColumnLayout {
            spacing: 20
            Text {
                Layout.fillWidth: true
                text: "원본 덮어쓰기를 선택하면 녹화 영상이 편집본으로 교체됩니다.\n분리된 WAV와 video.mkv 복구 원본은 유지됩니다."
                color: "#c7c7c7"
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
            color: "#222222"
            radius: 14
            border.color: "#555555"
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
                        if (recorder.state === "recording" || recorder.state === "paused" || recorder.state === "countdown" || recorder.state === "checking")
                            recorder.toggleRecording();
                        closeDialog.close();
                    }
                }
            }
        }
    }
}
