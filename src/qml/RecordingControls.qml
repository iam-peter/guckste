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

    // The screen the region is on; before the window is shown, `Screen`
    // would be the primary one.
    function screenOfRegion(): rect {
        const cx = region.x + region.width / 2
        const cy = region.y + region.height / 2
        const screens = Qt.application.screens
        let all = Qt.rect(0, 0, 0, 0)
        for (let i = 0; i < screens.length; ++i) {
            const s = screens[i]
            const r = Qt.rect(s.virtualX, s.virtualY, s.width, s.height)
            if (cx >= r.x && cx < r.x + r.width && cy >= r.y && cy < r.y + r.height)
                return r
            all = i === 0 ? r : Qt.rect(Math.min(all.x, r.x), Math.min(all.y, r.y),
                                        Math.max(all.x + all.width, r.x + r.width) - Math.min(all.x, r.x),
                                        Math.max(all.y + all.height, r.y + r.height) - Math.min(all.y, r.y))
        }
        return all
    }

    function place() {
        const screenRect = screenOfRegion()
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
                opacity: root.paused || root.recorder.starting ? 0.4 : 1
                SequentialAnimation on opacity {
                    running: !root.paused && !root.recorder.starting && root.recorder.state === ScreenRecorder.Recording
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.25; duration: 600 }
                    NumberAnimation { to: 1; duration: 600 }
                }
            }
            Label {
                // The first moment is left out, while windows the overlay
                // covered draw again.
                text: root.recorder.starting ? qsTr("Starting…") : root.time(root.recorder.duration)
                color: Theme.panelText
                font.pixelSize: 14
                font.family: "monospace"
                Layout.preferredWidth: 80
            }
            Label {
                text: `${root.region.width} × ${root.region.height}`
                color: Theme.panelText
                opacity: 0.7
                font.pixelSize: 12
            }
            // The last keys pressed, when they are recorded.
            Label {
                visible: root.recorder.recentKeys.length > 0
                text: root.recorder.recentKeys.slice(-3).join("  ")
                color: Theme.panelText
                font.pixelSize: 12
                font.bold: true
                Layout.maximumWidth: 180
                elide: Text.ElideLeft
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
