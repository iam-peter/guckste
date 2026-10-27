pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Guckste
import Guckste.Core
import Guckste.Ui

// One editor for screenshots and videos: a canvas at any zoom to draw on
// and measure, to put screenshots, images and video clips together on.
// Clips play side by side where they are placed and one after the other
// where they are placed in time; with one in it, a timeline below lists
// every layer with when it shows, and the canvas is saved as a video.
// Any frame can be taken out as an image to go on with. What is copied
// or saved is what is seen: the canvas pixel for pixel, the marks along
// with it. The layer panel lists every layer.
ApplicationWindow {
    id: root

    required property Document document
    // A recording not saved anywhere yet: deleted when the window closes,
    // unless saving moved it into place.
    property string temporaryVideo: ""
    // A recording's name: its folder in the screenshot folder, where its
    // video and stills are saved together.
    property string recordingName: ""
    property bool saved: false
    readonly property bool videoMode: document.hasVideo
    // The part of the time a video is saved from.
    property int trimStart: 0
    property int trimEnd: -1
    readonly property int effectiveEnd: trimEnd < 0 ? playback.duration : Math.min(trimEnd, playback.duration)
    readonly property bool unsaved: document.modified || (temporaryVideo !== "" && !saved)
    // Opened with nothing, from the tray or "guckste open".
    readonly property bool empty: document.annotationCount === 0 && document.imageWidth === 0
    // The stills of the part kept.
    readonly property int keptStills: videoMode && document.revision >= 0
                                      ? document.stillCount(trimStart, effectiveEnd) : 0
    // The playhead is on a lossless still of a recording.
    readonly property bool atStill: document.revision >= 0 && document.time >= 0 && document.stillShown()

    readonly property list<real> zoomLevels: [0.25, 0.5, 0.75, 1, 1.5, 2, 3, 4, 6, 8, 12, 16, 24, 32]
    property real zoom: 1
    property int tool: Document.Select
    property color toolColor: Preferences.color
    property real size: Preferences.size
    property real textSize: Preferences.textSize
    property bool showGrid: Preferences.showGrid
    // Export at the zoom on screen, or at the image's own size.
    property bool exportAtZoom: true
    readonly property real exportScale: exportAtZoom ? zoom : 1
    property point cursor: Qt.point(-1, -1)
    property bool discardConfirmed: false
    property bool showLayers: true
    property bool snapToGuides: Preferences.snapToGuides
    property bool exportGuides: Preferences.exportGuides
    onSnapToGuidesChanged: Preferences.snapToGuides = snapToGuides
    onExportGuidesChanged: Preferences.exportGuides = exportGuides
    readonly property bool pointerInside: cursor.x !== -1 || cursor.y !== -1
    // The canvas as last laid out, to keep the view still when it grows.
    property rect shownCanvas: Qt.rect(document.canvasX, document.canvasY, document.canvasWidth, document.canvasHeight)

    readonly property string fileName: document.name !== "" ? document.name
                                       : videoMode ? qsTr("Recording") : qsTr("Screenshot")

    // Room for the toolbar and the canvas to start with; then the window
    // keeps its size while images come and go.
    width: Math.min(Screen.desktopAvailableWidth * 0.9,
                    Math.max(760, toolbar.implicitWidth, document.canvasWidth + 60 + (showLayers ? layers.width : 0)))
    height: Math.max(520, Math.min(document.canvasHeight + 140 + (videoMode ? 170 : 0),
                                   Screen.desktopAvailableHeight * 0.9))
    title: `${fileName}${unsaved ? " •" : ""} — Guckste`
    color: Qt.darker(palette.window, 1.25)

    function clamp(value: real, low: real, high: real): real {
        return Math.max(low, Math.min(high, value))
    }

    // Zooms keeping the image point under (viewX, viewY) where it is.
    function zoomAt(newZoom: real, viewX: real, viewY: real) {
        newZoom = clamp(newZoom, 0.1, 64)
        const p = marks.toImage(viewX, viewY)
        zoom = newZoom
        const contentWidth = Math.max(flick.width, document.canvasWidth * zoom)
        const contentHeight = Math.max(flick.height, document.canvasHeight * zoom)
        const canvasX = (contentWidth - document.canvasWidth * zoom) / 2
        const canvasY = (contentHeight - document.canvasHeight * zoom) / 2
        flick.contentX = Math.round(clamp((p.x - document.canvasX) * zoom + canvasX - viewX, 0, contentWidth - flick.width))
        flick.contentY = Math.round(clamp((p.y - document.canvasY) * zoom + canvasY - viewY, 0, contentHeight - flick.height))
    }

    function zoomStep(direction: int) {
        let next = zoom
        if (direction > 0) {
            next = zoomLevels.find(level => level > zoom + 1e-6) ?? zoomLevels[zoomLevels.length - 1]
        } else {
            const lower = zoomLevels.filter(level => level < zoom - 1e-6)
            next = lower.length > 0 ? lower[lower.length - 1] : zoomLevels[0]
        }
        zoomAt(next, flick.width / 2, flick.height / 2)
    }

    function zoomToFit() {
        if (document.canvasWidth === 0 || document.canvasHeight === 0)
            return
        const fit = Math.min((flick.width - 24) / document.canvasWidth, (flick.height - 24) / document.canvasHeight)
        zoom = clamp(Math.min(fit, 1), 0.1, 64)
        flick.contentX = 0
        flick.contentY = 0
    }

    // The canvas grew or shrank on its top or left: shift the view along,
    // so what is on screen stays where it is.
    function followCanvas() {
        const now = Qt.rect(document.canvasX, document.canvasY, document.canvasWidth, document.canvasHeight)
        const dx = (shownCanvas.x - now.x) * zoom
        const dy = (shownCanvas.y - now.y) * zoom
        shownCanvas = now
        Qt.callLater(() => {
            if (!flick)
                return
            flick.contentX = Math.round(clamp(flick.contentX + dx, 0, Math.max(0, flick.contentWidth - flick.width)))
            flick.contentY = Math.round(clamp(flick.contentY + dy, 0, Math.max(0, flick.contentHeight - flick.height)))
        })
    }

    // The toolbar sets a text's pixel size instead of the stroke size.
    readonly property bool textMode: document.revision >= 0
                                     && (tool === Document.Text || marks.editingIndex >= 0
                                         || (tool === Document.Select && document.selectedIndex >= 0
                                             && document.annotationTool(document.selectedIndex) === Document.Text))

    function addFromClipboard() {
        if (document.pasteImage() < 0) {
            status.flash(qsTr("No image on the clipboard"))
        } else {
            pickTool(Document.Select)
            fitIfNeeded()
        }
    }

    function pickTool(picked: int) {
        tool = picked
        if (picked !== Document.Select)
            document.selectedIndex = -1
    }

    function time(ms: int): string {
        const s = Math.floor(Math.max(0, ms) / 1000)
        const m = Math.floor(s / 60)
        const two = n => (n < 10 ? "0" : "") + n
        return `${two(m)}:${two(s % 60)}`
    }

    function togglePlay() {
        if (playback.playing) {
            playback.pause()
            return
        }
        if (playback.position >= effectiveEnd - 50 || playback.position < trimStart)
            playback.position = trimStart
        playback.play()
    }

    function seek(ms: int) {
        playback.pause()
        playback.position = ms
    }

    // Seeks to the next still after the playhead, or the last before it.
    function stepStill(direction: int) {
        const times = timeline.stills.map(s => s.time)
        const next = direction > 0 ? times.find(t => t > playback.position)
                                   : times.reverse().find(t => t < playback.position)
        if (next !== undefined)
            seek(next)
    }

    function stepFrame(frames: int) {
        seek(playback.position + Math.round(frames * 1000 / Math.max(1, playback.frameRate)))
    }

    // The frame shown, as an image of its own in a new editor.
    function takeFrame() {
        marks.commitText()
        playback.pause()
        App.openFrame(document)
    }

    // Where Save puts the video: a recording into its folder.
    function videoTarget(): string {
        return recordingName !== "" ? Preferences.recordingVideoPath(recordingName, App.recordingSuffix())
                                    : Preferences.nextVideoPath(App.recordingSuffix())
    }

    // The stills of the part kept, into the recording's folder.
    function saveStills() {
        const folder = recordingName !== "" ? Preferences.recordingFolder(recordingName)
                                            : Preferences.saveDirectory
        const saved = document.saveStills(folder, trimStart, effectiveEnd)
        status.flash(qsTr("Saved %n still(s) to %1", "", saved).arg(folder))
    }

    function saveVideo(path: string) {
        marks.commitText()
        playback.pause()
        if (exporter.running)
            return
        App.exportVideo(exporter, path, document, trimStart, trimEnd, temporaryVideo)
    }

    function rememberTools() {
        Preferences.color = toolColor
        Preferences.size = size
        Preferences.textSize = textSize
    }

    function copy() {
        marks.commitText()
        if (videoMode) {
            playback.pause()
            document.copyToClipboard(1)
            status.flash(qsTr("Copied the frame at %1").arg(time(playback.position)))
            return
        }
        document.copyToClipboard(exportScale)
        rememberTools()
        status.flash(qsTr("Copied at %1 %").arg(Math.round(exportScale * 100)))
    }

    function save() {
        marks.commitText()
        if (videoMode) {
            rememberTools()
            saveVideo(videoTarget())
            return
        }
        if (document.filePath !== "" && exportScale === 1) {
            if (document.save(document.filePath, 1))
                status.flash(qsTr("Saved %1").arg(document.filePath))
            return
        }
        saveDialog.open()
    }

    function saveAs() {
        marks.commitText()
        if (videoMode) {
            playback.pause()
            // Asked for here, not bound: the first ask probes the encoder,
            // which opening an editor should not wait for.
            const suffix = App.recordingSuffix()
            videoSaveDialog.nameFilters = [qsTr("Video (*.%1)").arg(suffix)]
            videoSaveDialog.defaultSuffix = suffix
            videoSaveDialog.selectedFile = "file://" + videoTarget()
            videoSaveDialog.open()
        } else {
            saveDialog.open()
        }
    }

    function toolHint(): string {
        switch (tool) {
        case Document.Select: return videoMode
            ? qsTr("Click a mark, image or clip to select it, drag it or its handles; locked layers are unlocked in the layer list. Space plays, Ctrl+wheel zooms.")
            : qsTr("Click a mark or image to select it, drag it or its handles, double-click a text to edit it; locked layers are unlocked in the layer list. Space drags the view, Ctrl+wheel zooms.")
        case Document.Measure: return qsTr("Drag along a row or column of pixels; the mark shows their count. Alt measures at any angle.")
        case Document.Text: return qsTr("Click where the text starts. Enter finishes, Shift+Enter breaks the line.")
        case Document.Line:
        case Document.Arrow: return qsTr("Drag to draw. Shift snaps to 45° steps.")
        case Document.Rectangle:
        case Document.Ellipse: return qsTr("Drag to draw. Shift keeps it square.")
        case Document.Pixelate: return qsTr("Drag over what should not be readable. The size sets the block size.")
        case Document.Counter: return qsTr("Click to place the next number.")
        default: return qsTr("Drag to draw.")
        }
    }

    onClosing: close => {
        if (unsaved && !discardConfirmed) {
            close.accepted = false
            // Closed from the taskbar the window may be behind others or
            // minimized: it comes forward to ask.
            if (visibility === Window.Minimized)
                showNormal()
            raise()
            requestActivate()
            // Made for what is asked, as its text does not change once laid out.
            discardDialog.createObject(root, {
                message: temporaryVideo !== "" && !saved ? qsTr("The recording has not been saved and will be deleted.")
                    : videoMode ? qsTr("The changes to this video are not saved.")
                    : qsTr("The changes to this screenshot are not saved.")
            }).open()
        }
    }

    Component.onCompleted: {
        width = width
        height = height
        fitIfNeeded()
    }

    // After an image was added: all of the canvas in view, if it was not.
    // Once the view has its size, when the window is new.
    property bool fitPending: false
    function fitIfNeeded() {
        fitPending = true
        Qt.callLater(fitNow)
    }
    function fitNow() {
        if (!fitPending || !flick || flick.width <= 24 || flick.height <= 24)
            return
        fitPending = false
        if (document.canvasWidth * zoom > flick.width || document.canvasHeight * zoom > flick.height)
            zoomToFit()
    }

    // What is exported carries the guides when asked to.
    Binding {
        target: root.document
        property: "exportGuides"
        value: root.exportGuides
    }

    Connections {
        target: root.document
        function onCanvasChanged() { root.followCanvas() }
        function onVideoAdded() {
            root.pickTool(Document.Select)
            root.fitIfNeeded()
        }
        function onVideoFailed(path: string) {
            status.flash(qsTr("Could not read %1 as a video").arg(path.split("/").pop()))
        }
    }

    // Plays the clips, and sets the document's time.
    Playback {
        id: playback
        objectName: "playback"
        document: root.document
        stopAt: root.trimEnd
    }

    VideoExporter {
        id: exporter
        objectName: "exporter"
        onFinished: path => {
            // Saved by moving the recording: the clip plays from there now.
            if (root.temporaryVideo !== "" && !App.fileExists(root.temporaryVideo)) {
                root.document.relinkSource(root.temporaryVideo, path)
                root.temporaryVideo = ""
            }
            root.saved = true
            root.document.markSaved()
            // The stills go along, so the flow stays together.
            if (root.recordingName !== "")
                root.document.saveStills(path.substring(0, path.lastIndexOf("/")), root.trimStart, root.effectiveEnd)
            status.flash(qsTr("Saved %1").arg(path))
            if (Preferences.copyPathAfterSave)
                Clipboard.setText(path)
            App.notify(qsTr("Video saved"), path)
        }
        onFailed: message => status.flash(qsTr("Could not save: %1").arg(message))
    }

    header: AnnotationToolbar {
        id: toolbar
        overflow: true
        objectName: "toolbar"
        tool: root.tool
        color: root.toolColor
        size: root.size
        textSize: root.textSize
        textMode: root.textMode
        document: root.document
        onToolPicked: tool => root.pickTool(tool)
        // It restyles the selected mark itself.
        onTextSizePicked: size => root.textSize = size
        onColorPicked: picked => root.toolColor = picked
        onSizePicked: size => root.size = size

        IconButton {
            id: addButton
            iconName: "image-plus"
            tip: qsTr("Add a screenshot or image")
            onClicked: addMenu.open()
            Menu {
                id: addMenu
                y: addButton.height
                MenuItem {
                    text: qsTr("Capture a region…")
                    onTriggered: App.addScreenshotTo(root, root.document)
                }
                MenuItem {
                    text: qsTr("Image file…")
                    onTriggered: addDialog.open()
                }
                MenuItem {
                    text: root.videoMode ? qsTr("Video after the others…") : qsTr("Video…")
                    onTriggered: {
                        addVideoDialog.after = true
                        addVideoDialog.open()
                    }
                }
                MenuItem {
                    text: qsTr("Video beside, at the same time…")
                    enabled: root.videoMode
                    onTriggered: {
                        addVideoDialog.after = false
                        addVideoDialog.open()
                    }
                }
                MenuItem {
                    text: qsTr("Paste (Ctrl+V)")
                    onTriggered: root.addFromClipboard()
                }
            }
        }
        IconButton {
            iconName: "layers"
            tip: qsTr("Layers")
            checked: root.showLayers
            onClicked: root.showLayers = !root.showLayers
        }
        ToolSeparator {}
        IconButton { iconName: "zoom-out"; tip: qsTr("Zoom out (Ctrl+-)"); onClicked: root.zoomStep(-1) }
        ToolButton {
            id: zoomButton
            text: `${Math.round(root.zoom * 100)} %`
            font.pixelSize: 12
            implicitWidth: 56
            focusPolicy: Qt.NoFocus
            onClicked: zoomMenu.open()
            Tip { text: qsTr("Zoom"); visible: zoomButton.hovered }
            Menu {
                id: zoomMenu
                y: parent.height
                Repeater {
                    model: root.zoomLevels
                    MenuItem {
                        required property real modelData
                        text: `${Math.round(modelData * 100)} %`
                        onTriggered: root.zoomAt(modelData, flick.width / 2, flick.height / 2)
                    }
                }
            }
        }
        IconButton { iconName: "zoom-in"; tip: qsTr("Zoom in (Ctrl++)"); onClicked: root.zoomStep(1) }
        IconButton { iconName: "maximize"; tip: qsTr("Fit (Ctrl+0)"); onClicked: root.zoomToFit() }
        ToolButton {
            id: actualSizeButton
            text: "1:1"
            font.pixelSize: 12
            focusPolicy: Qt.NoFocus
            onClicked: root.zoomAt(1, flick.width / 2, flick.height / 2)
            Tip { text: qsTr("Actual size (Ctrl+1)"); visible: actualSizeButton.hovered }
        }
        IconButton {
            iconName: "grid-3x3"
            tip: qsTr("Pixel grid from 800 % (G)")
            // Follows G too, so it is not a toggle of its own.
            checked: root.showGrid
            onClicked: root.showGrid = !root.showGrid
        }
        ToolSeparator {}
        IconButton {
            objectName: "frameButton"
            property bool available: root.videoMode
            iconName: "camera"
            tip: root.atStill ? qsTr("Open this lossless still as an image (Ctrl+Shift+F)")
                              : qsTr("Open this frame as an image (Ctrl+Shift+F)")
            onClicked: root.takeFrame()
        }
        IconButton {
            objectName: "stillsSheetButton"
            property bool available: root.keptStills > 0
            iconName: "layout-grid"
            tip: qsTr("Open the stills of the part kept as one image")
            onClicked: {
                playback.pause()
                App.openStills(root.document, root.trimStart, root.effectiveEnd)
            }
        }
        IconButton {
            objectName: "saveStillsButton"
            property bool available: root.keptStills > 0
            iconName: "image"
            tip: qsTr("Save the %n still(s) of the part kept to the recording's folder", "", root.keptStills)
            onClicked: root.saveStills()
        }
        IconButton {
            iconName: "copy"
            tip: root.videoMode ? qsTr("Copy this frame (Ctrl+C)") : qsTr("Copy (Ctrl+C)")
            onClicked: root.copy()
        }
        IconButton {
            iconName: "save"
            tip: root.videoMode ? qsTr("Save the video to the screenshot folder (Ctrl+S)") : qsTr("Save (Ctrl+S)")
            enabled: !exporter.running
            onClicked: root.save()
        }
        IconButton {
            iconName: "download"
            tip: qsTr("Save as… (Ctrl+Shift+S)")
            enabled: !exporter.running
            onClicked: root.saveAs()
        }
        IconButton { iconName: "folder-open"; tip: qsTr("Open an image… (Ctrl+O)"); onClicked: openDialog.open() }
        IconButton { iconName: "settings"; tip: qsTr("Settings"); onClicked: App.openSettings() }
    }

    // Rulers in image pixels. Dragging out of one makes a guide: from the
    // top a horizontal one, from the left a vertical one.
    component GuideRuler: Ruler {
        id: ruler
        background: Qt.darker(root.palette.window, 1.1)
        foreground: root.palette.windowText
        zoom: root.zoom

        MouseArea {
            id: rulerMouse
            property int guide: -1
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            cursorShape: ruler.vertical ? Qt.SplitHCursor : Qt.SplitVCursor

            function position(x: real, y: real): real {
                const p = mapToItem(marks, x, y)
                const image = marks.toImage(p.x, p.y)
                return ruler.vertical ? image.x : image.y
            }

            onPressed: mouse => {
                if (mouse.button === Qt.RightButton) {
                    guideMenu.popup()
                    return
                }
                guide = root.document.addGuide(ruler.vertical, position(mouse.x, mouse.y))
            }
            onPositionChanged: mouse => {
                if (guide >= 0)
                    root.document.moveGuide(guide, position(mouse.x, mouse.y))
            }
            onReleased: mouse => {
                if (guide < 0)
                    return
                // Let go over the ruler again: no guide after all.
                const p = mapToItem(marks, mouse.x, mouse.y)
                if (p.x < 0 || p.y < 0 || p.x > marks.width || p.y > marks.height)
                    root.document.removeGuide(guide)
                guide = -1
            }
        }
    }

    readonly property int rulerSize: 20

    // The canvas above, and with video the play controls, the timeline
    // and the lanes below, with a divider between them to drag.
    SplitView {
        id: split
        anchors.fill: parent
        orientation: Qt.Vertical

        Item {
            id: workArea
            SplitView.fillHeight: true

            LayerPanel {
                id: layers
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 240
                visible: root.showLayers
                document: root.document
                onPicked: root.tool = Document.Select
                onEditRequested: index => {
                    root.tool = Document.Select
                    marks.editText(index)
                }
            }

            Menu {
                id: guideMenu
                MenuItem {
                    text: qsTr("Remove all guides")
                    enabled: root.document.guides.length > 0
                    onTriggered: root.document.clearGuides()
                }
            }

            Rectangle {
                // The corner where the rulers meet.
                x: 0
                y: 0
                width: root.rulerSize
                height: root.rulerSize
                color: Qt.darker(root.palette.window, 1.1)
            }
            GuideRuler {
                id: topRuler
                objectName: "topRuler"
                x: root.rulerSize
                y: 0
                width: flick.width
                height: root.rulerSize
                origin: marks.originX
                pointer: root.pointerInside ? root.cursor.x : NaN
            }
            GuideRuler {
                id: leftRuler
                objectName: "leftRuler"
                vertical: true
                x: 0
                y: root.rulerSize
                width: root.rulerSize
                height: flick.height
                origin: marks.originY
                pointer: root.pointerInside ? root.cursor.y : NaN
            }

            Flickable {
                id: flick
                anchors.left: parent.left
                anchors.leftMargin: root.rulerSize
                anchors.top: parent.top
                anchors.topMargin: root.rulerSize
                anchors.bottom: parent.bottom
                anchors.right: root.showLayers ? layers.left : parent.right
                contentWidth: Math.max(width, Math.round(root.document.canvasWidth * root.zoom))
                contentHeight: Math.max(height, Math.round(root.document.canvasHeight * root.zoom))
                // The scroll bars move by fractions too.
                onContentXChanged: if (contentX !== Math.round(contentX)) contentX = Math.round(contentX)
                onContentYChanged: if (contentY !== Math.round(contentY)) contentY = Math.round(contentY)
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                interactive: false
                onWidthChanged: root.fitNow()
                onHeightChanged: root.fitNow()
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

                // The canvas: a checkerboard where nothing covers it, which stays
                // transparent in what is exported.
                Item {
                    id: canvasView
                    // On whole pixels: an image shown unsmoothed at a half-pixel
                    // offset doubles some pixels and drops others.
                    x: Math.round((flick.contentWidth - width) / 2)
                    y: Math.round((flick.contentHeight - height) / 2)
                    width: root.document.canvasWidth * root.zoom
                    height: root.document.canvasHeight * root.zoom

                    Image {
                        anchors.fill: parent
                        fillMode: Image.Tile
                        source: "data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16'>"
                                + "<rect width='16' height='16' fill='%23ffffff'/><rect width='8' height='8' fill='%23d8d8d8'/>"
                                + "<rect x='8' y='8' width='8' height='8' fill='%23d8d8d8'/></svg>"
                    }

                    Image {
                        id: image
                        x: Math.round(-root.document.canvasX * root.zoom)
                        y: Math.round(-root.document.canvasY * root.zoom)
                        width: root.document.imageWidth * root.zoom
                        height: root.document.imageHeight * root.zoom
                        source: root.document.imageSource
                        cache: false
                        // Enlarged pixels stay blocks; shrinking averages them.
                        smooth: root.zoom < 1
                        mipmap: root.zoom < 1
                        fillMode: Image.Stretch
                    }
                }
            }

            // An empty editor says what to do.
            Column {
                objectName: "emptyHint"
                visible: root.empty
                anchors.centerIn: flick
                spacing: 14
                Icon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    name: "image-plus"
                    size: 40
                    color: root.palette.placeholderText
                }
                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    horizontalAlignment: Text.AlignHCenter
                    color: root.palette.placeholderText
                    text: qsTr("Open an image or a video, paste one, or drop it here.")
                }
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 8
                    Button {
                        text: qsTr("Open… (Ctrl+O)")
                        onClicked: openDialog.open()
                    }
                    Button {
                        text: qsTr("Paste (Ctrl+V)")
                        onClicked: root.addFromClipboard()
                    }
                    Button {
                        text: qsTr("Capture the screen")
                        onClicked: App.addScreenshotTo(root, root.document)
                    }
                }
            }

            // Images dropped on the window are added, and videos after the
            // other clips.
            DropArea {
                anchors.fill: flick
                keys: ["text/uri-list"]
                onDropped: drop => {
                    let added = 0
                    for (const url of drop.urls) {
                        const path = decodeURIComponent(url.toString().replace(/^file:\/\//, ""))
                        if (App.isVideoFile(path)) {
                            root.document.addVideoFile(path, true)
                            ++added
                        } else if (root.document.addImageFile(path) >= 0) {
                            ++added
                        }
                    }
                    if (added > 0) {
                        root.pickTool(Document.Select)
                        root.fitIfNeeded()
                        drop.acceptProposedAction()
                    } else {
                        status.flash(qsTr("Nothing there could be read as an image or video"))
                    }
                }
            }

            DrawingArea {
                id: marks
                objectName: "drawingArea"
                anchors.fill: flick
                document: root.document
                zoom: root.zoom
                originX: (flick.contentX - canvasView.x) / root.zoom + root.document.canvasX
                originY: (flick.contentY - canvasView.y) / root.zoom + root.document.canvasY
                tool: root.tool
                toolColor: root.toolColor
                size: root.size
                textSize: root.textSize
                showGrid: root.showGrid
                gridMinZoom: 8
                showGuides: true
                snapToGuides: root.snapToGuides
                panEnabled: true
                onCursorChanged: root.cursor = cursor
                onPanned: (dx, dy) => {
                    flick.contentX = Math.round(root.clamp(flick.contentX - dx, 0, flick.contentWidth - flick.width))
                    flick.contentY = Math.round(root.clamp(flick.contentY - dy, 0, flick.contentHeight - flick.height))
                }
                onZoomRequested: (factor, x, y) => root.zoomAt(root.zoom * factor, x, y)
                onScrolled: (dx, dy) => {
                    flick.contentX = Math.round(root.clamp(flick.contentX - dx, 0, flick.contentWidth - flick.width))
                    flick.contentY = Math.round(root.clamp(flick.contentY - dy, 0, flick.contentHeight - flick.height))
                }
            }
        }

        Pane {
            id: bottomPanel
            objectName: "bottomPanel"
            visible: root.videoMode
            SplitView.preferredHeight: Math.min(implicitHeight, split.height / 2)
            SplitView.minimumHeight: transport.implicitHeight + topPadding + bottomPadding
            padding: 6
            background: Rectangle { color: root.palette.window }

            ColumnLayout {
                anchors.fill: parent
                spacing: 4

                RowLayout {
                    id: transport
                    Layout.fillWidth: true
                    spacing: 6
                    IconButton { iconName: "skip-back"; tip: qsTr("To the start (Home)"); onClicked: root.seek(root.trimStart) }
                    IconButton { iconName: "step-back"; tip: qsTr("A frame back (,)"); onClicked: root.stepFrame(-1) }
                    IconButton {
                        objectName: "playButton"
                        iconName: playback.playing ? "pause" : "play"
                        tip: qsTr("Play or pause (Space)")
                        onClicked: root.togglePlay()
                    }
                    IconButton { iconName: "step-forward"; tip: qsTr("A frame on (.)"); onClicked: root.stepFrame(1) }
                    IconButton { iconName: "skip-forward"; tip: qsTr("To the end (End)"); onClicked: root.seek(root.effectiveEnd) }
                    Label {
                        font.pixelSize: 12
                        font.family: "monospace"
                        text: `${root.time(playback.position)} / ${root.time(playback.duration)}`
                    }

                    Timeline {
                        id: timeline
                        objectName: "timeline"
                        Layout.fillWidth: true
                        duration: playback.duration
                        // Room past the end to drag into; held while a
                        // lane is dragged.
                        span: lanes.heldSpan > 0 ? lanes.heldSpan
                                                 : playback.duration + Math.max(2000, Math.round(playback.duration * 0.15))
                        position: playback.position
                        trimStart: root.trimStart
                        trimEnd: root.trimEnd
                        stills: root.document.revision >= 0 ? root.document.stillMarks() : []
                        onSeekRequested: ms => root.seek(ms)
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
                    position: playback.position
                    trimStart: root.trimStart
                    trimEnd: root.trimEnd
                    onPicked: root.tool = Document.Select
                    onSeekRequested: ms => root.seek(ms)
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
            interval: 4000
            onTriggered: status.message = ""
        }

        padding: 4
        RowLayout {
            anchors.fill: parent
            spacing: 12

            Label {
                readonly property bool inside: root.cursor.x >= root.document.canvasX && root.cursor.y >= root.document.canvasY
                                               && root.cursor.x < root.document.canvasX + root.document.canvasWidth
                                               && root.cursor.y < root.document.canvasY + root.document.canvasHeight
                Layout.preferredWidth: 130
                font.pixelSize: 12
                text: inside ? qsTr("x %1  y %2").arg(Math.floor(root.cursor.x)).arg(Math.floor(root.cursor.y)) : ""
            }
            RowLayout {
                spacing: 4
                readonly property color pixel: root.document.revision >= 0
                                               ? root.document.pixelAt(Math.floor(root.cursor.x), Math.floor(root.cursor.y)) : "transparent"
                visible: root.cursor.x !== -1 || root.cursor.y !== -1
                Rectangle {
                    implicitWidth: 12
                    implicitHeight: 12
                    radius: 2
                    color: parent.pixel
                    border.color: "#80808080"
                }
                Label {
                    font.pixelSize: 12
                    font.family: "monospace"
                    text: parent.pixel.toString()
                }
            }
            Label {
                font.pixelSize: 12
                text: qsTr("%1 × %2 px").arg(root.document.canvasWidth).arg(root.document.canvasHeight)
            }
            Label {
                font.pixelSize: 12
                visible: root.videoMode && (root.trimStart > 0 || root.trimEnd >= 0)
                text: qsTr("Keeps %1 – %2").arg(root.time(root.trimStart)).arg(root.time(root.effectiveEnd))
            }
            Label {
                Layout.fillWidth: true
                font.pixelSize: 12
                elide: Text.ElideRight
                color: status.message !== "" ? Theme.accent : palette.placeholderText
                text: status.message !== "" ? status.message
                    : exporter.running ? qsTr("Writing the video…")
                    : root.toolHint()
            }
            ProgressBar {
                visible: exporter.running
                Layout.preferredWidth: 160
                from: 0
                to: 1
                value: exporter.progress
            }
            ToolButton {
                visible: exporter.running
                text: qsTr("Cancel")
                font.pixelSize: 12
                focusPolicy: Qt.NoFocus
                onClicked: exporter.cancel()
            }
            CheckBox {
                id: snapBox
                text: qsTr("Snap to guides")
                font.pixelSize: 12
                checked: root.snapToGuides
                onToggled: root.snapToGuides = checked
                Tip {
                    text: qsTr("Line up what is drawn, moved or resized with the guides dragged out of the rulers")
                    visible: snapBox.hovered
                }
            }
            CheckBox {
                id: exportGuidesBox
                visible: !root.videoMode
                text: qsTr("Export guides")
                font.pixelSize: 12
                checked: root.exportGuides
                onToggled: root.exportGuides = checked
                Tip { text: qsTr("Draw the guides into copies and saved images"); visible: exportGuidesBox.hovered }
            }
            CheckBox {
                id: exportAtZoomBox
                visible: !root.videoMode
                text: qsTr("Export at %1 %").arg(Math.round(root.zoom * 100))
                font.pixelSize: 12
                checked: root.exportAtZoom
                onToggled: root.exportAtZoom = checked
                Tip {
                    text: qsTr("Copy and save the image enlarged as on screen; off for its own size")
                    visible: exportAtZoomBox.hovered
                }
            }
        }
    }

    FileDialog {
        id: saveDialog
        title: qsTr("Save screenshot")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("PNG image (*.png)"), qsTr("JPEG image (*.jpg)")]
        defaultSuffix: "png"
        currentFolder: "file://" + Preferences.saveDirectory
        selectedFile: "file://" + Preferences.nextSavePath()
        onAccepted: {
            const path = selectedFile.toString().replace(/^file:\/\//, "")
            if (root.document.save(path, root.exportScale))
                status.flash(qsTr("Saved %1 at %2 %").arg(path).arg(Math.round(root.exportScale * 100)))
            else
                status.flash(qsTr("Could not save %1").arg(path))
        }
    }

    FileDialog {
        id: addDialog
        title: qsTr("Add images")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.bmp *.webp *.gif)"), qsTr("All files (*)")]
        currentFolder: "file://" + Preferences.saveDirectory
        onAccepted: {
            for (const file of selectedFiles)
                root.document.addImageFile(decodeURIComponent(file.toString().replace(/^file:\/\//, "")))
            root.pickTool(Document.Select)
            root.fitIfNeeded()
        }
    }

    FileDialog {
        id: addVideoDialog
        // After the clips there are, or beside them from the start.
        property bool after: true
        title: after ? qsTr("Add a video after the others") : qsTr("Add a video beside the others")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Videos (*.mp4 *.webm *.mkv *.avi *.mov *.m4v)"), qsTr("All files (*)")]
        currentFolder: "file://" + Preferences.saveDirectory
        onAccepted: root.document.addVideoFile(decodeURIComponent(selectedFile.toString().replace(/^file:\/\//, "")), after)
    }

    FileDialog {
        id: videoSaveDialog
        title: qsTr("Save video")
        fileMode: FileDialog.SaveFile
        currentFolder: "file://" + Preferences.saveDirectory
        onAccepted: root.saveVideo(selectedFile.toString().replace(/^file:\/\//, ""))
    }

    FileDialog {
        id: openDialog
        title: qsTr("Open image")
        nameFilters: [qsTr("Images and videos (*.png *.jpg *.jpeg *.bmp *.webp *.gif *.mp4 *.webm *.mkv *.avi *.mov)"), qsTr("All files (*)")]
        currentFolder: "file://" + Preferences.saveDirectory
        onAccepted: {
            // Opened in a window of its own; an empty editor gives way to it.
            if (App.openEditor(decodeURIComponent(selectedFile.toString().replace(/^file:\/\//, ""))) && root.empty)
                root.close()
        }
    }

    Component {
        id: discardDialog
        Dialog {
            id: dialog
            objectName: "discardDialog"
            required property string message
            anchors.centerIn: Overlay.overlay
            modal: true
            // A window of its own, titled and decorated by the desktop like
            // the settings, rather than a panel drawn into the editor.
            popupType: Popup.Window
            padding: 16
            title: qsTr("Discard changes?")
            standardButtons: Dialog.Discard | Dialog.Cancel
            Label { text: dialog.message }
            onDiscarded: {
                root.discardConfirmed = true
                close()
                root.close()
            }
            onClosed: destroy()
        }
    }

    Shortcut { sequence: "Ctrl+C"; enabled: !marks.editingText; onActivated: root.copy() }
    Shortcut { sequence: "Ctrl+S"; onActivated: root.save() }
    Shortcut { sequence: "Ctrl+Shift+S"; onActivated: root.saveAs() }
    Shortcut { sequence: "Ctrl+O"; onActivated: openDialog.open() }
    Shortcut { sequence: "Ctrl+V"; enabled: !marks.editingText; onActivated: root.addFromClipboard() }
    Shortcut { sequence: "Ctrl+W"; onActivated: root.close() }
    Shortcut { sequence: "Ctrl+Shift+F"; enabled: root.videoMode; onActivated: root.takeFrame() }
    // Playing, with a video; without, Space held drags the view.
    Shortcut { sequence: "Space"; enabled: root.videoMode && !marks.editingText; onActivated: root.togglePlay() }
    Shortcut { sequence: ","; enabled: root.videoMode && !marks.editingText; onActivated: root.stepFrame(-1) }
    Shortcut { sequence: "."; enabled: root.videoMode && !marks.editingText; onActivated: root.stepFrame(1) }
    Shortcut { sequence: "Ctrl+,"; enabled: root.videoMode && !marks.editingText; onActivated: root.stepStill(-1) }
    Shortcut { sequence: "Ctrl+."; enabled: root.videoMode && !marks.editingText; onActivated: root.stepStill(1) }
    Shortcut { sequence: "Home"; enabled: root.videoMode && !marks.editingText; onActivated: root.seek(root.trimStart) }
    Shortcut { sequence: "End"; enabled: root.videoMode && !marks.editingText; onActivated: root.seek(root.effectiveEnd) }
    Shortcut {
        sequence: "["
        enabled: root.videoMode && !marks.editingText
        onActivated: root.trimStart = Math.min(playback.position, root.effectiveEnd - 100)
    }
    Shortcut {
        sequence: "]"
        enabled: root.videoMode && !marks.editingText
        onActivated: root.trimEnd = Math.max(playback.position, root.trimStart + 100)
    }
    Shortcut { sequence: "Ctrl+Z"; enabled: !marks.editingText; onActivated: root.document.undo() }
    Shortcut { sequences: ["Ctrl+Shift+Z", "Ctrl+Y"]; enabled: !marks.editingText; onActivated: root.document.redo() }
    Shortcut { sequences: ["Ctrl++", "Ctrl+="]; onActivated: root.zoomStep(1) }
    Shortcut { sequence: "Ctrl+-"; onActivated: root.zoomStep(-1) }
    Shortcut { sequence: "Ctrl+0"; onActivated: root.zoomToFit() }
    Shortcut { sequence: "Ctrl+1"; onActivated: root.zoomAt(1, flick.width / 2, flick.height / 2) }
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
            { key: "G", action: () => root.showGrid = !root.showGrid },
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
                onActivated: {
                    if (keyHolder.modelData.action)
                        keyHolder.modelData.action()
                    else
                        root.pickTool(keyHolder.modelData.tool)
                }
            }
        }
    }

    // Space held: dragging pans.
    Item {
        focus: !marks.editingText
        Keys.onPressed: event => {
            if (event.key === Qt.Key_Space && !event.isAutoRepeat) {
                marks.spaceHeld = true
                event.accepted = true
            }
        }
        Keys.onReleased: event => {
            if (event.key === Qt.Key_Space && !event.isAutoRepeat) {
                marks.spaceHeld = false
                event.accepted = true
            }
        }
    }
}
