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

        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
        cursorShape: {
            if (mode === "pan" || (area.panEnabled && area.spaceHeld)) return Qt.ClosedHandCursor
            if (mode === "drag") return Qt.SizeAllCursor
            if (mode === "handle") return area.handleCursor(handle)
            if (area.tool === Document.Select) return hovered >= 0 ? Qt.SizeAllCursor : Qt.ArrowCursor
            if (area.tool === Document.Text) return Qt.IBeamCursor
            return Qt.CrossCursor
        }

        onPressed: event => {
            const p = marks.toImage(event.x, event.y)
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
                const hit = area.document.hitTest(p.x, p.y, area.zoom)
                area.document.selectedIndex = hit
                if (hit >= 0) {
                    area.document.beginMove(hit)
                    mode = "drag"
                    last = p
                }
                return
            }
            if (area.tool === Document.Text) {
                textEntry.openAt(event.x, event.y, p.x, p.y, "")
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
            const p = marks.toImage(event.x, event.y)
            area.cursor = p
            switch (mode) {
            case "pan":
                area.panned(event.x - panStart.x, event.y - panStart.y)
                panStart = Qt.point(event.x, event.y)
                break
            case "draw":
                area.document.updateAnnotation(p.x, p.y, area.constrainFor(event.modifiers))
                break
            case "drag":
                area.document.moveBy(p.x - last.x, p.y - last.y)
                last = p
                break
            case "handle":
                area.document.dragHandle(handle, p.x, p.y, event.modifiers)
                break
            case "":
                hovered = area.tool === Document.Select ? area.document.hitTest(p.x, p.y, area.zoom) : -1
                break
            }
        }

        onReleased: {
            if (mode === "draw")
                area.document.commitAnnotation()
            else if (mode === "drag")
                area.document.endMove()
            else if (mode === "handle")
                area.document.endHandleDrag()
            mode = ""
        }

        onExited: {
            area.cursor = Qt.point(-1, -1)
            hovered = -1
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
        pixelSize: area.editingIndex >= 0 && area.document.revision >= 0
                   ? area.document.annotationSize(area.editingIndex) : area.textSize
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
