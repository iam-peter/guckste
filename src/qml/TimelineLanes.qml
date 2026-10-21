pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Guckste
import Guckste.Core

// A lane under the timeline for every mark on a recording, topmost first:
// its name on the left, and a bar for when it shows, on the timeline's
// scale. Drag the bar to move it in time, its edges to set when it comes
// and goes; click to select the mark, double-click to go to its start,
// right-click for more. Ctrl-click and Shift-click select several lanes,
// which then move and stretch together.
ListView {
    id: lanes

    required property Document document
    // The timeline above, whose scale the bars share; it starts at
    // `trackX` in this list.
    required property Timeline timeline
    property real trackX: 0
    property int position: 0
    property int trimStart: 0
    property int trimEnd: -1
    readonly property int duration: timeline.duration
    readonly property real laneHeight: 22

    signal picked(int index)
    signal seekRequested(int ms)

    // The marks whose lanes are selected, by document index.
    property var chosen: []
    // Where a Shift-click range starts, by row.
    property int anchorRow: -1

    function isChosen(index: int): bool {
        return chosen.indexOf(index) >= 0
    }

    // Selects a lane as a click with these modifiers does.
    function choose(row: int, index: int, modifiers: int) {
        if (modifiers & Qt.ShiftModifier && anchorRow >= 0) {
            const list = []
            for (let r = Math.min(anchorRow, row); r <= Math.max(anchorRow, row); ++r)
                list.push(count - 1 - r)
            chosen = list
        } else if (modifiers & (Qt.ControlModifier | Qt.MetaModifier)) {
            chosen = isChosen(index) ? chosen.filter(i => i !== index) : chosen.concat([index])
            anchorRow = row
        } else {
            chosen = [index]
            anchorRow = row
        }
        document.selectedIndex = index
        picked(index)
    }

    // The selected lanes' timings, ends to the end made explicit.
    function chosenTimings(): var {
        return chosen.map(i => ({
            index: i,
            start: document.annotationStart(i),
            end: document.annotationEnd(i) < 0 ? duration : document.annotationEnd(i)
        }))
    }

    // Applies timings, an end at the very end staying "to the end".
    function timingList(timings: var): var {
        return timings.map(t => [t.index, t.start, t.end >= duration ? -1 : t.end])
    }

    Connections {
        target: lanes.model
        // Lanes came, went or moved: indices no longer hold.
        function onModelReset() {
            lanes.chosen = []
            lanes.anchorRow = -1
        }
    }

    implicitHeight: Math.min(count, 4) * laneHeight
    visible: count > 0
    clip: true
    // Scrolls by the wheel, the scroll bar, or a drag beside the bars; a
    // drag on a bar is the bar's.
    flickableDirection: Flickable.VerticalFlick
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
    model: LayerModel {
        document: lanes.document
        includeBase: false
    }

    function msToX(ms: int): real {
        return lanes.trackX + lanes.timeline.msToX(ms)
    }

    delegate: Item {
        id: lane

        required property int index
        required property int layerIndex
        required property int kind
        required property string label
        required property color layerColor
        required property int startMs
        required property int endMs

        readonly property bool current: lanes.isChosen(layerIndex)
                                        || (lanes.chosen.length === 0 && layerIndex === lanes.document.selectedIndex)
        readonly property int shownEnd: endMs < 0 ? lanes.duration : endMs

        width: ListView.view.width
        height: lanes.laneHeight

        function pick(modifiers: int) {
            lanes.choose(lane.index, lane.layerIndex, modifiers)
            // Out of view at the playhead: go to where it shows.
            if (!lanes.document.isShown(lane.layerIndex))
                lanes.seekRequested(lane.startMs)
        }

        // The name, under the play controls.
        RowLayout {
            x: 4
            width: lanes.trackX - 8
            height: parent.height
            spacing: 4
            Icon {
                name: Icons.forTool(lane.kind)
                color: lanes.palette.text
                size: 12
                opacity: 0.8
            }
            Label {
                Layout.fillWidth: true
                text: lane.label
                elide: Text.ElideRight
                font.pixelSize: 11
                font.bold: lane.current
            }
        }

        MouseArea {
            width: lanes.trackX
            height: parent.height
            onClicked: event => lane.pick(event.modifiers)
        }

        // The track, and the bar for when the mark shows.
        Rectangle {
            x: lanes.msToX(0)
            y: 2
            width: Math.max(0, lanes.msToX(lanes.duration) - x)
            height: parent.height - 4
            radius: 3
            color: Qt.darker(lanes.palette.window, 1.35)
        }
        Rectangle {
            id: bar
            x: lanes.msToX(lane.startMs)
            y: 3
            width: Math.max(4, lanes.msToX(lane.shownEnd) - x)
            height: parent.height - 6
            radius: 3
            color: Qt.rgba(lane.layerColor.r, lane.layerColor.g, lane.layerColor.b, lane.current ? 0.75 : 0.5)
            border.color: lane.current ? Theme.accent : Qt.darker(lane.layerColor, 1.4)
            border.width: lane.current ? 2 : 1
            Label {
                anchors.fill: parent
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                verticalAlignment: Text.AlignVCenter
                text: lane.label
                elide: Text.ElideRight
                font.pixelSize: 10
                color: "white"
                style: Text.Outline
                styleColor: "#80000000"
            }
        }

        MouseArea {
            id: drag

            // "move", "in", "out"
            property string mode: ""
            property real pressX
            // The selected lanes' timings when the drag began.
            property var atPress: []
            property bool moved: false

            x: lanes.msToX(0) - 6
            width: lanes.msToX(lanes.duration) - x + 12
            height: parent.height
            hoverEnabled: true
            preventStealing: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            cursorShape: mode === "in" || mode === "out" || edgeAt(mouseX) !== "" ? Qt.SizeHorCursor
                       : mode === "move" ? Qt.ClosedHandCursor
                       : overBar(mouseX) ? Qt.OpenHandCursor : Qt.ArrowCursor

            function toLane(x: real): real {
                return x + drag.x
            }
            function overBar(x: real): bool {
                const at = toLane(x)
                return at >= bar.x && at <= bar.x + bar.width
            }
            function edgeAt(x: real): string {
                const at = toLane(x)
                if (Math.abs(at - bar.x) <= 5)
                    return "in"
                if (Math.abs(at - (bar.x + bar.width)) <= 5)
                    return "out"
                return ""
            }
            function msAt(x: real): int {
                return lanes.timeline.xToMs(toLane(x) - lanes.trackX)
            }

            onPressed: event => {
                if (event.button === Qt.RightButton) {
                    // Keeps a selection the lane is in, for the menu.
                    if (!lanes.isChosen(lane.layerIndex))
                        lane.pick(Qt.NoModifier)
                    menu.popup()
                    return
                }
                const edge = edgeAt(event.x)
                const modifiers = event.modifiers & (Qt.ShiftModifier | Qt.ControlModifier | Qt.MetaModifier)
                if ((edge === "" && !overBar(event.x)) || modifiers) {
                    lane.pick(event.modifiers)
                    return
                }
                // A bar outside the selection is taken alone.
                if (!lanes.isChosen(lane.layerIndex))
                    lane.pick(Qt.NoModifier)
                else {
                    lanes.document.selectedIndex = lane.layerIndex
                    lanes.picked(lane.layerIndex)
                }
                mode = edge !== "" ? edge : "move"
                moved = false
                pressX = event.x
                atPress = lanes.chosenTimings()
                lanes.document.beginRetimeMany()
            }
            onPositionChanged: event => {
                if (mode === "")
                    return
                const minimum = 100
                let delta = msAt(event.x) - msAt(pressX)
                if (Math.abs(event.x - pressX) >= 2)
                    moved = true
                let timings
                if (mode === "move") {
                    // Together, held so none leaves the video.
                    const first = Math.min(...atPress.map(t => t.start))
                    const last = Math.max(...atPress.map(t => t.end))
                    delta = Math.max(-first, Math.min(lanes.duration - last, delta))
                    timings = atPress.map(t => ({ index: t.index, start: t.start + delta, end: t.end + delta }))
                } else if (mode === "in") {
                    timings = atPress.map(t => ({
                        index: t.index,
                        start: Math.max(0, Math.min(t.end - minimum, t.start + delta)),
                        end: t.end
                    }))
                } else {
                    timings = atPress.map(t => ({
                        index: t.index,
                        start: t.start,
                        end: Math.min(lanes.duration, Math.max(t.start + minimum, t.end + delta))
                    }))
                }
                lanes.document.retimeMany(lanes.timingList(timings))
            }
            onReleased: {
                if (mode !== "")
                    lanes.document.endRetime()
                // A click on a bar of several selected: that one alone.
                if (mode === "move" && !moved && lanes.chosen.length > 1)
                    lanes.choose(lane.index, lane.layerIndex, Qt.NoModifier)
                mode = ""
            }
            onDoubleClicked: lanes.seekRequested(lane.startMs)

            ToolTip.visible: mode !== ""
            ToolTip.text: `${lanes.timeline.time(lane.startMs)} – ${lanes.timeline.time(lane.shownEnd)}`
                          + (lanes.chosen.length > 1 ? qsTr(" (%1 marks)").arg(lanes.chosen.length) : "")
        }

        // For every selected lane.
        Menu {
            id: menu
            MenuItem {
                text: qsTr("Show from the playhead")
                onTriggered: lanes.document.setTimings(lanes.timingList(lanes.chosenTimings()
                    .filter(t => lanes.position < t.end)
                    .map(t => ({ index: t.index, start: lanes.position, end: t.end }))))
            }
            MenuItem {
                text: qsTr("Hide from the playhead")
                onTriggered: lanes.document.setTimings(lanes.timingList(lanes.chosenTimings()
                    .filter(t => lanes.position > t.start)
                    .map(t => ({ index: t.index, start: t.start, end: lanes.position }))))
            }
            MenuItem {
                text: qsTr("Show the whole time")
                onTriggered: lanes.document.setTimings(lanes.chosen.map(i => [i, 0, -1]))
            }
            MenuSeparator {}
            MenuItem {
                text: lanes.chosen.length > 1 ? qsTr("Remove %1 marks").arg(lanes.chosen.length) : qsTr("Remove")
                onTriggered: {
                    // From the top down, so the indices below stay.
                    const indices = lanes.chosen.slice().sort((a, b) => b - a)
                    lanes.chosen = []
                    for (const i of indices)
                        lanes.document.removeAnnotation(i)
                }
            }
        }
    }

    // What the trim cuts away, dimmed, and the playhead, across all lanes.
    Rectangle {
        parent: lanes
        x: lanes.msToX(0)
        width: Math.max(0, lanes.msToX(lanes.trimStart) - x)
        height: lanes.height
        color: "#60000000"
    }
    Rectangle {
        parent: lanes
        x: lanes.msToX(lanes.trimEnd < 0 ? lanes.duration : lanes.trimEnd)
        width: Math.max(0, lanes.msToX(lanes.duration) - x)
        height: lanes.height
        color: "#60000000"
    }
    Rectangle {
        parent: lanes
        x: lanes.msToX(lanes.position) - 1
        width: 2
        height: lanes.height
        color: "#ffffff"
    }
}
