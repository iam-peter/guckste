pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Shapes
import Guckste
import Guckste.Core
import Guckste.Ui

// The screen as it was a moment ago, over the whole desktop: drag out a
// region, draw on it, then copy, save or open it in the editor.
Window {
    id: root

    required property Document document
    required property rect screenGeometry
    // Opened to record: Enter starts the recording instead of copying.
    property bool recordMode: false
    // The windows on screen, front to back, in capture coordinates: the one
    // under the pointer is highlighted and a click takes it.
    property var windowRects: []
    // The screen the pointer was on when the overlay opened, in capture
    // coordinates: the mode bar goes there.
    property rect pointerScreen: Qt.rect(0, 0, 0, 0)
    // The screen the pointer is on now, in window coordinates: the mode bar
    // follows it from screen to screen.
    property rect barScreen: pointerScreen
    onCursorChanged: {
        if (cursor.x < 0)
            return
        const gx = cursor.x / imageScale + screenGeometry.x
        const gy = cursor.y / imageScale + screenGeometry.y
        const screens = Qt.application.screens
        for (let i = 0; i < screens.length; ++i) {
            const s = screens[i]
            if (gx < s.virtualX || gx >= s.virtualX + s.width || gy < s.virtualY || gy >= s.virtualY + s.height)
                continue
            const x = s.virtualX - screenGeometry.x, y = s.virtualY - screenGeometry.y
            if (x !== barScreen.x || y !== barScreen.y || s.width !== barScreen.width || s.height !== barScreen.height)
                barScreen = Qt.rect(x, y, s.width, s.height)
            return
        }
    }
    // How a click picks: "screen", "window", "region", "all" or "previous"
    // (a drag always picks a region, whatever the mode). Kept between
    // screenshots; "all" and "previous" select right away.
    property string pickMode: Preferences.pickMode
    // Only a mode picked here is kept, not the fallback while opening.
    property bool started: false
    onPickModeChanged: if (started) Preferences.pickMode = pickMode
    // A region to start with, in screen coordinates: after a timer.
    property rect presetRegion: Qt.rect(0, 0, 0, 0)
    // Opened from an editor to add a screenshot to its document: Enter
    // adds the region there.
    property Document addTarget: null

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
    // The region of last time, in image pixels, offered until one is picked.
    readonly property rect lastRegion: {
        const r = Preferences.lastRegion
        if (r.width <= 0 || r.height <= 0)
            return Qt.rect(0, 0, 0, 0)
        const x = (r.x - screenGeometry.x) * imageScale
        const y = (r.y - screenGeometry.y) * imageScale
        const w = r.width * imageScale
        const h = r.height * imageScale
        if (x < 0 || y < 0 || x + w > document.imageWidth || y + h > document.imageHeight)
            return Qt.rect(0, 0, 0, 0)
        return Qt.rect(x, y, w, h)
    }
    readonly property bool lastRegionOffered: !hasSelection && lastRegion.width > 0 && mouse.mode === ""

    readonly property bool picking: tool === Document.Select && mouse.mode === "" && cursor.x >= 0
                                    && !(hasSelection && mouse.insideSelection(cursor))

    // The window under the pointer, in image pixels, while picking.
    readonly property rect hoveredWindow: {
        if (pickMode !== "window" || !picking)
            return Qt.rect(0, 0, 0, 0)
        const vx = cursor.x / imageScale, vy = cursor.y / imageScale
        for (let i = 0; i < windowRects.length; ++i) {
            const r = windowRects[i]
            if (vx >= r.x && vx < r.x + r.width && vy >= r.y && vy < r.y + r.height)
                return Qt.rect(r.x * imageScale, r.y * imageScale, r.width * imageScale, r.height * imageScale)
        }
        return Qt.rect(0, 0, 0, 0)
    }

    // What a click takes in the current mode, in image pixels.
    readonly property rect hoveredTarget: pickMode === "screen" ? hoveredScreen
                                        : pickMode === "window" ? hoveredWindow
                                        : pickMode === "all" && picking ? Qt.rect(0, 0, document.imageWidth, document.imageHeight)
                                        : pickMode === "previous" && picking ? lastRegion
                                        : Qt.rect(0, 0, 0, 0)

    // The screen under the pointer, in image pixels, while picking.
    readonly property rect hoveredScreen: {
        if (pickMode !== "screen" || !picking)
            return Qt.rect(0, 0, 0, 0)
        const gx = cursor.x / imageScale + screenGeometry.x
        const gy = cursor.y / imageScale + screenGeometry.y
        const screens = Qt.application.screens
        for (let i = 0; i < screens.length; ++i) {
            const s = screens[i]
            if (gx >= s.virtualX && gx < s.virtualX + s.width && gy >= s.virtualY && gy < s.virtualY + s.height)
                return Qt.rect((s.virtualX - screenGeometry.x) * imageScale, (s.virtualY - screenGeometry.y) * imageScale,
                               s.width * imageScale, s.height * imageScale)
        }
        return Qt.rect(0, 0, 0, 0)
    }

    property int tool: Document.Select
    property color toolColor: Preferences.color
    property real size: Preferences.size
    property real textSize: Preferences.textSize
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
        Preferences.textSize = textSize
        if (hasSelection)
            Preferences.lastRegion = Qt.rect(Math.round(screenGeometry.x + selection.x / imageScale),
                                             Math.round(screenGeometry.y + selection.y / imageScale),
                                             Math.round(selection.width / imageScale),
                                             Math.round(selection.height / imageScale))
    }

    function takeLastRegion() {
        if (lastRegion.width > 0)
            setSelection(lastRegion.x, lastRegion.y, lastRegion.width, lastRegion.height)
    }

    function takeAllScreens() {
        setSelection(0, 0, document.imageWidth, document.imageHeight)
    }

    // Switching the mode starts the pick over: a region already taken
    // would cover what the new mode outlines, all of it for "all".
    function setPickMode(mode: string) {
        if (hasSelection)
            clearSelection()
        pickMode = mode
        if (mode === "all")
            takeAllScreens()
        else if (mode === "previous")
            takeLastRegion()
    }

    // The region captured again after the timer, in screen coordinates.
    function selectionOnScreen(): rect {
        return Qt.rect(Math.round(screenGeometry.x + selection.x / imageScale),
                       Math.round(screenGeometry.y + selection.y / imageScale),
                       Math.round(selection.width / imageScale), Math.round(selection.height / imageScale))
    }

    function captureLater(seconds: int) {
        if (!hasSelection)
            return
        textEntry.commit()
        rememberTools()
        App.captureLater(seconds, selectionOnScreen(), recordMode)
    }

    Component.onCompleted: {
        if (presetRegion.width > 0)
            setSelection((presetRegion.x - screenGeometry.x) * imageScale, (presetRegion.y - screenGeometry.y) * imageScale,
                         presetRegion.width * imageScale, presetRegion.height * imageScale)
        else if (pickMode === "all")
            takeAllScreens()
        else if (pickMode === "previous" && lastRegion.width > 0)
            takeLastRegion()
        else if (pickMode === "previous")
            pickMode = "region"
        started = true
    }

    // A click with no drag takes what the mode highlighted.
    function pickAt(x: real, y: real) {
        const r = mouse.pressedTarget
        if (r.width > 0)
            setSelection(r.x, r.y, r.width, r.height)
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
        if (addTarget) {
            addToEditor()
            return
        }
        if (!hasSelection)
            return
        textEntry.commit()
        rememberTools()
        App.editInWindow(document, selection)
    }

    function record() {
        if (!hasSelection || addTarget)
            return
        textEntry.commit()
        rememberTools()
        // The region in screen coordinates, as the recorder wants it.
        App.startRecording(Qt.rect(screenGeometry.x + selection.x / imageScale,
                                   screenGeometry.y + selection.y / imageScale,
                                   selection.width / imageScale, selection.height / imageScale))
    }

    function addToEditor() {
        if (!hasSelection || !addTarget)
            return
        textEntry.commit()
        rememberTools()
        App.addToDocument(addTarget, document, selection)
    }

    function primaryAction() {
        if (addTarget)
            addToEditor()
        else if (recordMode)
            record()
        else
            copy()
    }

    function cancel() {
        Preferences.color = toolColor
        Preferences.size = size
        Preferences.textSize = textSize
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

    // The region's frame, kept inside the window and thicker along its
    // edges: a region reaching them, all screens or a whole screen, would
    // otherwise have its frame off the screen.
    Rectangle {
        readonly property rect s: root.selectionView
        readonly property bool atEdge: s.x <= 0 || s.y <= 0 || s.x + s.width >= root.width || s.y + s.height >= root.height
        visible: root.hasSelection
        x: Math.max(0, s.x - 1)
        y: Math.max(0, s.y - 1)
        width: Math.min(root.width, s.x + s.width + 1) - x
        height: Math.min(root.height, s.y + s.height + 1) - y
        color: "transparent"
        border.color: Theme.selectionBorder
        border.width: atEdge ? 3 : 1
    }

    // The window or screen under the pointer: click to take it.
    Rectangle {
        readonly property rect target: root.hoveredTarget
        visible: target.width > 0
        x: target.x * root.zoom
        y: target.y * root.zoom
        width: target.width * root.zoom
        height: target.height * root.zoom
        color: "#18ffffff"
        border.color: "#ffffff"
        border.width: 2
        Rectangle {
            x: 4
            y: 4
            width: windowLabel.implicitWidth + 12
            height: windowLabel.implicitHeight + 6
            radius: 4
            color: Theme.panel
            Label {
                id: windowLabel
                anchors.centerIn: parent
                color: Theme.panelText
                font.pixelSize: 12
                text: `${Math.round(parent.parent.target.width)} × ${Math.round(parent.parent.target.height)}`
            }
        }
    }

    // Last time's region, dashed: the mode bar's Previous takes it again.
    Item {
        id: ghost
        visible: root.lastRegionOffered
        x: root.lastRegion.x * root.zoom
        y: root.lastRegion.y * root.zoom
        width: root.lastRegion.width * root.zoom
        height: root.lastRegion.height * root.zoom
        Shape {
            anchors.fill: parent
            ShapePath {
                strokeColor: Theme.accent
                strokeWidth: 2
                strokeStyle: ShapePath.DashLine
                dashPattern: [4, 3]
                fillColor: "transparent"
                startX: 1; startY: 1
                PathLine { x: ghost.width - 1; y: 1 }
                PathLine { x: ghost.width - 1; y: ghost.height - 1 }
                PathLine { x: 1; y: ghost.height - 1 }
                PathLine { x: 1; y: 1 }
            }
        }
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
            x: root.clamp(cx - width / 2, 0, root.width - width)
            y: root.clamp(cy - height / 2, 0, root.height - height)
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
            text: root.recordMode ? qsTr("Record %1 × %2").arg(root.selection.width).arg(root.selection.height)
                                  : `${root.selection.width} × ${root.selection.height}`
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
        property rect pressedTarget
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
                // Noted before "select" mode hides the highlight.
                pressedTarget = root.hoveredTarget
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
                    pressedTarget = root.hoveredTarget
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
                    root.pickAt(event.x, event.y)
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

    // Screen, window, region, all screens or last time's region, on the
    // screen the pointer is on.
    Rectangle {
        id: modeBar
        readonly property rect home: root.barScreen.width > 0 ? root.barScreen : Qt.rect(0, 0, root.width, root.height)
        x: home.x + home.width / 2 - width / 2
        y: home.y + 12
        width: modeRow.implicitWidth + 12
        height: modeRow.implicitHeight + 10
        radius: 8
        color: Theme.panel
        border.color: "#55ffffff"
        border.width: 1
        visible: mouse.mode === ""
        palette.buttonText: Theme.panelText
        palette.button: "#3a3a44"
        palette.highlight: Theme.accent

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
        }

        Row {
            id: modeRow
            anchors.centerIn: parent
            spacing: 2
            Repeater {
                model: [
                    { mode: "screen", icon: "monitor", text: qsTr("Screen"), tip: qsTr("Click a screen (1)") },
                    { mode: "window", icon: "app-window", text: qsTr("Window"), tip: qsTr("Click a window (2)") },
                    { mode: "region", icon: "square-dashed", text: qsTr("Region"), tip: qsTr("Drag out a region (3)") },
                    { mode: "all", icon: "layout-grid", text: qsTr("All screens"), tip: qsTr("Take all screens at once (4)") },
                    { mode: "previous", icon: "history", text: qsTr("Previous"), tip: qsTr("Take the region of last time (5)") },
                ]
                IconButton {
                    required property var modelData
                    objectName: "mode-" + modelData.mode
                    iconName: modelData.icon
                    text: modelData.text
                    tip: modelData.tip
                    // Shows the mode rather than toggling: a click on the
                    // one in use leaves it in use.
                    checked: root.pickMode === modelData.mode
                    enabled: modelData.mode !== "previous" || root.lastRegion.width > 0
                    onClicked: root.setPickMode(modelData.mode)
                }
            }
        }
    }

    TextEntry {
        id: textEntry
        pixelSize: root.textSize * root.zoom
        color: root.toolColor
        onAccepted: text => root.document.addText(imagePoint.x, imagePoint.y, text, root.toolColor, root.textSize)
    }

    AnnotationToolbar {
        id: toolbar
        objectName: "toolbar"
        dark: true
        visible: root.hasSelection && mouse.mode !== "select" && mouse.mode !== "move" && mouse.mode !== "resize"
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
            tip: root.addTarget !== null ? qsTr("Copy to clipboard") : qsTr("Copy to clipboard (Enter)")
            onClicked: root.copy()
        }
        IconButton {
            iconName: "save"
            tip: qsTr("Save (Ctrl+S)")
            onClicked: root.save()
        }
        IconButton {
            visible: root.addTarget !== null
            iconName: "image-plus"
            tip: qsTr("Add to the editor (Enter)")
            onClicked: root.addToEditor()
        }
        IconButton {
            visible: root.addTarget === null
            iconName: "pencil-ruler"
            tip: qsTr("Open in the editor to zoom and measure (E)")
            onClicked: root.edit()
        }
        IconButton {
            visible: root.addTarget === null
            iconName: "video"
            tip: root.recordMode ? qsTr("Record this region (Enter)") : qsTr("Record this region (Shift+Enter)")
            checked: root.recordMode
            onClicked: root.record()
        }
        IconButton {
            id: timerButton
            visible: root.addTarget === null
            iconName: "timer"
            tip: qsTr("Capture this region again after a delay, for menus and tooltips")
            onClicked: timerMenu.open()
            Menu {
                id: timerMenu
                y: timerButton.height
                Repeater {
                    model: [3, 5, 10]
                    MenuItem {
                        required property int modelData
                        text: qsTr("In %1 seconds").arg(modelData)
                        onTriggered: root.captureLater(modelData)
                    }
                }
            }
        }
        IconButton {
            iconName: "x"
            tip: qsTr("Cancel (Esc)")
            onClicked: root.cancel()
        }
    }

    Shortcut {
        sequence: "Escape"
        onActivated: {
            if (timerMenu.opened)
                timerMenu.close()
            else if (textEntry.visible)
                textEntry.cancel()
            else
                root.cancel()
        }
    }
    Shortcut { sequences: ["Return", "Enter"]; enabled: !textEntry.visible; onActivated: root.primaryAction() }
    Shortcut { sequence: "Ctrl+C"; enabled: !textEntry.visible; onActivated: root.copy() }
    Shortcut { sequences: ["Shift+Return", "Shift+Enter"]; enabled: !textEntry.visible; onActivated: root.record() }
    Shortcut { sequence: "Ctrl+S"; onActivated: root.save() }
    Shortcut { sequence: "Ctrl+Z"; onActivated: root.document.undo() }
    Shortcut { sequences: ["Ctrl+Shift+Z", "Ctrl+Y"]; onActivated: root.document.redo() }
    Shortcut {
        sequences: ["Delete", "Backspace"]
        enabled: !textEntry.visible
        onActivated: root.document.removeAnnotation(root.document.selectedIndex)
    }
    Shortcut { sequence: "Ctrl+A"; enabled: !textEntry.visible; onActivated: root.setSelection(0, 0, root.document.imageWidth, root.document.imageHeight) }
    Shortcut { sequence: "1"; enabled: !textEntry.visible; onActivated: root.setPickMode("screen") }
    Shortcut { sequence: "2"; enabled: !textEntry.visible; onActivated: root.setPickMode("window") }
    Shortcut { sequence: "3"; enabled: !textEntry.visible; onActivated: root.setPickMode("region") }
    Shortcut { sequence: "4"; enabled: !textEntry.visible; onActivated: root.setPickMode("all") }
    Shortcut { sequence: "5"; enabled: !textEntry.visible; onActivated: if (root.lastRegion.width > 0) root.setPickMode("previous") }
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
