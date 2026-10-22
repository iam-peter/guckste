pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Guckste

// The video's timeline: a ruler, the playhead, and the in and out handles
// of the part to keep. Click or drag anywhere to scrub, drag a handle to
// trim; the parts cut away are dimmed.
Item {
    id: timeline

    property int duration: 0
    // How much time the width shows: the video, and room past its end to
    // drag things into.
    property int span: duration
    readonly property int shownSpan: Math.max(span, duration)
    property int position: 0
    property int trimStart: 0
    // -1: up to the end.
    property int trimEnd: -1
    readonly property int effectiveEnd: trimEnd < 0 ? duration : trimEnd
    // The least that may be kept.
    property int minimumKept: 100
    readonly property bool scrubbing: mouse.mode === "seek"

    signal seekRequested(int ms)
    signal trimStartRequested(int ms)
    signal trimEndRequested(int ms)

    readonly property real margin: 10
    readonly property real rulerHeight: 16
    readonly property real trackY: rulerHeight + 4
    readonly property real trackHeight: height - trackY - 4
    readonly property color keptColor: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.28)

    implicitHeight: 46
    implicitWidth: 300

    function msToX(ms: int): real {
        return shownSpan > 0 ? margin + ms / shownSpan * (width - 2 * margin) : margin
    }

    function xToMs(x: real): int {
        if (shownSpan <= 0)
            return 0
        return Math.round(Math.max(0, Math.min(1, (x - margin) / (width - 2 * margin))) * shownSpan)
    }

    function time(ms: int): string {
        const s = Math.floor(Math.max(0, ms) / 1000)
        const m = Math.floor(s / 60)
        const two = n => (n < 10 ? "0" : "") + n
        return `${two(m)}:${two(s % 60)}`
    }

    // Ticks at a round step wide enough to label.
    readonly property int tickStep: {
        const steps = [250, 500, 1000, 2000, 5000, 10000, 15000, 30000, 60000, 120000, 300000, 600000]
        const usable = width - 2 * margin
        for (const step of steps) {
            if (shownSpan <= 0 || usable * step / shownSpan >= 64)
                return step
        }
        return steps[steps.length - 1]
    }

    // Ruler
    Repeater {
        model: timeline.shownSpan > 0 ? Math.floor(timeline.shownSpan / timeline.tickStep) + 1 : 0
        Item {
            id: tick
            required property int index
            readonly property int ms: index * timeline.tickStep
            x: timeline.msToX(ms)
            y: 0
            Rectangle {
                x: -0.5
                y: timeline.rulerHeight - 4
                width: 1
                height: 4
                color: timeline.palette.placeholderText
            }
            Label {
                x: 3
                y: -1
                font.pixelSize: 10
                color: timeline.palette.placeholderText
                text: timeline.tickStep < 1000 ? (tick.ms / 1000).toFixed(2) : timeline.time(tick.ms)
            }
        }
    }

    // Track, as long as the video
    Rectangle {
        x: timeline.margin
        y: timeline.trackY
        width: Math.max(0, timeline.msToX(timeline.duration) - x)
        height: timeline.trackHeight
        radius: 4
        color: Qt.darker(timeline.palette.window, 1.6)
        border.color: Qt.darker(timeline.palette.window, 2)
        border.width: 1
    }
    // The kept part
    Rectangle {
        x: timeline.msToX(timeline.trimStart)
        y: timeline.trackY + 1
        width: Math.max(0, timeline.msToX(timeline.effectiveEnd) - x)
        height: timeline.trackHeight - 2
        color: timeline.keptColor
    }
    // The cut parts, hatched by a dim
    Rectangle {
        x: timeline.margin
        y: timeline.trackY + 1
        width: Math.max(0, timeline.msToX(timeline.trimStart) - x)
        height: timeline.trackHeight - 2
        color: "#70000000"
    }
    Rectangle {
        x: timeline.msToX(timeline.effectiveEnd)
        y: timeline.trackY + 1
        width: Math.max(0, timeline.msToX(timeline.duration) - x)
        height: timeline.trackHeight - 2
        color: "#70000000"
    }

    // Playhead
    Item {
        x: timeline.msToX(timeline.position)
        y: 0
        Rectangle {
            x: -1
            y: timeline.rulerHeight - 2
            width: 2
            height: timeline.height - y
            color: "#ffffff"
        }
        Rectangle {
            x: -6
            y: timeline.rulerHeight - 8
            width: 12
            height: 12
            rotation: 45
            radius: 2
            color: "#ffffff"
        }
    }

    // In and out handles
    Repeater {
        model: 2
        Rectangle {
            id: handle
            required property int index
            readonly property bool isStart: index === 0
            readonly property int ms: isStart ? timeline.trimStart : timeline.effectiveEnd
            readonly property bool dragged: mouse.mode === (isStart ? "start" : "end")
            x: timeline.msToX(ms) - (isStart ? width : 0)
            y: timeline.trackY - 3
            width: 10
            height: timeline.trackHeight + 6
            radius: 3
            color: dragged ? Qt.lighter(Theme.accent, 1.2) : Theme.accent
            Rectangle {
                anchors.centerIn: parent
                width: 2
                height: parent.height * 0.45
                radius: 1
                color: "#c0000000"
            }
            ToolTip.visible: handle.dragged
            ToolTip.text: timeline.time(handle.ms)
        }
    }

    MouseArea {
        id: mouse

        // "seek", "start", "end"
        property string mode: ""
        property real hoverX: -1

        anchors.fill: parent
        hoverEnabled: true
        cursorShape: {
            if (mode === "start" || mode === "end" || handleAt(hoverX) !== "")
                return Qt.SizeHorCursor
            return Qt.PointingHandCursor
        }

        function handleAt(x: real): string {
            if (x < 0)
                return ""
            const startX = timeline.msToX(timeline.trimStart)
            const endX = timeline.msToX(timeline.effectiveEnd)
            if (x >= startX - 12 && x <= startX + 2)
                return "start"
            if (x >= endX - 2 && x <= endX + 12)
                return "end"
            return ""
        }

        onPressed: event => {
            const h = handleAt(event.x)
            mode = h !== "" ? h : "seek"
            apply(event.x)
        }
        onPositionChanged: event => {
            hoverX = event.x
            if (pressed)
                apply(event.x)
        }
        onReleased: mode = ""
        onExited: hoverX = -1

        function apply(x: real) {
            // Within the video, past which there is nothing to see.
            const ms = Math.min(timeline.xToMs(x), timeline.duration)
            switch (mode) {
            case "seek":
                timeline.seekRequested(ms)
                break
            case "start":
                timeline.trimStartRequested(Math.min(ms, timeline.effectiveEnd - timeline.minimumKept))
                break
            case "end":
                timeline.trimEndRequested(Math.max(ms, timeline.trimStart + timeline.minimumKept))
                break
            }
        }

        ToolTip.visible: hoverX >= 0 && mode === "" && handleAt(hoverX) === ""
        ToolTip.text: timeline.time(timeline.xToMs(hoverX))
        ToolTip.delay: 300
    }
}
