pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Guckste
import Guckste.Core
import Guckste.Ui

// A screenshot at any zoom, to draw on and measure, and to combine with
// further screenshots and images. What is copied or saved is what is seen:
// the canvas enlarged pixel for pixel, the marks enlarged along with it.
// The layer panel lists every mark and image.
ApplicationWindow {
    id: root

    required property Document document

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

    readonly property string fileName: document.filePath !== "" ? document.filePath.split("/").pop() : qsTr("Screenshot")

    // Room for the toolbar and the canvas to start with; then the window
    // keeps its size while images come and go.
    width: Math.min(Screen.desktopAvailableWidth * 0.9,
                    Math.max(760, toolbar.implicitWidth, document.canvasWidth + 60 + (showLayers ? layers.width : 0)))
    height: Math.max(520, Math.min(document.canvasHeight + 140, Screen.desktopAvailableHeight * 0.9))
    title: `${fileName}${document.modified ? " •" : ""} — Guckste`
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

    function rememberTools() {
        Preferences.color = toolColor
        Preferences.size = size
        Preferences.textSize = textSize
    }

    function copy() {
        marks.commitText()
        document.copyToClipboard(exportScale)
        rememberTools()
        status.flash(qsTr("Copied at %1 %").arg(Math.round(exportScale * 100)))
    }

    function save() {
        marks.commitText()
        if (document.filePath !== "" && exportScale === 1) {
            if (document.save(document.filePath, 1))
                status.flash(qsTr("Saved %1").arg(document.filePath))
            return
        }
        saveDialog.open()
    }

    function saveAs() {
        marks.commitText()
        saveDialog.open()
    }

    function toolHint(): string {
        switch (tool) {
        case Document.Select: return qsTr("Click a mark or image to select it, drag it or its handles, double-click a text to edit it. Delete removes it. Space drags the view, Ctrl+wheel zooms.")
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
        if (document.modified && !discardConfirmed) {
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

    Component.onCompleted: {
        width = width
        height = height
        fitIfNeeded()
    }

    // After an image was added: all of the canvas in view, if it was not.
    function fitIfNeeded() {
        Qt.callLater(() => {
            if (document.canvasWidth * zoom > flick.width || document.canvasHeight * zoom > flick.height)
                zoomToFit()
        })
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
                    text: qsTr("Paste (Ctrl+V)")
                    onTriggered: root.addFromClipboard()
                }
            }
        }
        IconButton {
            iconName: "layers"
            tip: qsTr("Layers")
            checkable: true
            checked: root.showLayers
            onClicked: root.showLayers = checked
        }
        ToolSeparator {}
        IconButton { iconName: "zoom-out"; tip: qsTr("Zoom out (Ctrl+-)"); onClicked: root.zoomStep(-1) }
        ToolButton {
            text: `${Math.round(root.zoom * 100)} %`
            font.pixelSize: 12
            implicitWidth: 56
            focusPolicy: Qt.NoFocus
            onClicked: zoomMenu.open()
            ToolTip.text: qsTr("Zoom")
            ToolTip.visible: hovered
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
            text: "1:1"
            font.pixelSize: 12
            focusPolicy: Qt.NoFocus
            onClicked: root.zoomAt(1, flick.width / 2, flick.height / 2)
            ToolTip.text: qsTr("Actual size (Ctrl+1)")
            ToolTip.visible: hovered
        }
        IconButton {
            iconName: "grid-3x3"
            tip: qsTr("Pixel grid from 800 % (G)")
            checkable: true
            checked: root.showGrid
            onClicked: root.showGrid = checked
        }
        ToolSeparator {}
        IconButton { iconName: "copy"; tip: qsTr("Copy (Ctrl+C)"); onClicked: root.copy() }
        IconButton { iconName: "save"; tip: qsTr("Save (Ctrl+S)"); onClicked: root.save() }
        IconButton { iconName: "download"; tip: qsTr("Save as… (Ctrl+Shift+S)"); onClicked: root.saveAs() }
        IconButton { iconName: "folder-open"; tip: qsTr("Open an image… (Ctrl+O)"); onClicked: openDialog.open() }
        IconButton { iconName: "settings"; tip: qsTr("Settings"); onClicked: App.openSettings() }
    }

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

    Menu {
        id: guideMenu
        MenuItem {
            text: qsTr("Remove all guides")
            enabled: root.document.guides.length > 0
            onTriggered: root.document.clearGuides()
        }
    }

    readonly property int rulerSize: 20

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

    // Images dropped on the window are added.
    DropArea {
        anchors.fill: flick
        keys: ["text/uri-list"]
        onDropped: drop => {
            let added = 0
            for (const url of drop.urls) {
                const path = url.toString().replace(/^file:\/\//, "")
                if (root.document.addImageFile(decodeURIComponent(path)) >= 0)
                    ++added
            }
            if (added > 0) {
                root.pickTool(Document.Select)
                root.fitIfNeeded()
                drop.acceptProposedAction()
            } else {
                status.flash(qsTr("Nothing there could be read as an image"))
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
                Layout.fillWidth: true
                font.pixelSize: 12
                elide: Text.ElideRight
                color: status.message !== "" ? Theme.accent : palette.placeholderText
                text: status.message !== "" ? status.message : root.toolHint()
            }
            CheckBox {
                text: qsTr("Snap to guides")
                font.pixelSize: 12
                checked: root.snapToGuides
                onToggled: root.snapToGuides = checked
                ToolTip.text: qsTr("Line up what is drawn, moved or resized with the guides dragged out of the rulers")
                ToolTip.visible: hovered
            }
            CheckBox {
                text: qsTr("Export guides")
                font.pixelSize: 12
                checked: root.exportGuides
                onToggled: root.exportGuides = checked
                ToolTip.text: qsTr("Draw the guides into copies and saved images")
                ToolTip.visible: hovered
            }
            CheckBox {
                text: qsTr("Export at %1 %").arg(Math.round(root.zoom * 100))
                font.pixelSize: 12
                checked: root.exportAtZoom
                onToggled: root.exportAtZoom = checked
                ToolTip.text: qsTr("Copy and save the image enlarged as on screen; off for its own size")
                ToolTip.visible: hovered
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
        id: openDialog
        title: qsTr("Open image")
        nameFilters: [qsTr("Images and videos (*.png *.jpg *.jpeg *.bmp *.webp *.gif *.mp4 *.webm *.mkv *.avi *.mov)"), qsTr("All files (*)")]
        currentFolder: "file://" + Preferences.saveDirectory
        onAccepted: App.openEditor(selectedFile.toString().replace(/^file:\/\//, ""))
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
        title: qsTr("Discard changes?")
        standardButtons: Dialog.Discard | Dialog.Cancel
        Label { text: qsTr("The changes to this screenshot are not saved.") }
        onDiscarded: {
            root.discardConfirmed = true
            close()
            root.close()
        }
    }

    Shortcut { sequence: "Ctrl+C"; enabled: !marks.editingText; onActivated: root.copy() }
    Shortcut { sequence: "Ctrl+S"; onActivated: root.save() }
    Shortcut { sequence: "Ctrl+Shift+S"; onActivated: root.saveAs() }
    Shortcut { sequence: "Ctrl+O"; onActivated: openDialog.open() }
    Shortcut { sequence: "Ctrl+V"; enabled: !marks.editingText; onActivated: root.addFromClipboard() }
    Shortcut { sequence: "Ctrl+W"; onActivated: root.close() }
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
