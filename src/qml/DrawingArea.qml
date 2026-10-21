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
    property bool showGrid: false
    property real gridMinZoom: 8
    // Middle button or Space drag the view; Ctrl+wheel zooms, wheel scrolls.
    property bool panEnabled: false
    property bool spaceHeld: false
    // The image pixel under the pointer, (-1, -1) outside.
    property point cursor: Qt.point(-1, -1)
    readonly property bool editingText: textEntry.visible
    readonly property bool busy: mouse.mode !== ""

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
    }

    MouseArea {
        id: mouse

        // "draw": an annotation; "drag": moving one; "pan": the view.
        property string mode: ""
        property point last
        property point panStart

        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
        cursorShape: {
            if (mode === "pan" || (area.panEnabled && area.spaceHeld)) return Qt.ClosedHandCursor
            if (mode === "drag") return Qt.SizeAllCursor
            if (area.tool === Document.Select) return Qt.ArrowCursor
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
                // Edit the text in place: the old one goes, the entry gets it.
                const at = area.document.annotationPosition(hit)
                const text = area.document.annotationText(hit)
                const v = marks.toView(at.x, at.y)
                area.document.removeAnnotation(hit)
                textEntry.openAt(v.x, v.y, at.x, at.y, text)
                mode = ""
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
            }
        }

        onReleased: {
            if (mode === "draw")
                area.document.commitAnnotation()
            else if (mode === "drag")
                area.document.endMove()
            mode = ""
        }

        onExited: area.cursor = Qt.point(-1, -1)

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

    TextEntry {
        id: textEntry
        pixelSize: 10 + area.size * 2
        color: area.toolColor
        onAccepted: text => area.document.addText(imagePoint.x, imagePoint.y, text, area.toolColor, area.size)
    }
}
