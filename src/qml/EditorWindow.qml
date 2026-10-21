pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Guckste
import Guckste.Core
import Guckste.Ui

// A screenshot at any zoom, to draw on and measure. What is copied or saved
// is what is seen: the image enlarged pixel for pixel, with the marks on
// top at their on-screen size.
ApplicationWindow {
    id: root

    required property Document document

    readonly property list<real> zoomLevels: [0.25, 0.5, 0.75, 1, 1.5, 2, 3, 4, 6, 8, 12, 16, 24, 32]
    property real zoom: 1
    property int tool: Document.Select
    property color toolColor: Preferences.color
    property real size: Preferences.size
    property bool showGrid: Preferences.showGrid
    // Export at the zoom on screen, or at the image's own size.
    property bool exportAtZoom: true
    readonly property real exportScale: exportAtZoom ? zoom : 1
    property point cursor: Qt.point(-1, -1)
    property bool discardConfirmed: false

    readonly property string fileName: document.filePath !== "" ? document.filePath.split("/").pop() : qsTr("Screenshot")

    width: Math.max(760, Math.min(document.imageWidth + 60, Screen.desktopAvailableWidth * 0.9))
    height: Math.max(520, Math.min(document.imageHeight + 140, Screen.desktopAvailableHeight * 0.9))
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
        const contentWidth = Math.max(flick.width, document.imageWidth * zoom)
        const contentHeight = Math.max(flick.height, document.imageHeight * zoom)
        const imageX = (contentWidth - document.imageWidth * zoom) / 2
        const imageY = (contentHeight - document.imageHeight * zoom) / 2
        flick.contentX = clamp(p.x * zoom + imageX - viewX, 0, contentWidth - flick.width)
        flick.contentY = clamp(p.y * zoom + imageY - viewY, 0, contentHeight - flick.height)
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
        if (document.imageWidth === 0 || document.imageHeight === 0)
            return
        const fit = Math.min((flick.width - 24) / document.imageWidth, (flick.height - 24) / document.imageHeight)
        zoomAt(Math.min(fit, 1), flick.width / 2, flick.height / 2)
    }

    function rememberTools() {
        Preferences.color = toolColor
        Preferences.size = size
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
        case Document.Select: return qsTr("Click a mark to select it, drag to move it, Delete removes it. Middle button or Space drags the view, Ctrl+wheel zooms.")
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
            discardDialog.open()
        }
    }

    Component.onCompleted: {
        if (document.imageWidth * zoom > flick.width || document.imageHeight * zoom > flick.height)
            zoomToFit()
    }

    header: AnnotationToolbar {
        id: toolbar
        tool: root.tool
        color: root.toolColor
        size: root.size
        document: root.document
        onToolPicked: tool => root.tool = tool
        onColorPicked: picked => root.toolColor = picked
        onSizePicked: size => root.size = size

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

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: Math.max(width, root.document.imageWidth * root.zoom)
        contentHeight: Math.max(height, root.document.imageHeight * root.zoom)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        interactive: false
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

        Image {
            id: image
            x: (flick.contentWidth - width) / 2
            y: (flick.contentHeight - height) / 2
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

    DrawingArea {
        id: marks
        anchors.fill: flick
        document: root.document
        zoom: root.zoom
        originX: (flick.contentX - image.x) / root.zoom
        originY: (flick.contentY - image.y) / root.zoom
        tool: root.tool
        toolColor: root.toolColor
        size: root.size
        showGrid: root.showGrid
        gridMinZoom: 8
        panEnabled: true
        onCursorChanged: root.cursor = cursor
        onPanned: (dx, dy) => {
            flick.contentX = root.clamp(flick.contentX - dx, 0, flick.contentWidth - flick.width)
            flick.contentY = root.clamp(flick.contentY - dy, 0, flick.contentHeight - flick.height)
        }
        onZoomRequested: (factor, x, y) => root.zoomAt(root.zoom * factor, x, y)
        onScrolled: (dx, dy) => {
            flick.contentX = root.clamp(flick.contentX - dx, 0, flick.contentWidth - flick.width)
            flick.contentY = root.clamp(flick.contentY - dy, 0, flick.contentHeight - flick.height)
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
                readonly property bool inside: root.cursor.x >= 0 && root.cursor.y >= 0
                                               && root.cursor.x < root.document.imageWidth
                                               && root.cursor.y < root.document.imageHeight
                Layout.preferredWidth: 130
                font.pixelSize: 12
                text: inside ? qsTr("x %1  y %2").arg(Math.floor(root.cursor.x)).arg(Math.floor(root.cursor.y)) : ""
            }
            RowLayout {
                spacing: 4
                readonly property color pixel: root.document.pixelAt(Math.floor(root.cursor.x), Math.floor(root.cursor.y))
                visible: root.cursor.x >= 0
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
                text: qsTr("%1 × %2 px").arg(root.document.imageWidth).arg(root.document.imageHeight)
            }
            Label {
                Layout.fillWidth: true
                font.pixelSize: 12
                elide: Text.ElideRight
                color: status.message !== "" ? Theme.accent : palette.placeholderText
                text: status.message !== "" ? status.message : root.toolHint()
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
        id: openDialog
        title: qsTr("Open image")
        nameFilters: [qsTr("Images and videos (*.png *.jpg *.jpeg *.bmp *.webp *.gif *.mp4 *.webm *.mkv *.avi *.mov)"), qsTr("All files (*)")]
        currentFolder: "file://" + Preferences.saveDirectory
        onAccepted: App.openEditor(selectedFile.toString().replace(/^file:\/\//, ""))
    }

    Dialog {
        id: discardDialog
        anchors.centerIn: parent
        modal: true
        title: qsTr("Discard changes?")
        standardButtons: Dialog.Discard | Dialog.Cancel
        Label { text: qsTr("The marks on this screenshot are not saved.") }
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
                        root.tool = keyHolder.modelData.tool
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
