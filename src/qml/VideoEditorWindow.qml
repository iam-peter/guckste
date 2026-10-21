pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtMultimedia
import Guckste
import Guckste.Core
import Guckste.Ui

// A recording to draw on and trim before it is saved: the marks lie over
// the whole video, and Save writes a new file with them on every frame
// and the part outside the trim left out.
ApplicationWindow {
    id: root

    required property Document document
    // The recording as it was captured; moved on save when temporary,
    // removed when discarded.
    required property string sourcePath
    required property VideoExporter exporter
    property bool temporary: false

    property int tool: Document.Select
    property color toolColor: Preferences.color
    property real size: Preferences.size
    property real textSize: Preferences.textSize
    property int trimStart: 0
    property int trimEnd: -1
    property bool saved: false
    property bool discardConfirmed: false
    property string savedPath: ""

    // From the player once it knows, from the preview frame until then.
    readonly property size videoSize: player.metaData.value(MediaMetaData.Resolution) ?? Qt.size(0, 0)
    readonly property int videoWidth: videoSize.width > 0 ? videoSize.width : document.imageWidth
    readonly property int videoHeight: videoSize.height > 0 ? videoSize.height : document.imageHeight
    readonly property real fit: videoWidth > 0 && videoHeight > 0
                                ? Math.min((stage.width - 24) / videoWidth, (stage.height - 24) / videoHeight, 1) : 1
    readonly property int duration: player.duration
    readonly property int effectiveEnd: trimEnd < 0 ? duration : trimEnd
    readonly property bool unchanged: document.annotationCount === 0 && trimStart <= 0 && trimEnd < 0

    width: Math.max(900, Math.min(videoWidth + 60, Screen.desktopAvailableWidth * 0.9))
    height: Math.max(600, Math.min(videoHeight + 180, Screen.desktopAvailableHeight * 0.9))
    readonly property string fileName: sourcePath.split("/").pop()
    title: `${temporary ? qsTr("Recording") : fileName}${saved ? "" : " •"} — Guckste`
    color: Qt.darker(palette.window, 1.25)

    function time(ms: int): string {
        const s = Math.floor(Math.max(0, ms) / 1000)
        const m = Math.floor(s / 60)
        const two = n => (n < 10 ? "0" : "") + n
        return `${two(m)}:${two(s % 60)}`
    }

    function togglePlay() {
        if (player.playbackState === MediaPlayer.PlayingState)
            player.pause()
        else {
            if (player.position >= effectiveEnd - 50 || player.position < trimStart)
                player.position = trimStart
            player.play()
        }
    }

    function stepFrame(frames: int) {
        player.pause()
        const frameMs = 1000 / Math.max(1, player.metaData.value(MediaMetaData.VideoFrameRate) || 30)
        player.position = root.clamp(player.position + frames * frameMs, 0, duration)
    }

    function clamp(value: real, low: real, high: real): real {
        return Math.max(low, Math.min(high, value))
    }

    function save() {
        marks.commitText()
        player.pause()
        if (exporter.running)
            return
        App.exportRecording(exporter, sourcePath, Preferences.nextRecordingPath(App.recordingSuffix()), document,
                            trimStart, trimEnd, temporary)
    }

    function saveAs() {
        marks.commitText()
        player.pause()
        saveDialog.open()
    }

    onClosing: close => {
        if (!saved && !discardConfirmed && (temporary || !unchanged)) {
            close.accepted = false
            // Closed from the taskbar the window may be behind others or
            // minimized: it comes forward to ask.
            if (visibility === Window.Minimized)
                showNormal()
            raise()
            requestActivate()
            discardDialog.open()
        }
    }

    Connections {
        target: root.exporter
        function onFinished(path: string) {
            root.saved = true
            root.savedPath = path
            status.flash(qsTr("Saved %1").arg(path))
            if (Preferences.copyPathAfterSave)
                Clipboard.setText(path)
            App.notify(qsTr("Recording saved"), path)
        }
        function onFailed(message: string) {
            status.flash(qsTr("Could not save: %1").arg(message))
        }
    }

    // Marks show at their time on the recording, and new ones start at
    // the playhead.
    Binding {
        target: root.document
        property: "time"
        value: player.position
    }

    MediaPlayer {
        id: player
        objectName: "player"
        source: "file://" + root.sourcePath
        videoOutput: videoView
        audioOutput: AudioOutput {}
        // Nothing is shown until the player renders a frame, which a seek
        // makes it do, as scrubbing does; a play that is paused at once
        // does not. Only when the file first loads: the player reports it
        // loaded again while playing and seeking, and would stop each time.
        property bool showedFirstFrame: false
        onMediaStatusChanged: {
            if (player.mediaStatus === MediaPlayer.LoadedMedia && !showedFirstFrame) {
                showedFirstFrame = true
                player.pause()
                firstFrame.restart()
            }
        }
        onPositionChanged: {
            if (player.playbackState === MediaPlayer.PlayingState && player.position >= root.effectiveEnd
                    && root.trimEnd >= 0)
                player.pause()
        }
    }

    Timer {
        id: firstFrame
        interval: 80
        onTriggered: {
            player.position = 1
            player.position = 0
        }
    }

    header: AnnotationToolbar {
        overflow: true
        tool: root.tool
        color: root.toolColor
        size: root.size
        textSize: root.textSize
        textMode: root.tool === Document.Text
        document: root.document
        onToolPicked: tool => root.tool = tool
        onColorPicked: picked => root.toolColor = picked
        onSizePicked: size => root.size = size
        onTextSizePicked: size => root.textSize = size

        IconButton { iconName: "save"; tip: qsTr("Save to the screenshot folder (Ctrl+S)"); enabled: !root.exporter.running; onClicked: root.save() }
        IconButton { iconName: "download"; tip: qsTr("Save as… (Ctrl+Shift+S)"); enabled: !root.exporter.running; onClicked: root.saveAs() }
        IconButton { iconName: "settings"; tip: qsTr("Settings"); onClicked: App.openSettings() }
    }

    // The video above, the controls, the timeline and the lanes below,
    // with a divider between them to drag.
    SplitView {
        id: split
        anchors.fill: parent
        orientation: Qt.Vertical

        Item {
            id: stage
            SplitView.fillHeight: true

            // The video at its own size, scaled down to fit: marks are drawn
            // at output size, so what is seen is what is saved.
            Item {
                id: canvas
                x: Math.round((stage.width - root.videoWidth * root.fit) / 2)
                y: Math.round((stage.height - root.videoHeight * root.fit) / 2)
                width: root.videoWidth
                height: root.videoHeight
                scale: root.fit
                transformOrigin: Item.TopLeft

                VideoOutput {
                    id: videoView
                    anchors.fill: parent
                    fillMode: VideoOutput.Stretch
                }
                // Pixelated marks coarsen the frame on screen, not the first.
                Component.onCompleted: root.document.followVideo(videoView.videoSink)

                DrawingArea {
                    id: marks
                    anchors.fill: parent
                    document: root.document
                    zoom: 1
                    tool: root.tool
                    toolColor: root.toolColor
                    size: root.size
                    textSize: root.textSize
                }
            }
        }

        Pane {
            id: bottomPanel
            objectName: "bottomPanel"
            SplitView.preferredHeight: Math.min(implicitHeight, split.height / 2)
            SplitView.minimumHeight: controlsColumn.children[0].implicitHeight + topPadding + bottomPadding
            padding: 6
            background: Rectangle { color: root.palette.window }

            ColumnLayout {
                id: controlsColumn
                anchors.fill: parent
                spacing: 4

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                IconButton {
                    iconName: "skip-back"
                    tip: qsTr("To the start (Home)")
                    onClicked: { player.pause(); player.position = root.trimStart }
                }
                IconButton { iconName: "step-back"; tip: qsTr("A frame back (,)"); onClicked: root.stepFrame(-1) }
                IconButton {
                    objectName: "playButton"
                    iconName: player.playbackState === MediaPlayer.PlayingState ? "pause" : "play"
                    tip: qsTr("Play or pause (Space)")
                    onClicked: root.togglePlay()
                }
                IconButton { iconName: "step-forward"; tip: qsTr("A frame on (.)"); onClicked: root.stepFrame(1) }
                IconButton {
                    iconName: "skip-forward"
                    tip: qsTr("To the end (End)")
                    onClicked: { player.pause(); player.position = root.effectiveEnd }
                }
                Label {
                    font.pixelSize: 12
                    font.family: "monospace"
                    text: `${root.time(player.position)} / ${root.time(root.duration)}`
                }

                Timeline {
                    id: timeline
                    objectName: "timeline"
                    Layout.fillWidth: true
                    duration: root.duration
                    position: player.position
                    trimStart: root.trimStart
                    trimEnd: root.trimEnd
                    onSeekRequested: ms => { player.pause(); player.position = ms }
                    onTrimStartRequested: ms => root.trimStart = ms
                    onTrimEndRequested: ms => root.trimEnd = ms
                }

            }

            TimelineLanes {
                id: lanes
                objectName: "lanes"
                Layout.fillWidth: true
                // As tall as the divider above leaves room for.
                Layout.fillHeight: true
                document: root.document
                timeline: timeline
                trackX: timeline.x
                position: player.position
                trimStart: root.trimStart
                trimEnd: root.trimEnd
                onPicked: root.tool = Document.Select
                onSeekRequested: ms => { player.pause(); player.position = ms }
            }
            }
        }
    }

    footer: ToolBar {
        id: status

        property string message: ""

        function flash(text: string) {
            message = text
            messageTimer.restart()
        }
        Timer {
            id: messageTimer
            interval: 5000
            onTriggered: status.message = ""
        }

        padding: 6
        ColumnLayout {
            anchors.fill: parent
            spacing: 4

            RowLayout {
                spacing: 12
                Label {
                    font.pixelSize: 12
                    text: qsTr("%1 × %2 px").arg(root.videoWidth).arg(root.videoHeight)
                }
                Label {
                    font.pixelSize: 12
                    visible: root.trimStart > 0 || root.trimEnd >= 0
                    text: qsTr("Keeps %1 – %2").arg(root.time(root.trimStart)).arg(root.time(root.effectiveEnd))
                }
                Label {
                    Layout.fillWidth: true
                    font.pixelSize: 12
                    elide: Text.ElideRight
                    color: status.message !== "" ? Theme.accent : palette.placeholderText
                    text: status.message !== "" ? status.message
                        : root.exporter.running ? qsTr("Writing the video…")
                        : root.unchanged ? qsTr("Draw on the video, or set a start and an end; Save keeps it as it is.")
                        : qsTr("Save writes a new video with each mark on the frames of its time; drag its bar below to set them.")
                }
                ProgressBar {
                    visible: root.exporter.running
                    Layout.preferredWidth: 160
                    from: 0
                    to: 1
                    value: root.exporter.progress
                }
                ToolButton {
                    visible: root.exporter.running
                    text: qsTr("Cancel")
                    focusPolicy: Qt.NoFocus
                    onClicked: root.exporter.cancel()
                }
            }
        }
    }

    FileDialog {
        id: saveDialog
        title: qsTr("Save recording")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Video (*.%1)").arg(App.recordingSuffix())]
        defaultSuffix: App.recordingSuffix()
        currentFolder: "file://" + Preferences.saveDirectory
        selectedFile: "file://" + Preferences.nextRecordingPath(App.recordingSuffix())
        onAccepted: App.exportRecording(root.exporter, root.sourcePath, selectedFile.toString().replace(/^file:\/\//, ""),
                                        root.document, root.trimStart, root.trimEnd, root.temporary)
    }

    Dialog {
        id: discardDialog
        objectName: "discardDialog"
        anchors.centerIn: parent
        modal: true
        // A window of its own, titled and decorated by the desktop like
        // the settings, rather than a panel drawn into the editor.
        popupType: Popup.Window
        padding: 16
        title: root.temporary ? qsTr("Discard the recording?") : qsTr("Discard the changes?")
        standardButtons: Dialog.Discard | Dialog.Cancel
        Label { text: root.temporary ? qsTr("It has not been saved and will be deleted.") : qsTr("The marks and the trim are not saved.") }
        onDiscarded: {
            root.discardConfirmed = true
            close()
            root.close()
        }
    }

    Shortcut { sequence: "Ctrl+S"; onActivated: root.save() }
    Shortcut { sequence: "Ctrl+Shift+S"; onActivated: root.saveAs() }
    Shortcut { sequence: "Ctrl+W"; onActivated: root.close() }
    Shortcut { sequence: "Ctrl+Z"; enabled: !marks.editingText; onActivated: root.document.undo() }
    Shortcut { sequences: ["Ctrl+Shift+Z", "Ctrl+Y"]; enabled: !marks.editingText; onActivated: root.document.redo() }
    Shortcut { sequence: "Space"; enabled: !marks.editingText; onActivated: root.togglePlay() }
    Shortcut { sequence: ","; enabled: !marks.editingText; onActivated: root.stepFrame(-1) }
    Shortcut { sequence: "."; enabled: !marks.editingText; onActivated: root.stepFrame(1) }
    Shortcut { sequence: "Home"; enabled: !marks.editingText; onActivated: { player.pause(); player.position = root.trimStart } }
    Shortcut { sequence: "End"; enabled: !marks.editingText; onActivated: { player.pause(); player.position = root.effectiveEnd } }
    Shortcut { sequence: "["; enabled: !marks.editingText; onActivated: root.trimStart = Math.min(player.position, root.effectiveEnd - 100) }
    Shortcut { sequence: "]"; enabled: !marks.editingText; onActivated: root.trimEnd = Math.max(player.position, root.trimStart + 100) }
    Shortcut {
        sequences: ["Delete", "Backspace"]
        enabled: !marks.editingText
        onActivated: root.document.removeAnnotation(root.document.selectedIndex)
    }
    Shortcut {
        sequence: "Escape"
        onActivated: {
            if (marks.editingText)
                marks.cancelText()
            else if (root.document.drawing)
                root.document.cancelAnnotation()
            else
                root.document.selectedIndex = -1
        }
    }
    Repeater {
        model: [
            { key: "V", tool: Document.Select },
            { key: "P", tool: Document.Pen },
            { key: "L", tool: Document.Line },
            { key: "A", tool: Document.Arrow },
            { key: "R", tool: Document.Rectangle },
            { key: "O", tool: Document.Ellipse },
            { key: "M", tool: Document.Marker },
            { key: "T", tool: Document.Text },
            { key: "X", tool: Document.Pixelate },
            { key: "N", tool: Document.Counter },
            { key: "D", tool: Document.Measure },
        ]
        Item {
            id: keyHolder
            required property var modelData
            Shortcut {
                sequence: keyHolder.modelData.key
                enabled: !marks.editingText
                onActivated: root.tool = keyHolder.modelData.tool
            }
        }
    }
}
