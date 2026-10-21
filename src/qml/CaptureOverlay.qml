pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Guckste
import Guckste.Core
import Guckste.Ui

// The screen as it was a moment ago, over the whole desktop: drag out a
// region, draw on it, then copy, save or open it in the editor.
Window {
    id: root

    required property Document document
    required property rect screenGeometry

    x: screenGeometry.x
    y: screenGeometry.y
    width: screenGeometry.width
    height: screenGeometry.height
    // No frame and no window manager in between, so it covers every screen
    // and nothing can get on top of it.
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.BypassWindowManagerHint
    color: "black"
    title: "Guckste"

    // Image pixels per window pixel: 1, or more on a HiDPI screen.
    readonly property real imageScale: document.imageWidth > 0 && width > 0 ? document.imageWidth / width : 1
    readonly property real zoom: 1 / imageScale

    // The region, in image pixels.
    property bool hasSelection: false
    property rect selection: Qt.rect(0, 0, 0, 0)
    readonly property rect selectionView: Qt.rect(selection.x * zoom, selection.y * zoom,
                                                  selection.width * zoom, selection.height * zoom)
    property int tool: Document.Select
    property color toolColor: Preferences.color
    property real size: Preferences.size
    property point cursor: Qt.point(-1, -1)

    function toImage(x: real, y: real): point {
        return Qt.point(x * imageScale, y * imageScale)
    }

    function clamp(value: real, low: real, high: real): real {
        return Math.max(low, Math.min(high, value))
    }

    function setSelection(x: real, y: real, w: real, h: real) {
        const x0 = clamp(Math.round(x), 0, document.imageWidth)
        const y0 = clamp(Math.round(y), 0, document.imageHeight)
        const x1 = clamp(Math.round(x + w), 0, document.imageWidth)
        const y1 = clamp(Math.round(y + h), 0, document.imageHeight)
        selection = Qt.rect(x0, y0, x1 - x0, y1 - y0)
        hasSelection = selection.width > 0 && selection.height > 0
    }

    function selectBetween(a: point, b: point) {
        setSelection(Math.min(a.x, b.x), Math.min(a.y, b.y), Math.abs(b.x - a.x), Math.abs(b.y - a.y))
    }

    function moveSelection(start: rect, dx: real, dy: real) {
        const nx = clamp(Math.round(start.x + dx), 0, document.imageWidth - start.width)
        const ny = clamp(Math.round(start.y + dy), 0, document.imageHeight - start.height)
        selection = Qt.rect(nx, ny, start.width, start.height)
    }

    // Handles: 0 top-left, then clockwise.
    function resizeSelection(start: rect, handle: int, dx: real, dy: real) {
        let left = start.x, top = start.y, right = start.x + start.width, bottom = start.y + start.height
        if (handle === 0 || handle === 6 || handle === 7) left += dx
        if (handle === 2 || handle === 3 || handle === 4) right += dx
        if (handle === 0 || handle === 1 || handle === 2) top += dy
        if (handle === 4 || handle === 5 || handle === 6) bottom += dy
        selectBetween(Qt.point(left, top), Qt.point(right, bottom))
    }

    function nudge(dx: int, dy: int) {
        if (hasSelection)
            moveSelection(selection, dx, dy)
    }

    // The whole screen under a window point.
    function selectScreenAt(x: real, y: real) {
        const gx = x + screenGeometry.x
        const gy = y + screenGeometry.y
        const screens = Qt.application.screens
        for (let i = 0; i < screens.length; ++i) {
            const s = screens[i]
            if (gx >= s.virtualX && gx < s.virtualX + s.width && gy >= s.virtualY && gy < s.virtualY + s.height) {
                setSelection((s.virtualX - screenGeometry.x) * imageScale, (s.virtualY - screenGeometry.y) * imageScale,
                             s.width * imageScale, s.height * imageScale)
                return
            }
        }
        setSelection(0, 0, document.imageWidth, document.imageHeight)
    }

    function clearSelection() {
        document.cancelAnnotation()
        document.clearAnnotations()
        hasSelection = false
        selection = Qt.rect(0, 0, 0, 0)
        tool = Document.Select
    }

    function rememberTools() {
        Preferences.color = toolColor
        Preferences.size = size
    }

    function copy() {
        if (!hasSelection)
            return
        textEntry.commit()
        document.copyToClipboard(1, selection)
        rememberTools()
        root.close()
    }

    function save() {
        if (!hasSelection)
            return
        textEntry.commit()
        const path = Preferences.nextSavePath()
        if (document.save(path, 1, selection)) {
            if (Preferences.copyPathAfterSave)
                Clipboard.setText(path)
            App.notify(qsTr("Screenshot saved"), path)
        } else {
            App.notify(qsTr("Could not save"), path)
        }
        rememberTools()
        root.close()
    }

    function edit() {
        if (!hasSelection)
            return
        textEntry.commit()
        rememberTools()
        App.editInWindow(document, selection)
    }

    function cancel() {
        rememberTools()
        root.close()
    }

    function constrainFor(modifiers: int): bool {
        if (tool === Document.Measure)
            return !(modifiers & Qt.AltModifier)
        return (modifiers & Qt.ShiftModifier) !== 0
    }

    Image {
        anchors.fill: parent
        source: root.document.imageSource
        cache: false
        smooth: root.imageScale !== 1
    }

    // Everything outside the region is dimmed.
    Rectangle {
        anchors.fill: parent
        color: Theme.dim
        visible: !root.hasSelection
    }
    Rectangle {
        visible: root.hasSelection
        x: 0; y: 0; width: root.width; height: root.selectionView.y
        color: Theme.dim
    }
    Rectangle {
        visible: root.hasSelection
        x: 0; y: root.selectionView.y + root.selectionView.height
        width: root.width; height: root.height - y
        color: Theme.dim
    }
    Rectangle {
        visible: root.hasSelection
        x: 0; y: root.selectionView.y; width: root.selectionView.x; height: root.selectionView.height
        color: Theme.dim
    }
    Rectangle {
        visible: root.hasSelection
        x: root.selectionView.x + root.selectionView.width; y: root.selectionView.y
        width: root.width - x; height: root.selectionView.height
        color: Theme.dim
    }

    AnnotationLayer {
        id: layer
        visible: root.hasSelection
        x: root.selectionView.x
        y: root.selectionView.y
        width: root.selectionView.width
        height: root.selectionView.height
        document: root.document
        zoom: root.zoom
        originX: root.selection.x
        originY: root.selection.y
    }

    Rectangle {
        visible: root.hasSelection
        x: root.selectionView.x - 1
        y: root.selectionView.y - 1
        width: root.selectionView.width + 2
        height: root.selectionView.height + 2
        color: "transparent"
        border.color: Theme.selectionBorder
        border.width: 1
    }

    // Crosshair while there is no region yet.
    Rectangle {
        visible: !root.hasSelection && root.cursor.x >= 0 && mouse.mode === ""
        x: Math.round(root.cursor.x * root.zoom)
        y: 0
        width: 1
        height: root.height
        color: "#b0ffffff"
    }
    Rectangle {
        visible: !root.hasSelection && root.cursor.y >= 0 && mouse.mode === ""
        x: 0
        y: Math.round(root.cursor.y * root.zoom)
        width: root.width
        height: 1
        color: "#b0ffffff"
    }

    // Handles to resize the region.
    Repeater {
        model: 8
        Rectangle {
            id: handle
            required property int index
            readonly property rect s: root.selectionView
            readonly property real cx: index === 0 || index === 6 || index === 7 ? s.x
                                     : index === 1 || index === 5 ? s.x + s.width / 2 : s.x + s.width
            readonly property real cy: index === 0 || index === 1 || index === 2 ? s.y
                                     : index === 3 || index === 7 ? s.y + s.height / 2 : s.y + s.height
            visible: root.hasSelection && root.tool === Document.Select && mouse.mode !== "select"
            x: cx - width / 2
            y: cy - height / 2
            width: 9
            height: 9
            color: "white"
            border.color: "#80000000"
            border.width: 1
        }
    }

    // The region's size, above it or inside its top edge.
    Rectangle {
        visible: root.hasSelection
        x: root.selectionView.x
        y: root.selectionView.y >= height + 6 ? root.selectionView.y - height - 4 : root.selectionView.y + 4
        width: sizeLabel.implicitWidth + 12
        height: sizeLabel.implicitHeight + 6
        radius: 4
        color: Theme.panel
        Label {
            id: sizeLabel
            anchors.centerIn: parent
            color: Theme.panelText
            font.pixelSize: 12
            text: `${root.selection.width} × ${root.selection.height}`
        }
    }

    // Where the pointer is, while picking the region.
    Rectangle {
        visible: !root.hasSelection && root.cursor.x >= 0
        x: Math.min(root.cursor.x * root.zoom + 16, root.width - width - 4)
        y: Math.min(root.cursor.y * root.zoom + 16, root.height - height - 4)
        width: cursorLabel.implicitWidth + 12
        height: cursorLabel.implicitHeight + 6
        radius: 4
        color: Theme.panel
        Label {
            id: cursorLabel
            anchors.centerIn: parent
            color: Theme.panelText
            font.pixelSize: 12
            text: mouse.mode === "select"
                  ? `${root.selection.width} × ${root.selection.height}`
                  : `${Math.floor(root.cursor.x)}, ${Math.floor(root.cursor.y)}`
        }
    }

    MouseArea {
        id: mouse

        // "select": dragging out a region; "move", "resize": changing it;
        // "draw": an annotation; "drag": moving an annotation.
        property string mode: ""
        property point anchor
        property rect startSelection
        property int handle: -1
        property point last
        property int hoverHandle: -1

        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: {
            if (mode === "move" || (mode === "" && root.hasSelection && root.tool === Document.Select
                                    && hoverHandle === -1 && root.cursor.x >= 0 && insideSelection(root.cursor)))
                return Qt.SizeAllCursor
            const h = mode === "resize" ? handle : hoverHandle
            if (h === 0 || h === 4) return Qt.SizeFDiagCursor
            if (h === 2 || h === 6) return Qt.SizeBDiagCursor
            if (h === 1 || h === 5) return Qt.SizeVerCursor
            if (h === 3 || h === 7) return Qt.SizeHorCursor
            if (root.tool === Document.Text && root.hasSelection) return Qt.IBeamCursor
            return Qt.CrossCursor
        }

        function insideSelection(p: point): bool {
            const s = root.selection
            return p.x >= s.x && p.x < s.x + s.width && p.y >= s.y && p.y < s.y + s.height
        }

        function handleAt(x: real, y: real): int {
            if (!root.hasSelection || root.tool !== Document.Select)
                return -1
            const s = root.selectionView
            const xs = [s.x, s.x + s.width / 2, s.x + s.width, s.x + s.width, s.x + s.width, s.x + s.width / 2, s.x, s.x]
            const ys = [s.y, s.y, s.y, s.y + s.height / 2, s.y + s.height, s.y + s.height, s.y + s.height, s.y + s.height / 2]
            for (let i = 0; i < 8; ++i) {
                if (Math.abs(xs[i] - x) <= 7 && Math.abs(ys[i] - y) <= 7)
                    return i
            }
            return -1
        }

        onPressed: event => {
            const p = root.toImage(event.x, event.y)
            if (event.button === Qt.RightButton) {
                if (root.document.drawing)
                    root.document.cancelAnnotation()
                else if (root.document.selectedIndex >= 0)
                    root.document.selectedIndex = -1
                else if (root.hasSelection)
                    root.clearSelection()
                else
                    root.cancel()
                return
            }
            textEntry.commit()
            if (!root.hasSelection) {
                mode = "select"
                anchor = p
                return
            }
            if (root.tool === Document.Select) {
                const h = handleAt(event.x, event.y)
                if (h >= 0) {
                    mode = "resize"
                    handle = h
                    startSelection = root.selection
                    anchor = p
                    return
                }
                const hit = root.document.hitTest(p.x, p.y, root.zoom)
                if (hit >= 0 && insideSelection(p)) {
                    root.document.selectedIndex = hit
                    root.document.beginMove(hit)
                    mode = "drag"
                    last = p
                    return
                }
                root.document.selectedIndex = -1
                if (insideSelection(p)) {
                    mode = "move"
                    startSelection = root.selection
                    anchor = p
                } else {
                    mode = "select"
                    anchor = p
                }
                return
            }
            if (root.tool === Document.Text) {
                textEntry.openAt(event.x, event.y, p.x, p.y, "")
                return
            }
            root.document.beginAnnotation(root.tool, p.x, p.y, root.toolColor, root.size)
            mode = "draw"
        }

        onPositionChanged: event => {
            const p = root.toImage(event.x, event.y)
            root.cursor = p
            switch (mode) {
            case "select":
                root.selectBetween(anchor, p)
                break
            case "move":
                root.moveSelection(startSelection, p.x - anchor.x, p.y - anchor.y)
                break
            case "resize":
                root.resizeSelection(startSelection, handle, p.x - anchor.x, p.y - anchor.y)
                break
            case "draw":
                root.document.updateAnnotation(p.x, p.y, root.constrainFor(event.modifiers))
                break
            case "drag":
                root.document.moveBy(p.x - last.x, p.y - last.y)
                last = p
                break
            default:
                hoverHandle = handleAt(event.x, event.y)
                break
            }
        }

        onReleased: event => {
            const p = root.toImage(event.x, event.y)
            switch (mode) {
            case "select":
                if (Math.abs(p.x - anchor.x) < 3 && Math.abs(p.y - anchor.y) < 3)
                    root.selectScreenAt(event.x, event.y)
                else
                    root.selectBetween(anchor, p)
                break
            case "draw":
                root.document.commitAnnotation()
                break
            case "drag":
                root.document.endMove()
                break
            }
            mode = ""
            hoverHandle = handleAt(event.x, event.y)
        }

        onExited: root.cursor = Qt.point(-1, -1)
    }

    TextEntry {
        id: textEntry
        pixelSize: 10 + root.size * 2
        color: root.toolColor
        onAccepted: text => root.document.addText(imagePoint.x, imagePoint.y, text, root.toolColor, root.size)
    }

    AnnotationToolbar {
        id: toolbar
        dark: true
        visible: root.hasSelection && mouse.mode !== "select" && mouse.mode !== "move" && mouse.mode !== "resize"
        tool: root.tool
        color: root.toolColor
        size: root.size
        document: root.document
        onToolPicked: tool => root.tool = tool
        onColorPicked: picked => root.toolColor = picked
        onSizePicked: size => root.size = size
        x: root.clamp(root.selectionView.x + root.selectionView.width / 2 - width / 2, 4, root.width - width - 4)
        y: {
            const s = root.selectionView
            if (s.y + s.height + 8 + height <= root.height)
                return s.y + s.height + 8
            if (s.y - 8 - height >= 0)
                return s.y - 8 - height
            return s.y + s.height - height - 8
        }

        IconButton {
            iconName: "copy"
            tip: qsTr("Copy to clipboard (Enter)")
            onClicked: root.copy()
        }
        IconButton {
            iconName: "save"
            tip: qsTr("Save (Ctrl+S)")
            onClicked: root.save()
        }
        IconButton {
            iconName: "pencil-ruler"
            tip: qsTr("Open in the editor to zoom and measure (E)")
            onClicked: root.edit()
        }
        IconButton {
            iconName: "x"
            tip: qsTr("Cancel (Esc)")
            onClicked: root.cancel()
        }
    }

    Shortcut { sequence: "Escape"; onActivated: textEntry.visible ? textEntry.cancel() : root.cancel() }
    Shortcut { sequences: ["Return", "Enter", "Ctrl+C"]; enabled: !textEntry.visible; onActivated: root.copy() }
    Shortcut { sequence: "Ctrl+S"; onActivated: root.save() }
    Shortcut { sequence: "Ctrl+Z"; onActivated: root.document.undo() }
    Shortcut { sequences: ["Ctrl+Shift+Z", "Ctrl+Y"]; onActivated: root.document.redo() }
    Shortcut {
        sequences: ["Delete", "Backspace"]
        enabled: !textEntry.visible
        onActivated: root.document.removeAnnotation(root.document.selectedIndex)
    }
    Shortcut { sequence: "Ctrl+A"; enabled: !textEntry.visible; onActivated: root.setSelection(0, 0, root.document.imageWidth, root.document.imageHeight) }
    Shortcut { sequence: "Left"; enabled: !textEntry.visible; onActivated: root.nudge(-1, 0) }
    Shortcut { sequence: "Right"; enabled: !textEntry.visible; onActivated: root.nudge(1, 0) }
    Shortcut { sequence: "Up"; enabled: !textEntry.visible; onActivated: root.nudge(0, -1) }
    Shortcut { sequence: "Down"; enabled: !textEntry.visible; onActivated: root.nudge(0, 1) }
    Shortcut { sequence: "Shift+Left"; enabled: !textEntry.visible; onActivated: root.nudge(-10, 0) }
    Shortcut { sequence: "Shift+Right"; enabled: !textEntry.visible; onActivated: root.nudge(10, 0) }
    Shortcut { sequence: "Shift+Up"; enabled: !textEntry.visible; onActivated: root.nudge(0, -10) }
    Shortcut { sequence: "Shift+Down"; enabled: !textEntry.visible; onActivated: root.nudge(0, 10) }

    Repeater {
        model: [
            { key: "E", action: () => root.edit() },
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
                enabled: !textEntry.visible && root.hasSelection
                onActivated: {
                    if (keyHolder.modelData.action)
                        keyHolder.modelData.action()
                    else
                        root.tool = keyHolder.modelData.tool
                }
            }
        }
    }
}
