pragma ComponentBehavior: Bound

import QtQuick
import Guckste
import Guckste.Core
import Guckste.Ui

// Where marks are drawn: the layer that paints them over the image or
// video below, the pointer handling for every tool, and the text entry.
// The window around it decides the zoom and what the item's top-left
// corner shows (`originX/Y`, image pixels), and gets the pan and zoom
// gestures handed to it.
Item {
    id: area

    required property Document document
    property real zoom: 1
    property real originX: 0
    property real originY: 0
    property int tool: Document.Select
    property color toolColor: "#ff3b30"
    property real size: 3
    // A new text's font size in pixels.
    property real textSize: 20
    property bool showGrid: false
    property real gridMinZoom: 8
    // Middle button or Space drag the view; Ctrl+wheel zooms, wheel scrolls.
    property bool panEnabled: false
    property bool spaceHeld: false
    // The image pixel under the pointer, (-1, -1) outside.
    property point cursor: Qt.point(-1, -1)
    readonly property bool editingText: textEntry.visible
    readonly property bool busy: mouse.mode !== ""
    // The text mark being edited in place, -1 while typing a new one.
    property int editingIndex: -1
    // The document's guides are shown, and with the select tool dragged;
    // one dragged off the area goes.
    property bool showGuides: false
    // Points drawn, handles dragged and marks moved line up with guides
    // within a few screen pixels.
    property bool snapToGuides: false
    readonly property var guides: document && showGuides ? document.guides : []
    // The guide being dragged, -1 for none.
    readonly property int draggedGuide: mouse.mode === "guide" ? mouse.guide : -1
    // Where the selected mark can be reshaped, in image pixels.
    readonly property var handlePoints: document && tool === Document.Select && !textEntry.visible
                                        && document.revision >= 0 ? document.handlePoints(document.selectedIndex) : []

    signal panned(real dx, real dy)
    signal zoomRequested(real factor, real viewX, real viewY)
    signal scrolled(real dx, real dy)

    function toImage(x: real, y: real): point {
        return marks.toImage(x, y)
    }

    function toView(x: real, y: real): point {
        return marks.toView(x, y)
    }

    function commitText() {
        textEntry.commit()
    }

    function cancelText() {
        textEntry.cancel()
    }

    // How far a point is pulled to a guide, in image pixels.
    readonly property real snapDistance: 8 / zoom

    // The nearest guide position within reach of `value`, or `value`.
    function snapValue(value: real, vertical: bool): real {
        let best = value
        let distance = snapDistance
        for (const guide of guides) {
            if (guide.vertical !== vertical)
                continue
            const d = Math.abs(guide.position - value)
            if (d <= distance) {
                distance = d
                best = guide.position
            }
        }
        return best
    }

    function snap(p: point): point {
        if (!snapToGuides || guides.length === 0)
            return p
        return Qt.point(snapValue(p.x, true), snapValue(p.y, false))
    }

    // How far to shift a rectangle so its nearest edge meets a guide.
    function snapShift(edges: list<real>, vertical: bool): real {
        let shift = 0
        let distance = snapDistance
        for (const edge of edges) {
            const snapped = snapValue(edge, vertical)
            const d = Math.abs(snapped - edge)
            if (snapped !== edge && d <= distance) {
                distance = d
                shift = snapped - edge
            }
        }
        return shift
    }

    // The guide under a view point, -1 for none.
    function guideAt(x: real, y: real): int {
        for (let i = 0; i < guides.length; ++i) {
            const g = guides[i]
            const v = g.vertical ? (g.position - originX) * zoom : (g.position - originY) * zoom
            if (Math.abs((g.vertical ? x : y) - v) <= 3)
                return i
        }
        return -1
    }

    // Opens a text mark for editing where it is.
    function editText(index: int) {
        if (document.annotationTool(index) !== Document.Text)
            return
        textEntry.commit()
        const at = document.annotationPosition(index)
        const v = marks.toView(at.x, at.y)
        document.selectedIndex = index
        editingIndex = index
        textEntry.openAt(v.x, v.y, at.x, at.y, document.annotationText(index))
    }

    // The handle under a view point, -1 for none.
    function handleAt(x: real, y: real): int {
        for (let i = 0; i < handlePoints.length; ++i) {
            const v = marks.toView(handlePoints[i].x, handlePoints[i].y)
            if (Math.abs(v.x - x) <= 7 && Math.abs(v.y - y) <= 7)
                return i
        }
        return -1
    }

    function handleCursor(handle: int): int {
        if (handlePoints.length === 2)
            return Qt.SizeAllCursor
        switch (handle) {
        case 0: case 4: return Qt.SizeFDiagCursor
        case 2: case 6: return Qt.SizeBDiagCursor
        case 1: case 5: return Qt.SizeVerCursor
        default: return Qt.SizeHorCursor
        }
    }

    function constrainFor(modifiers: int): bool {
        if (tool === Document.Measure)
            return !(modifiers & Qt.AltModifier)
        return (modifiers & Qt.ShiftModifier) !== 0
    }

    AnnotationLayer {
        id: marks
        anchors.fill: parent
        document: area.document
        zoom: area.zoom
        originX: area.originX
        originY: area.originY
        showGrid: area.showGrid
        gridMinZoom: area.gridMinZoom
        hiddenIndex: area.editingIndex
    }

    MouseArea {
        id: mouse

        // "draw": an annotation; "drag": moving one; "handle": reshaping
        // one; "pan": the view.
        property string mode: ""
        property point last
        property point panStart
        property int handle: -1
        // The mark under the pointer, with the select tool.
        property int hovered: -1
        // The guide under the pointer, or being dragged.
        property int hoveredGuide: -1
        property int guide: -1
        // A mark being moved: where it started and how far it went.
        property rect moveStart
        property point dragStart
        property point moved

        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
        cursorShape: {
            if (mode === "pan" || (area.panEnabled && area.spaceHeld)) return Qt.ClosedHandCursor
            if (mode === "drag") return Qt.SizeAllCursor
            if (mode === "handle") return area.handleCursor(handle)
            if (mode === "guide" || (area.tool === Document.Select && hoveredGuide >= 0)) {
                const g = area.guides[mode === "guide" ? guide : hoveredGuide]
                return g && g.vertical ? Qt.SplitHCursor : Qt.SplitVCursor
            }
            if (area.tool === Document.Select) return hovered >= 0 ? Qt.SizeAllCursor : Qt.ArrowCursor
            if (area.tool === Document.Text) return Qt.IBeamCursor
            return Qt.CrossCursor
        }

        onPressed: event => {
            const p = area.snap(marks.toImage(event.x, event.y))
            if (area.panEnabled && (event.button === Qt.MiddleButton || area.spaceHeld)) {
                mode = "pan"
                panStart = Qt.point(event.x, event.y)
                return
            }
            if (event.button !== Qt.LeftButton) {
                if (area.document.drawing)
                    area.document.cancelAnnotation()
                else
                    area.document.selectedIndex = -1
                return
            }
            textEntry.commit()
            if (area.tool === Document.Select) {
                const h = area.handleAt(event.x, event.y)
                if (h >= 0) {
                    area.document.beginHandleDrag(area.document.selectedIndex)
                    mode = "handle"
                    handle = h
                    return
                }
                const g = area.guideAt(event.x, event.y)
                if (g >= 0) {
                    mode = "guide"
                    guide = g
                    return
                }
                const free = marks.toImage(event.x, event.y)
                const hit = area.document.hitTest(free.x, free.y, area.zoom)
                area.document.selectedIndex = hit
                if (hit >= 0) {
                    area.document.beginMove(hit)
                    mode = "drag"
                    moveStart = area.document.snapBounds(hit, area.zoom)
                    dragStart = free
                    moved = Qt.point(0, 0)
                }
                return
            }
            if (area.tool === Document.Text) {
                const v = marks.toView(p.x, p.y)
                textEntry.openAt(v.x, v.y, p.x, p.y, "")
                return
            }
            area.document.beginAnnotation(area.tool, p.x, p.y, area.toolColor, area.size)
            mode = "draw"
        }

        onDoubleClicked: event => {
            if (event.button !== Qt.LeftButton || area.tool !== Document.Select)
                return
            const p = marks.toImage(event.x, event.y)
            const hit = area.document.hitTest(p.x, p.y, area.zoom)
            if (hit >= 0 && area.document.annotationTool(hit) === Document.Text) {
                if (mode === "drag")
                    area.document.endMove()
                mode = ""
                area.editText(hit)
            }
        }

        onPositionChanged: event => {
            const free = marks.toImage(event.x, event.y)
            const p = area.snap(free)
            area.cursor = free
            switch (mode) {
            case "pan":
                area.panned(event.x - panStart.x, event.y - panStart.y)
                panStart = Qt.point(event.x, event.y)
                break
            case "draw":
                area.document.updateAnnotation(p.x, p.y, area.constrainFor(event.modifiers))
                break
            case "drag": {
                // The whole way from the start, its edges pulled to guides.
                let dx = free.x - dragStart.x
                let dy = free.y - dragStart.y
                if (area.snapToGuides) {
                    const r = moveStart
                    dx += area.snapShift([r.x + dx, r.x + r.width + dx, r.x + r.width / 2 + dx], true)
                    dy += area.snapShift([r.y + dy, r.y + r.height + dy, r.y + r.height / 2 + dy], false)
                }
                area.document.moveBy(dx - moved.x, dy - moved.y)
                moved = Qt.point(dx, dy)
                break
            }
            case "handle":
                area.document.dragHandle(handle, p.x, p.y, event.modifiers)
                break
            case "guide": {
                const g = area.guides[guide]
                if (g)
                    area.document.moveGuide(guide, g.vertical ? free.x : free.y)
                break
            }
            case "":
                hovered = area.tool === Document.Select ? area.document.hitTest(free.x, free.y, area.zoom) : -1
                hoveredGuide = area.tool === Document.Select && hovered < 0 ? area.guideAt(event.x, event.y) : -1
                break
            }
        }

        onReleased: event => {
            if (mode === "draw")
                area.document.commitAnnotation()
            else if (mode === "drag")
                area.document.endMove()
            else if (mode === "handle")
                area.document.endHandleDrag()
            else if (mode === "guide" && (event.x < 0 || event.y < 0 || event.x > width || event.y > height))
                area.document.removeGuide(guide)
            mode = ""
            guide = -1
        }

        onExited: {
            area.cursor = Qt.point(-1, -1)
            hovered = -1
            hoveredGuide = -1
        }

        onWheel: wheel => {
            if (!area.panEnabled) {
                wheel.accepted = false
                return
            }
            if (wheel.modifiers & Qt.ControlModifier) {
                area.zoomRequested(Math.pow(1.25, wheel.angleDelta.y / 120), wheel.x, wheel.y)
                return
            }
            let dx = wheel.pixelDelta.x !== 0 ? wheel.pixelDelta.x : wheel.angleDelta.x / 120 * 60
            let dy = wheel.pixelDelta.y !== 0 ? wheel.pixelDelta.y : wheel.angleDelta.y / 120 * 60
            if (wheel.modifiers & Qt.ShiftModifier) {
                dx = dy
                dy = 0
            }
            area.scrolled(dx, dy)
        }
    }

    // Guides, across the whole area.
    Repeater {
        model: area.guides
        Rectangle {
            required property var modelData
            required property int index
            readonly property real at: Math.round(((modelData.vertical ? modelData.position - area.originX
                                                                      : modelData.position - area.originY)) * area.zoom)
            x: modelData.vertical ? at : 0
            y: modelData.vertical ? 0 : at
            width: modelData.vertical ? 1 : area.width
            height: modelData.vertical ? area.height : 1
            color: index === area.draggedGuide || index === mouse.hoveredGuide ? Theme.accent : "#2d8cff"
        }
    }

    // Where the selected mark can be reshaped.
    Repeater {
        model: mouse.mode === "draw" ? 0 : area.handlePoints.length
        Rectangle {
            required property int index
            readonly property point at: area.handlePoints[index] ?? Qt.point(0, 0)
            x: (at.x - area.originX) * area.zoom - width / 2
            y: (at.y - area.originY) * area.zoom - height / 2
            width: 9
            height: 9
            radius: area.handlePoints.length === 2 ? 4.5 : 0
            color: "white"
            border.color: "#a0000000"
            border.width: 1
            HoverHandler { cursorShape: area.handleCursor(parent.index) }
        }
    }

    TextEntry {
        id: textEntry
        // An edited text keeps its own size and color.
        // Text grows with the image, so it is typed at its size on screen.
        pixelSize: (area.editingIndex >= 0 && area.document.revision >= 0
                    ? area.document.annotationSize(area.editingIndex) : area.textSize) * area.zoom
        color: area.editingIndex >= 0 && area.document.revision >= 0
               ? area.document.annotationColor(area.editingIndex) : area.toolColor
        onAccepted: text => {
            const index = area.editingIndex
            area.editingIndex = -1
            if (index < 0) {
                const count = area.document.annotationCount
                area.document.addText(imagePoint.x, imagePoint.y, text, area.toolColor, area.textSize)
                // Selected, so the toolbar's color and size go to it.
                if (area.document.annotationCount > count)
                    area.document.selectedIndex = count
            } else if (text !== area.document.annotationText(index))
                area.document.setAnnotationText(index, text)
        }
        onCancelled: area.editingIndex = -1
    }
}
