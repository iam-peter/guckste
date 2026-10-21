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
// right-click for more.
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

    implicitHeight: Math.min(count, 4) * laneHeight
    visible: count > 0
    clip: true
    // Scrolled by the wheel and the scroll bar; a drag is for the bars.
    interactive: false
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

        readonly property bool current: layerIndex === lanes.document.selectedIndex
        readonly property int shownEnd: endMs < 0 ? lanes.duration : endMs

        width: ListView.view.width
        height: lanes.laneHeight

        function pick() {
            lanes.document.selectedIndex = lane.layerIndex
            lanes.picked(lane.layerIndex)
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
            TapHandler { onTapped: lane.pick() }
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
            property int startAtPress
            property int endAtPress

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
                    lane.pick()
                    menu.popup()
                    return
                }
                const edge = edgeAt(event.x)
                if (edge === "" && !overBar(event.x)) {
                    lane.pick()
                    return
                }
                lanes.document.selectedIndex = lane.layerIndex
                lanes.picked(lane.layerIndex)
                mode = edge !== "" ? edge : "move"
                pressX = event.x
                startAtPress = lane.startMs
                endAtPress = lane.shownEnd
                lanes.document.beginRetime(lane.layerIndex)
            }
            onPositionChanged: event => {
                if (mode === "")
                    return
                const minimum = 100
                const delta = msAt(event.x) - msAt(pressX)
                let start = startAtPress
                let end = endAtPress
                if (mode === "move") {
                    const length = endAtPress - startAtPress
                    start = Math.max(0, Math.min(lanes.duration - length, startAtPress + delta))
                    end = start + length
                } else if (mode === "in") {
                    start = Math.max(0, Math.min(endAtPress - minimum, startAtPress + delta))
                } else {
                    end = Math.min(lanes.duration, Math.max(startAtPress + minimum, endAtPress + delta))
                }
                // Up to the end stays "to the end".
                lanes.document.retime(start, end >= lanes.duration ? -1 : end)
            }
            onReleased: {
                if (mode !== "")
                    lanes.document.endRetime()
                mode = ""
            }
            onDoubleClicked: lanes.seekRequested(lane.startMs)

            ToolTip.visible: mode !== ""
            ToolTip.text: `${lanes.timeline.time(lane.startMs)} – ${lanes.timeline.time(lane.shownEnd)}`
        }

        Menu {
            id: menu
            MenuItem {
                text: qsTr("Show from the playhead")
                enabled: lane.endMs < 0 || lanes.position < lane.endMs
                onTriggered: lanes.document.setAnnotationTiming(lane.layerIndex, lanes.position, lane.endMs)
            }
            MenuItem {
                text: qsTr("Hide from the playhead")
                enabled: lanes.position > lane.startMs
                onTriggered: lanes.document.setAnnotationTiming(lane.layerIndex, lane.startMs, lanes.position)
            }
            MenuItem {
                text: qsTr("Show the whole time")
                onTriggered: lanes.document.setAnnotationTiming(lane.layerIndex, 0, -1)
            }
            MenuSeparator {}
            MenuItem {
                text: qsTr("Remove")
                onTriggered: lanes.document.removeAnnotation(lane.layerIndex)
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
