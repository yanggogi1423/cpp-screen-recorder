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
    property bool changingGeometry: false
    function rememberNormalBounds() {
        Qt.callLater(function () {
            if (!root.changingGeometry && !root.optimized && root.visibility === Window.Windowed)
                root.normalBounds = Qt.rect(root.x, root.y, root.width, root.height);
        });
    }
    onXChanged: rememberNormalBounds()
    onYChanged: rememberNormalBounds()
    onWidthChanged: rememberNormalBounds()
    onHeightChanged: rememberNormalBounds()
    function restoreWindow() {
        var bounds = normalBounds;
        changingGeometry = true;
        showNormal();
        width = Math.min(bounds.width, desktopWidth - 40);
        height = Math.min(bounds.height, desktopHeight - 72);
        x = bounds.x;
        y = bounds.y;
        Qt.callLater(function () {
            root.changingGeometry = false;
        });
    }
    function toggleMaximized() {
        if (visibility === Window.Maximized) {
            restoreWindow();
        } else {
            normalBounds = Qt.rect(x, y, width, height);
            showMaximized();
        }
    }
    function moveWindow(point) {
        if (visibility === Window.Maximized) {
            var globalPoint = contentItem.mapToGlobal(point);
            var fraction = point.x / width;
            restoreWindow();
            x = globalPoint.x - width * fraction;
            y = globalPoint.y - Math.min(point.y, 22);
        }
        startSystemMove();
    }
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
    color: Qt.platform.os === "windows" ? "transparent" : Theme.window
    font.family: "D2Coding"
    palette.window: "#1c1c1c"
    palette.base: "#151515"
    palette.text: "#eeeeee"
    palette.windowText: "#eeeeee"
    palette.buttonText: "#eeeeee"
    palette.button: "#292929"
    palette.highlight: "#666666"
    palette.highlightedText: "#eeeeee"
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
            changingGeometry = true;
            restoreMaximized = visibility === Window.Maximized;
            if (!restoreMaximized)
                normalBounds = Qt.rect(x, y, width, height);
            if (restoreMaximized)
                showNormal();
            // Let the minimum-size bindings update before shrinking the window.
            Qt.callLater(function () {
                if (root.optimized) {
                    root.width = 400;
                    root.height = 104;
                }
            });
        } else {
            changingGeometry = true;
            width = Math.min(normalBounds.width, desktopWidth - 40);
            height = Math.min(normalBounds.height, desktopHeight - 72);
            x = normalBounds.x;
            y = normalBounds.y;
            if (restoreMaximized)
                showMaximized();
            Qt.callLater(function () {
                root.changingGeometry = false;
            });
        }
    }

    property string displaySelection: ""
    property string windowSelection: ""
    property var selectedRoutes: recorder.audio.filter(a => a.selected)
    function kindRoutes(mic) {
        return recorder.audio.filter(a => mic ? a.kind === 1 : a.kind !== 1);
    }
    function restoreCombo(combo, stored, display) {
        var list = display ? recorder.displays : recorder.windows;
        var position = list.findIndex(a => a.id === stored);
        if (!stored && list.length) {
            position = 0;
            if (display)
                displaySelection = list[0].id;
            else
                windowSelection = list[0].id;
        }
        combo.currentIndex = position;
    }
    Component.onCompleted: recorder.configureWindowChrome(root)
    onVisibilityChanged: Qt.callLater(function () {
        recorder.configureWindowChrome(root);
    })
    background: Rectangle {
        radius: root.visibility === Window.Maximized ? 0 : Theme.windowRadius
        color: Qt.platform.os === "windows" ? "#f2191919" : Theme.window
        border.color: root.visibility === Window.Maximized ? "transparent" : "#3b3b3b"
    }
    component Caption: Text {
        color: Theme.muted
        font.pixelSize: 11
    }
    component Heading: Text {
        color: Theme.text
        font.pixelSize: 14
        font.weight: Font.DemiBold
    }
    component Select: QuietCombo {}
    component Segment: TabButton {
        id: segment
        implicitHeight: 32
        font.pixelSize: 12
        contentItem: Text {
            text: segment.text
            color: segment.checked ? Theme.text : Theme.muted
            font: segment.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 8
            color: segment.down ? Theme.pressed : segment.hovered ? Theme.hover : segment.checked ? "#383838" : "transparent"
        }
    }
    Shortcut {
        sequence: "Ctrl+R"
        enabled: !recorder.globalHotkeysAvailable
        onActivated: recorder.toggleRecording()
    }
    Shortcut {
        sequence: "Ctrl+P"
        enabled: !recorder.globalHotkeysAvailable
        onActivated: recorder.togglePause()
    }
    Shortcut {
        sequence: "Escape"
        enabled: recorder.state === "countdown" || recorder.state === "checking"
        onActivated: recorder.toggleRecording()
    }
    onClosing: function (close) {
        if (updater.state === "preparing") {
            close.accepted = false;
            updateDialog.open();
            return;
        }
        if (recorder.state !== "idle") {
            close.accepted = false;
            recorder.setOptimized(false);
            closeDialog.open();
        } else if (recorder.hasUnsavedRecording) {
            close.accepted = false;
            unsavedExit.open();
        }
    }
    Connections {
        target: recorder
        function onMessageChanged() {
            if (recorder.message.length > 0)
                root.closeAfterSave = false;
        }
        function onEditorRequested() {
            testPlayer.stop();
            editor.open();
        }
        function onRecordingReady() {
            recordingSave.prepare();
        }
        function onRecordingSaved() {
            var shouldClose = root.closeAfterSave;
            root.closeAfterSave = false;
            recordingSave.close();
            if (shouldClose)
                Qt.callLater(function () {
                    root.close();
                });
        }
        function onOverwriteRequested() {
            recordingOverwrite.open();
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
            if (editor.returnToSave) {
                editor.returnToSave = false;
                editor.close();
                recordingSave.open();
            } else {
                player.source = file;
                editor.open();
            }
        }
        function onStateChanged() {
            if (recorder.state !== "idle") {
                updateDialog.close();
                helpDialog.close();
                licenseDialog.close();
                aboutDialog.close();
                for (var i = 0; i < mainMenu.count; i++)
                    mainMenu.menuAt(i).close();
            }
            if (["preparing", "checking", "countdown", "starting", "recording"].indexOf(recorder.state) >= 0) {
                saveChoice.close();
                editor.close();
                recordingSave.close();
            }
            if (root.closeAfterSave && root.activeRecording)
                recorder.toggleRecording();
            if (root.closeAfterSave && root.idle)
                Qt.callLater(function () {
                    if (root.closeAfterSave && root.idle) {
                        if (recorder.hasUnsavedRecording)
                            recorder.requestRecordingSave();
                        else
                            root.close();
                    }
                });
            if (root.idle && editor.opened && player.source.toString().length === 0)
                player.source = recorder.lastFile;
            if (!root.idle)
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

    Item {
        id: titlebar
        objectName: "titlebar"
        visible: !root.optimized
        width: parent.width
        height: 44
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 5
            spacing: 4
            Image {
                source: "qrc:/assets/app-icon.png"
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
                mipmap: true
            }
            Text {
                text: "FARecorder"
                color: "#c2c2c2"
                font.pixelSize: 11
                Layout.leftMargin: 5
                Layout.rightMargin: 12
            }
            MenuBar {
                id: mainMenu
                objectName: "mainMenu"
                padding: 0
                implicitHeight: 30
                background: Item {}
                delegate: MenuBarItem {
                    id: menuTitle
                    implicitHeight: 30
                    leftPadding: 10
                    rightPadding: 10
                    font.pixelSize: 12
                    contentItem: Text {
                        text: menuTitle.text
                        color: Theme.text
                        font: menuTitle.font
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 6
                        color: menuTitle.highlighted || menuTitle.hovered ? Theme.hover : "transparent"
                    }
                }
                QuietMenu {
                    objectName: "fileMenu"
                    title: "파일"
                    Action {
                        text: "녹화 저장…"
                        enabled: root.idle && recorder.hasUnsavedRecording
                        onTriggered: recorder.requestRecordingSave()
                    }
                    Action {
                        text: "녹화 폴더 지정…"
                        enabled: root.idle
                        onTriggered: folderDialog.open()
                    }
                    Action {
                        text: "녹화 폴더 열기"
                        onTriggered: recorder.openOutputFolder()
                    }
                    Action {
                        text: "복구 파일 열기"
                        onTriggered: recorder.openRecoveryFolder()
                    }
                    QuietSeparator {}
                    Action {
                        text: "종료"
                        onTriggered: root.close()
                    }
                }
                QuietMenu {
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
                QuietMenu {
                    title: "도움말"
                    Action {
                        objectName: "updateAction"
                        text: updater.state === "available" || updater.state === "ready" ? "업데이트 · " + updater.version : "업데이트…"
                        onTriggered: updateDialog.open()
                    }
                    Action {
                        objectName: "shortcutsAction"
                        text: "단축키"
                        onTriggered: {
                            helpTabs.currentIndex = 0;
                            helpDialog.open();
                        }
                    }
                    Action {
                        text: "사용 안내"
                        onTriggered: {
                            helpTabs.currentIndex = 1;
                            helpDialog.open();
                        }
                    }
                    Action {
                        objectName: "licensesAction"
                        text: "오픈소스 라이선스"
                        onTriggered: licenseDialog.open()
                    }
                    QuietSeparator {}
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
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                MouseArea {
                    objectName: "titlebarDrag"
                    anchors.fill: parent
                    onDoubleClicked: root.toggleMaximized()
                    DragHandler {
                        target: null
                        onActiveChanged: if (active)
                            root.moveWindow(centroid.scenePosition)
                    }
                }
            }
            IconButton {
                visible: Qt.platform.os === "windows"
                implicitWidth: 38
                symbol: "minimize"
                text: "최소화"
                onClicked: root.showMinimized()
            }
            IconButton {
                objectName: "maximizeButton"
                visible: Qt.platform.os === "windows"
                implicitWidth: 38
                symbol: root.visibility === Window.Maximized ? "restore" : "maximize"
                text: root.visibility === Window.Maximized ? "이전 크기" : "최대화"
                onClicked: root.toggleMaximized()
            }
            IconButton {
                visible: Qt.platform.os === "windows"
                implicitWidth: 38
                danger: true
                symbol: "close"
                text: "닫기"
                onClicked: root.close()
            }
        }
    }
    Rectangle {
        id: compactContent
        objectName: "compactContent"
        visible: root.optimized
        anchors.fill: parent
        color: "#191919"
        border.color: "#3b3b3b"
        radius: Qt.platform.os === "windows" ? 10 : 0
        // Behind the controls: every unoccupied pixel can move the window.
        MouseArea {
            objectName: "compactDrag"
            anchors.fill: parent
            onPressed: function (mouse) {
                root.moveWindow(Qt.point(mouse.x, mouse.y));
            }
        }
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
                RowLayout {
                    spacing: 8
                    Text {
                        text: recorder.elapsed
                        color: Theme.text
                        font.pixelSize: 24
                    }
                    Caption {
                        objectName: "compactSize"
                        text: "약 " + recorder.estimatedSize
                        font.pixelSize: 10
                        Accessible.name: "예상 파일 크기 " + recorder.estimatedSize
                    }
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

    Connections {
        target: updater
        function onReadyToRestart() {
            root.close();
        }
        function onAvailable() {
            if (root.idle && !recorder.hasUnsavedRecording && !editor.opened)
                updateDialog.open();
        }
    }
    QuietDialog {
        id: updateDialog
        objectName: "updateDialog"
        anchors.centerIn: parent
        width: Math.min(580, root.width - 48)
        title: "FARecorder 업데이트"
        height: Math.min(updater.notes.length > 0 ? 550 : 360, root.height - 48)
        property bool busy: updater.state === "checking" || updater.state === "downloading" || updater.state === "preparing" || updater.state === "installing"
        closePolicy: updater.state === "preparing" || updater.state === "installing" ? Popup.NoAutoClose : Popup.CloseOnEscape
        contentItem: ColumnLayout {
            spacing: 16
            Caption {
                text: "현재 버전 " + recorder.appInfo.version
            }
            Text {
                Layout.fillWidth: true
                text: updater.message
                wrapMode: Text.Wrap
                color: updater.state === "error" ? "#e4aaa0" : Theme.text
                font.pixelSize: 13
            }
            ProgressBar {
                visible: updater.state === "downloading"
                Layout.fillWidth: true
                value: updater.progress
            }
            ScrollView {
                visible: updater.notes.length > 0
                Layout.fillWidth: true
                Layout.preferredHeight: 160
                contentWidth: availableWidth
                clip: true
                TextArea {
                    text: updater.notes
                    textFormat: TextEdit.PlainText
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    color: Theme.text
                    font.pixelSize: 11
                    background: Rectangle {
                        color: Theme.panel
                        radius: 10
                    }
                }
            }
            QuietCheckBox {
                text: "시작할 때 새 버전 확인"
                checked: updater.automatic
                onToggled: updater.automatic = checked
            }
            QuietCheckBox {
                text: "베타 버전 포함"
                enabled: !updateDialog.busy && updater.state !== "ready"
                checked: updater.beta
                onToggled: updater.beta = checked
            }
            RowLayout {
                ActionButton {
                    text: "닫기"
                    enabled: updater.state !== "preparing" && updater.state !== "installing"
                    onClicked: updateDialog.close()
                }
                ActionButton {
                    text: "릴리즈 페이지"
                    onClicked: updater.openReleasePage()
                }
                Item {
                    Layout.fillWidth: true
                }
                ActionButton {
                    visible: updater.state === "downloading" || updater.state === "preparing"
                    text: "취소"
                    onClicked: updater.cancel()
                }
                ActionButton {
                    visible: updater.state !== "downloading" && updater.state !== "ready"
                    text: updater.state === "available" ? "다운로드" : "업데이트 확인"
                    primary: true
                    enabled: !updateDialog.busy && (updater.state !== "available" || updater.allowed)
                    onClicked: updater.state === "available" ? updater.download() : updater.check()
                }
                ActionButton {
                    objectName: "installUpdate"
                    visible: updater.state === "ready"
                    text: "설치하고 다시 시작"
                    primary: true
                    enabled: updater.allowed
                    onClicked: updater.install()
                }
            }
        }
    }
    QuietDialog {
        id: aboutDialog
        objectName: "aboutDialog"
        anchors.centerIn: parent
        width: Math.min(420, root.width - 48)
        title: "FARecorder 정보"
        modal: true
        background: Rectangle {
            color: "#252525"
            radius: 14
            border.color: "#414141"
        }
        footer: DialogButtonBox {
            background: Item {}
            ActionButton {
                text: "닫기"
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
        }
        ColumnLayout {
            width: parent.width
            spacing: 16
            Image {
                source: "qrc:/assets/app-icon.png"
                Layout.preferredWidth: 60
                Layout.preferredHeight: 60
                mipmap: true
            }
            Heading {
                text: recorder.appInfo.name
            }
            Caption {
                text: "버전  " + recorder.appInfo.version + "\n제작자  " + recorder.appInfo.author + "\n마지막 업데이트  " + recorder.appInfo.updated
                lineHeight: 1.7
                color: "#bdbdbd"
            }
            Caption {
                text: "FARecorder · MIT License\nD2Coding 1.4.0 · NAVER · SIL OFL 1.1"
                lineHeight: 1.7
            }
            RowLayout {
                ActionButton {
                    text: "라이선스"
                    onClicked: licenseDialog.open()
                }
                ActionButton {
                    text: "GitHub"
                    onClicked: Qt.openUrlExternally(recorder.appInfo.repository)
                }
            }
        }
    }
    QuietDialog {
        id: licenseDialog
        objectName: "licenseDialog"
        anchors.centerIn: parent
        width: Math.min(700, root.width - 48)
        height: Math.min(600, root.height - 64)
        title: "오픈소스 라이선스"
        modal: true
        background: Rectangle {
            color: "#252525"
            radius: 14
            border.color: "#414141"
        }
        footer: DialogButtonBox {
            background: Item {}
            ActionButton {
                text: "닫기"
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: 14
            TabBar {
                id: licenseTabs
                Layout.fillWidth: true
                background: Item {}
                Segment {
                    text: "FARecorder · MIT"
                }
                Segment {
                    text: "D2Coding · OFL"
                }
                Segment {
                    text: "기타 구성요소"
                }
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                TextArea {
                    readOnly: true
                    selectByMouse: true
                    text: recorder.licenseText(licenseTabs.currentIndex === 0 ? "MIT" : licenseTabs.currentIndex === 1 ? "OFL" : "third-party")
                    wrapMode: TextEdit.Wrap
                    font.pixelSize: 12
                    color: Theme.text
                    background: Rectangle {
                        color: Theme.panel
                        radius: 10
                    }
                }
            }
        }
    }
    QuietDialog {
        id: helpDialog
        objectName: "helpDialog"
        anchors.centerIn: parent
        width: Math.min(620, root.width - 48)
        height: Math.min(helpTabs.currentIndex === 0 ? 360 : 560, root.height - 64)
        title: "도움말"
        modal: true
        background: Rectangle {
            color: "#252525"
            radius: 14
            border.color: "#414141"
        }
        footer: DialogButtonBox {
            background: Item {}
            ActionButton {
                text: "닫기"
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: 18
            TabBar {
                id: helpTabs
                Layout.fillWidth: true
                background: Item {}
                Segment {
                    text: "단축키"
                }
                Segment {
                    text: "녹화"
                }
                Segment {
                    text: "오디오"
                }
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: parent.width
                    spacing: 16
                    Repeater {
                        model: helpTabs.currentIndex === 0 ? [
                            {
                                key: Qt.platform.os === "osx" ? "⌘R" : "Ctrl+R",
                                label: "녹화 시작 / 종료"
                            },
                            {
                                key: Qt.platform.os === "osx" ? "⌘P" : "Ctrl+P",
                                label: "일시정지 / 재개"
                            },
                            {
                                key: "Esc",
                                label: "인코더 검사 · 카운트다운 · 영역 선택 취소"
                            }
                        ] : []
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            Caption {
                                text: modelData.label
                                Layout.fillWidth: true
                                color: Theme.text
                            }
                            ActionButton {
                                text: modelData.key
                                enabled: false
                                opacity: 1
                                implicitHeight: 30
                            }
                        }
                    }
                    Caption {
                        visible: helpTabs.currentIndex === 0
                        Layout.fillWidth: true
                        text: recorder.globalHotkeysAvailable ? "앱이 뒤에 있어도 동작하는 전역 단축키입니다." : recorder.hotkeyStatus
                        wrapMode: Text.Wrap
                        lineHeight: 1.7
                    }
                    Caption {
                        visible: helpTabs.currentIndex > 0
                        Layout.fillWidth: true
                        color: "#c4c4c4"
                        wrapMode: Text.Wrap
                        textFormat: Text.RichText
                        lineHeight: 1.8
                        text: helpTabs.currentIndex === 1 ? "<b>화면 선택</b><br>화면과 창 목록은 자동으로 갱신됩니다. 대상을 고른 뒤 연결하세요. 오른쪽 패널 아이콘에서 상세 설정을 엽니다.<br><br><b>영역 녹화</b><br>영역 선택 후 드래그하면 선택한 픽셀 크기로 저장합니다. 녹화·일시정지 중에도 손잡이나 테두리를 옮길 수 있습니다. 이동 범위는 같은 디스플레이 내부입니다.<br><br><b>녹화와 저장</b><br>첫 녹화 또는 설정 변경 시 실제 인코딩을 검사한 뒤 시작합니다. 녹화 중에는 작은 화면으로 바뀌며 화살표로 원래 화면에 돌아올 수 있습니다. 멈춘 시간은 영상과 소리에서 제외됩니다. 버튼 밖의 빈 공간은 어디서든 드래그할 수 있습니다. 타이머 옆의 용량은 현재까지의 영상과 최종 AAC 트랙에 대한 예상 파일 크기입니다. RAM 사용량이나 복구 원본을 합친 디스크 점유량이 아니며, 인코더 버퍼와 헤더 때문에 최종 크기와 차이가 있습니다.<br><br><b>저장 위치 · 편집</b><br>파일 메뉴나 하단 폴더 버튼에서 저장 폴더를 고릅니다. 기본 형식은 MP4입니다. 녹화 종료 후 저장 창에서 이름과 위치를 정하고, 필요한 경우 편집을 엽니다. 편집을 닫으면 저장 창으로 돌아옵니다. 같은 이름의 파일은 덮어쓰기를 확인합니다. 파일명은 녹화 시작 시간이며 바꿀 수 있습니다. 영상은 지정 폴더에 직접 저장하고 하위 폴더를 만들지 않습니다. 나중에 저장하면 종료 시 저장 여부를 다시 묻습니다. 편집본은 새 파일 또는 원본 덮어쓰기를 선택합니다. 복구용 영상과 독립 WAV는 앱 전용 Recovery 경로에 남으며 파일 메뉴의 복구 파일 열기에서 확인합니다." : "<b>녹음할 소리 선택</b><br>마이크와 컴퓨터 소리 선택기를 눌러 실제 장치나 앱을 고릅니다. 처음에는 사용 안 함 상태입니다. 전체 출력 장치와 앱별 소리는 중복 선택하지 않습니다.<br><br><b>녹화용 믹서</b><br>상세 패널의 마이크 / 컴퓨터 탭에서 각각 음량과 음소거를 조절합니다. 운영체제의 음량 설정은 바꾸지 않습니다. 녹화 중에는 기존 소리의 음량·음소거만 바꿀 수 있습니다. 음소거로 저장한 구간은 나중에 복원할 수 없습니다.<br><br><b>소리 확인</b><br>소리 테스트는 선택한 소리를 5초 동안 녹음합니다. 샘플 듣기를 직접 누른 경우에만 소리를 재생합니다. 앱이 목록에 없으면 해당 앱에서 소리를 재생해 주세요. 연결이 끊긴 장치는 선택이 해제됩니다.<br><br><b>장치와 권한</b><br>마이크·화면 녹화 권한이 필요할 수 있습니다. macOS 마이크는 현재 시스템 기본 입력을 사용합니다."
                    }
                }
            }
        }
    }

    RowLayout {
        id: bodyHeader
        visible: !root.optimized
        anchors {
            top: titlebar.bottom
            left: parent.left
            right: parent.right
            leftMargin: 20
            rightMargin: 20
        }
        height: root.height < 700 ? 46 : 52
        TabBar {
            id: sourceTabs
            objectName: "sourceTabs"
            background: Item {}
            currentIndex: root.mode
            enabled: root.canChoose
            onCurrentIndexChanged: root.mode = currentIndex
            Segment {
                text: "전체 화면"
                implicitWidth: 84
            }
            Segment {
                text: "특정 창"
                implicitWidth: 76
            }
            Segment {
                text: "영역"
                implicitWidth: 60
            }
        }
        Item {
            Layout.fillWidth: true
        }
        IconButton {
            visible: root.activeRecording
            symbol: "compact"
            text: "작은 녹화 화면"
            onClicked: recorder.setOptimized(true)
        }
        IconButton {
            objectName: "modeButton"
            symbol: "panel"
            text: root.expanded ? "상세 패널 닫기" : "상세 패널 열기"
            onClicked: recorder.setExpanded(!root.expanded)
        }
    }
    RowLayout {
        objectName: "normalContent"
        visible: !root.optimized
        anchors {
            top: bodyHeader.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
            rightMargin: 8
            bottomMargin: 8
        }
        spacing: 0
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: root.height < 700 ? 14 : 20
            spacing: root.height < 700 ? 8 : 14
            Rectangle {
                visible: recorder.message.length > 0
                Layout.fillWidth: true
                implicitHeight: alertText.implicitHeight + 24
                color: "#352522"
                radius: 10
                Text {
                    id: alertText
                    anchors {
                        left: parent.left
                        right: closeAlert.left
                        verticalCenter: parent.verticalCenter
                        margins: 12
                    }
                    text: recorder.message
                    color: "#e8b7b0"
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                }
                IconButton {
                    id: closeAlert
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    symbol: "close"
                    text: "알림 닫기"
                    onClicked: recorder.clearMessage()
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Select {
                    id: displayCombo
                    objectName: "displayCombo"
                    Layout.fillWidth: true
                    visible: root.mode !== 1
                    model: recorder.displays
                    textRole: "name"
                    valueRole: "id"
                    enabled: root.canChoose
                    displayText: currentIndex < 0 ? (recorder.discovering ? "화면 찾는 중…" : "화면 선택") : currentText
                    onActivated: root.displaySelection = currentValue
                    onModelChanged: Qt.callLater(function () {
                        root.restoreCombo(displayCombo, root.displaySelection, true);
                    })
                }
                Select {
                    id: windowCombo
                    objectName: "windowCombo"
                    Layout.fillWidth: true
                    visible: root.mode === 1
                    model: recorder.windows
                    textRole: "name"
                    valueRole: "id"
                    enabled: root.canChoose
                    displayText: currentIndex < 0 ? (recorder.discovering ? "창 찾는 중…" : "창 선택") : currentText
                    onActivated: root.windowSelection = currentValue
                    onModelChanged: Qt.callLater(function () {
                        root.restoreCombo(windowCombo, root.windowSelection, false);
                    })
                }
                ActionButton {
                    text: root.mode === 2 ? "영역 선택" : "연결"
                    implicitHeight: 38
                    implicitWidth: 84
                    enabled: root.canChoose && (root.mode === 1 ? windowCombo.currentIndex >= 0 : displayCombo.currentIndex >= 0)
                    onClicked: root.mode === 2 ? recorder.beginRegion(displayCombo.currentValue) : recorder.connectSource(root.mode === 1 ? windowCombo.currentValue : displayCombo.currentValue, root.mode === 1)
                }
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 100
                radius: 12
                color: "#101010"
                border.color: Theme.border
                clip: true
                Image {
                    anchors.fill: parent
                    anchors.margins: 1
                    fillMode: Image.PreserveAspectFit
                    asynchronous: false
                    cache: false
                    source: "image://preview/" + recorder.previewRevision
                    visible: recorder.hasSource
                }
                Column {
                    visible: !recorder.hasSource
                    anchors.centerIn: parent
                    spacing: 14
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 38
                        height: 26
                        color: "transparent"
                        radius: 4
                        border.width: 1
                        border.color: "#666666"
                    }
                    Caption {
                        text: "녹화할 화면을 선택하세요"
                    }
                }
                Rectangle {
                    visible: root.captureTestTick >= 0
                    width: 100
                    height: 60
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
                    color: "#dd161616"
                    Text {
                        anchors.centerIn: parent
                        text: recorder.countdown
                        color: Theme.text
                        font.pixelSize: 72
                    }
                }
                Rectangle {
                    visible: recorder.state === "paused"
                    anchors.centerIn: parent
                    width: 128
                    height: 36
                    color: "#dd222222"
                    radius: 10
                    Text {
                        anchors.centerIn: parent
                        text: "일시정지"
                        color: "#d1b57c"
                        font.pixelSize: 12
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Caption {
                    Layout.fillWidth: true
                    text: recorder.sourceName || ""
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
                spacing: 12
                AudioCard {
                    mic: true
                    Layout.fillWidth: true
                    routes: root.kindRoutes(true)
                    allowSelection: root.canChoose
                    allowMix: root.idle || root.activeRecording || recorder.audioTestActive
                }
                AudioCard {
                    mic: false
                    Layout.fillWidth: true
                    routes: root.kindRoutes(false)
                    allowSelection: root.canChoose
                    allowMix: root.idle || root.activeRecording || recorder.audioTestActive
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                ActionButton {
                    objectName: "audioTestButton"
                    text: recorder.audioTestActive ? "테스트 중지" : "소리 테스트"
                    implicitHeight: 30
                    font.pixelSize: 11
                    enabled: recorder.audioTestActive || (root.idle && root.selectedRoutes.length > 0)
                    onClicked: {
                        testPlayer.stop();
                        recorder.toggleAudioTest();
                    }
                }
                ActionButton {
                    objectName: "samplePlayButton"
                    text: testPlayer.playbackState === MediaPlayer.PlayingState ? "듣기 중지" : "샘플 듣기"
                    implicitHeight: 30
                    font.pixelSize: 11
                    enabled: root.idle && !recorder.audioTestActive && recorder.audioSample.toString().length > 0
                    onClicked: testPlayer.playbackState === MediaPlayer.PlayingState ? testPlayer.stop() : testPlayer.play()
                }
                Caption {
                    visible: recorder.audioTestActive
                    text: recorder.audioTestProgress + "%"
                }
                Item {
                    Layout.fillWidth: true
                }
            }
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Theme.border
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Text {
                    objectName: "recordingTimer"
                    visible: root.activeRecording
                    text: recorder.elapsed
                    color: Theme.text
                    font.pixelSize: 30
                    font.family: root.font.family
                    font.letterSpacing: 0
                }
                Caption {
                    visible: root.activeRecording
                    text: "약 " + recorder.estimatedSize
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
                TransportButton {
                    objectName: "recordButton"
                    large: true
                    symbol: root.activeRecording ? "stop" : "record"
                    text: root.activeRecording ? "녹화 종료" : "녹화 시작"
                    enabled: ["idle", "recording", "paused", "countdown", "checking", "preparing"].indexOf(recorder.state) >= 0
                    onClicked: {
                        testPlayer.stop();
                        recorder.toggleRecording();
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                ActionButton {
                    objectName: "chooseFolderButton"
                    text: "저장 위치"
                    implicitHeight: 28
                    font.pixelSize: 10
                    enabled: root.idle
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    ToolTip.text: recorder.settings.output + "\n" + recorder.diskSpace
                    onClicked: folderDialog.open()
                }
                Item {
                    Layout.fillWidth: true
                }
                Rectangle {
                    width: 6
                    height: 6
                    radius: 3
                    color: recorder.state === "recording" ? Theme.red : recorder.state === "paused" ? "#d1b57c" : "#757575"
                }
                Caption {
                    text: ({
                            idle: "대기",
                            checking: "검사 중",
                            preparing: "준비 중",
                            countdown: "시작 대기",
                            starting: "연결 중",
                            recording: "녹화 중",
                            paused: "일시정지",
                            saving: "저장 중",
                            exporting: "저장 중",
                            editing: "편집 중"
                        })[recorder.state] || recorder.state
                }
            }
        }
        Rectangle {
            visible: root.expanded
            Layout.fillHeight: true
            Layout.preferredWidth: 340
            color: "transparent"
            Rectangle {
                width: 1
                height: parent.height
                color: Theme.border
            }
            ScrollView {
                id: detailsScroll
                anchors.fill: parent
                anchors.margins: 20
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: detailsScroll.availableWidth
                    spacing: 16
                    Heading {
                        text: "녹화 설정"
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
                            model: Qt.platform.os === "osx" ? ["GPU 직접 · Apple", "CPU · H.264", "Apple · FFmpeg"] : ["GPU 직접 · Windows", "CPU · H.264", "NVIDIA · FFmpeg", "Intel · FFmpeg", "AMD · FFmpeg"]
                            property var ids: Qt.platform.os === "osx" ? ["native_gpu", "libx264", "h264_videotoolbox"] : ["native_gpu", "libx264", "h264_nvenc", "h264_qsv", "h264_amf"]
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
                    QuietCheckBox {
                        text: "마우스 커서 포함"
                        checked: recorder.settings.cursor
                        enabled: root.idle
                        onToggled: recorder.setSetting("cursor", checked)
                        font.pixelSize: 12
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: Theme.border
                    }
                    Heading {
                        text: "녹화용 믹서"
                    }
                    TabBar {
                        id: mixerTabs
                        objectName: "mixerTabs"
                        Layout.fillWidth: true
                        background: Rectangle {
                            radius: 9
                            color: Theme.panel
                        }
                        Segment {
                            text: "마이크"
                        }
                        Segment {
                            text: "컴퓨터 소리"
                        }
                    }
                    Repeater {
                        model: root.kindRoutes(mixerTabs.currentIndex === 0).length
                        delegate: AudioRoute {
                            required property int index
                            Layout.fillWidth: true
                            sourceData: root.kindRoutes(mixerTabs.currentIndex === 0)[index] || ({
                                    id: "",
                                    name: "",
                                    kind: -1,
                                    selected: false
                                })
                            allowSelection: root.canChoose
                            allowMix: root.idle || root.activeRecording || recorder.audioTestActive
                        }
                    }
                    Caption {
                        visible: root.kindRoutes(mixerTabs.currentIndex === 0).length === 0
                        Layout.fillWidth: true
                        text: recorder.discovering ? "찾는 중…" : mixerTabs.currentIndex === 0 ? "연결된 마이크 없음" : "사용 가능한 소리 없음"
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
        id: captureFrame
        objectName: "captureRegionFrame"
        title: "FARecorder · 녹화 영역"
        flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool | Qt.WindowDoesNotAcceptFocus
        color: "transparent"
        property int contentTop: recorder.regionInfo.handleBelow ? 4 : 30
        visible: recorder.regionInfo.active === true && !region.visible && ["idle", "checking", "countdown", "starting", "recording", "paused"].indexOf(recorder.state) >= 0
        x: (recorder.regionInfo.x || 0) - 4
        y: (recorder.regionInfo.y || 0) - contentTop
        width: (recorder.regionInfo.width || 0) + 8
        height: (recorder.regionInfo.height || 0) + 34
        onWidthChanged: recorder.configureRegionFrame(captureFrame)
        onHeightChanged: recorder.configureRegionFrame(captureFrame)
        onContentTopChanged: recorder.configureRegionFrame(captureFrame)
        onVisibleChanged: if (visible)
            Qt.callLater(function () {
                recorder.configureRegionFrame(captureFrame);
            })
        Rectangle {
            x: 1
            y: captureFrame.contentTop - 3
            width: parent.width - 2
            height: (recorder.regionInfo.height || 0) + 6
            color: "transparent"
            border.width: 2
            border.color: recorder.state === "paused" ? "#d1b57c" : root.activeRecording ? "#ef5751" : "#d0d0d0"
        }
        Rectangle {
            width: parent.width
            height: 26
            y: recorder.regionInfo.handleBelow ? parent.height - 26 : 0
            radius: 4
            color: "#242424"
            Text {
                anchors.verticalCenter: parent.verticalCenter
                x: 8
                width: parent.width - 16
                text: recorder.regionInfo.pixelWidth + " × " + recorder.regionInfo.pixelHeight + " · 드래그해 이동"
                color: "#eeeeee"
                font.pixelSize: 11
                elide: Text.ElideRight
            }
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
            property point pressGlobal
            property point initialRegion
            onPressed: function (mouse) {
                pressGlobal = mapToGlobal(mouse.x, mouse.y);
                initialRegion = Qt.point(recorder.regionInfo.x, recorder.regionInfo.y);
            }
            onPositionChanged: function (mouse) {
                if (pressed) {
                    var position = mapToGlobal(mouse.x, mouse.y);
                    recorder.moveRegion(initialRegion.x + position.x - pressGlobal.x, initialRegion.y + position.y - pressGlobal.y);
                }
            }
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
            onReleased: function (mouse) {
                region.endX = mouse.x;
                region.endY = mouse.y;
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

    FolderDialog {
        id: recordingFolder
        title: "영상 저장 위치"
        currentFolder: recordingSave.folder
        onAccepted: recordingSave.folder = selectedFolder
    }
    QuietDialog {
        id: recordingSave
        objectName: "recordingSave"
        property url folder
        property string preparedFor: ""
        function prepare() {
            var recordingKey = recorder.recordingFile.folder.toString();
            if (preparedFor !== recordingKey) {
                preparedFor = recordingKey;
                folder = recorder.outputFolder;
                recordingName.text = recorder.recordingFile.name;
            }
            open();
        }
        anchors.centerIn: parent
        width: Math.min(540, root.width - 48)
        title: recorder.state === "exporting" ? "영상 저장 중…" : "녹화 저장"
        closePolicy: root.idle ? Popup.CloseOnEscape : Popup.NoAutoClose
        onRejected: root.closeAfterSave = false
        contentItem: ColumnLayout {
            spacing: 16
            Caption {
                text: "파일 이름"
            }
            Field {
                id: recordingName
                objectName: "recordingName"
                Layout.fillWidth: true
                enabled: root.idle
                selectByMouse: true
            }
            Caption {
                text: "저장 위치"
            }
            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: decodeURIComponent(recordingSave.folder.toString().replace(/^file:\/\/\//, ""))
                    color: Theme.text
                    font.pixelSize: 12
                    elide: Text.ElideMiddle
                }
                ActionButton {
                    text: "변경…"
                    enabled: root.idle
                    onClicked: recordingFolder.open()
                }
            }
            Caption {
                text: recorder.recordingFile.size
            }
            Text {
                Layout.fillWidth: true
                visible: recorder.message.length > 0
                text: recorder.message
                color: "#e4aaa0"
                wrapMode: Text.Wrap
                font.pixelSize: 12
            }
            RowLayout {
                ActionButton {
                    text: "나중에"
                    enabled: root.idle
                    onClicked: {
                        root.closeAfterSave = false;
                        recordingSave.close();
                    }
                }
                Item {
                    Layout.fillWidth: true
                }
                ActionButton {
                    objectName: "editBeforeSave"
                    text: "편집…"
                    enabled: root.idle
                    onClicked: {
                        editor.returnToSave = true;
                        recordingSave.close();
                        editor.open();
                    }
                }
                ActionButton {
                    objectName: "saveRecording"
                    text: recorder.state === "exporting" ? "저장 중…" : "저장"
                    primary: true
                    enabled: root.idle
                    onClicked: {
                        recorder.clearMessage();
                        recorder.saveRecording(recordingSave.folder, recordingName.text);
                    }
                }
            }
        }
    }
    QuietDialog {
        id: recordingOverwrite
        onRejected: root.closeAfterSave = false
        objectName: "recordingOverwrite"
        anchors.centerIn: parent
        width: Math.min(440, root.width - 48)
        title: "같은 이름의 파일을 덮어쓸까요?"
        contentItem: RowLayout {
            ActionButton {
                objectName: "cancelRecordingOverwrite"
                text: "취소"
                onClicked: {
                    root.closeAfterSave = false;
                    recordingOverwrite.close();
                }
            }
            Item {
                Layout.fillWidth: true
            }
            ActionButton {
                objectName: "confirmRecordingOverwrite"
                text: "덮어쓰기"
                danger: true
                onClicked: {
                    recordingOverwrite.close();
                    recorder.saveRecording(recordingSave.folder, recordingName.text, true);
                }
            }
        }
    }
    QuietDialog {
        id: editor
        objectName: "editor"
        property bool returnToSave: false
        anchors.centerIn: parent
        width: Math.min(root.width - 80, 1000)
        height: Math.min(root.height - 60, 800)
        modal: true
        title: "영상 편집"
        background: Rectangle {
            color: "#191919"
            radius: Theme.windowRadius
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
            if (returnToSave && root.idle) {
                returnToSave = false;
                recordingSave.open();
            }
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
    QuietDialog {
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
    QuietDialog {
        id: unsavedExit
        objectName: "unsavedExit"
        anchors.centerIn: parent
        width: Math.min(520, root.width - 48)
        title: "저장하지 않은 녹화가 있습니다"
        onRejected: root.closeAfterSave = false
        contentItem: ColumnLayout {
            spacing: 16
            Caption {
                Layout.fillWidth: true
                text: recorder.recordingFile.name
                elide: Text.ElideMiddle
            }
            RowLayout {
                ActionButton {
                    objectName: "cancelUnsavedExit"
                    text: "취소"
                    onClicked: {
                        root.closeAfterSave = false;
                        unsavedExit.close();
                    }
                }
                ActionButton {
                    objectName: "exitWithoutSaving"
                    text: "저장하지 않고 종료"
                    onClicked: {
                        unsavedExit.close();
                        recorder.leaveRecordingUnsaved();
                        root.closeAfterSave = false;
                        root.close();
                    }
                }
                ActionButton {
                    objectName: "saveBeforeExit"
                    text: "저장 후 종료"
                    primary: true
                    onClicked: {
                        unsavedExit.close();
                        root.closeAfterSave = true;
                        editor.returnToSave = false;
                        editor.close();
                        recorder.requestRecordingSave();
                    }
                }
            }
        }
    }
    QuietDialog {
        id: closeDialog
        onRejected: root.closeAfterSave = false
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
                    onClicked: {
                        root.closeAfterSave = false;
                        closeDialog.close();
                    }
                }
                ActionButton {
                    text: "작업 마치고 저장"
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
