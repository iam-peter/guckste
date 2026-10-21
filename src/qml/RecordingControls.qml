pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Guckste
import Guckste.Core

// While recording: a small panel with the time, pause and stop, placed
// outside the region, and a red frame around the region made of four thin
// windows that let clicks through. Nothing of it is in the recording,
// unless the region is a whole screen; then the panel is left out and the
// tray menu stops the recording.
Window {
    id: root

    required property ScreenRecorder recorder

    readonly property rect region: recorder.region
    readonly property bool paused: recorder.state === ScreenRecorder.Paused

    function clamp(value: real, low: real, high: real): real {
        return Math.max(low, Math.min(high, value))
    }

    function place() {
        const screenRect = Qt.rect(Screen.virtualX, Screen.virtualY, Screen.width, Screen.height)
        x = clamp(region.x + region.width / 2 - width / 2, screenRect.x + 8, screenRect.x + screenRect.width - width - 8)
        if (region.y - height - 12 >= screenRect.y)
            y = region.y - height - 12
        else if (region.y + region.height + 12 + height <= screenRect.y + screenRect.height)
            y = region.y + region.height + 12
        else
            y = screenRect.y + 8
    }

    function time(ms: int): string {
        const s = Math.floor(ms / 1000)
        const m = Math.floor(s / 60)
        const h = Math.floor(m / 60)
        const two = n => (n < 10 ? "0" : "") + n
        return h > 0 ? `${h}:${two(m % 60)}:${two(s % 60)}` : `${two(m)}:${two(s % 60)}`
    }

    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.BypassWindowManagerHint
    width: row.implicitWidth + 20
    height: row.implicitHeight + 12
    color: "transparent"
    visible: !recorder.wholeScreen
    title: qsTr("Recording")

    Component.onCompleted: place()

    Rectangle {
        anchors.fill: parent
        radius: 8
        color: Theme.panel
        border.color: "#55ffffff"
        border.width: 1

        // Drag the panel anywhere.
        MouseArea {
            id: drag
            property point pressAt
            anchors.fill: parent
            onPressed: event => pressAt = Qt.point(event.x, event.y)
            onPositionChanged: event => {
                if (pressed) {
                    root.x += event.x - pressAt.x
                    root.y += event.y - pressAt.y
                }
            }
        }

        RowLayout {
            id: row
            anchors.centerIn: parent
            spacing: 8

            Rectangle {
                id: dot
                implicitWidth: 12
                implicitHeight: 12
                radius: 6
                color: "#ff3b30"
                opacity: root.paused ? 0.4 : 1
                SequentialAnimation on opacity {
                    running: !root.paused && root.recorder.state === ScreenRecorder.Recording
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.25; duration: 600 }
                    NumberAnimation { to: 1; duration: 600 }
                }
            }
            Label {
                text: root.time(root.recorder.duration)
                color: Theme.panelText
                font.pixelSize: 14
                font.family: "monospace"
                Layout.preferredWidth: 60
            }
            Label {
                text: `${root.region.width} × ${root.region.height}`
                color: Theme.panelText
                opacity: 0.7
                font.pixelSize: 12
            }
            IconButton {
                iconName: root.paused ? "play" : "pause"
                tip: root.recorder.canPause ? (root.paused ? qsTr("Resume") : qsTr("Pause"))
                                            : qsTr("No pause while the microphone is recorded")
                enabled: root.recorder.canPause
                palette.buttonText: Theme.panelText
                onClicked: root.paused ? root.recorder.resume() : root.recorder.pause()
            }
            IconButton {
                iconName: "circle-stop"
                tip: qsTr("Stop and save")
                palette.buttonText: "#ff3b30"
                enabled: root.recorder.state !== ScreenRecorder.Stopping
                onClicked: root.recorder.stop()
            }
        }
    }

    // The frame: top, bottom, left, right, each 2 px outside the region.
    Instantiator {
        model: root.recorder.wholeScreen ? 0 : 4
        delegate: Window {
            id: edge
            required property int index
            readonly property int thickness: 2
            readonly property rect r: root.region
            flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.BypassWindowManagerHint
                   | Qt.WindowTransparentForInput | Qt.WindowDoesNotAcceptFocus
            color: root.paused ? "#ff9500" : "#ff3b30"
            x: index === 3 ? r.x + r.width : r.x - thickness
            y: index === 1 ? r.y + r.height : r.y - thickness
            width: index < 2 ? r.width + 2 * thickness : thickness
            height: index < 2 ? thickness : r.height + 2 * thickness
            visible: true
        }
    }
}
