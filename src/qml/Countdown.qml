pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Guckste

// The seconds left until a region is captured again, shown outside the
// region on its screen; Escape or the button call it off.
Window {
    id: root

    required property rect region
    required property int seconds
    property int left: seconds

    signal elapsed()
    signal cancelled()

    function clamp(value: real, low: real, high: real): real {
        return Math.max(low, Math.min(high, value))
    }

    function screenOfRegion(): rect {
        const cx = region.x + region.width / 2
        const cy = region.y + region.height / 2
        const screens = Qt.application.screens
        for (let i = 0; i < screens.length; ++i) {
            const s = screens[i]
            const r = Qt.rect(s.virtualX, s.virtualY, s.width, s.height)
            if (cx >= r.x && cx < r.x + r.width && cy >= r.y && cy < r.y + r.height)
                return r
        }
        return Qt.rect(screens[0].virtualX, screens[0].virtualY, screens[0].width, screens[0].height)
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

    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.BypassWindowManagerHint
    width: row.implicitWidth + 24
    height: row.implicitHeight + 14
    color: "transparent"
    title: qsTr("Capturing soon")

    Component.onCompleted: place()

    Timer {
        interval: 1000
        running: root.visible && root.left > 0
        repeat: true
        onTriggered: {
            root.left -= 1
            if (root.left <= 0)
                root.elapsed()
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: 8
        color: Theme.panel
        border.color: "#55ffffff"
        border.width: 1

        RowLayout {
            id: row
            anchors.centerIn: parent
            spacing: 10
            Icon {
                name: "timer"
                color: Theme.panelText
                size: 18
            }
            Label {
                text: qsTr("Capturing in %1").arg(root.left)
                color: Theme.panelText
                font.pixelSize: 16
                font.bold: true
                Layout.preferredWidth: 130
            }
            IconButton {
                iconName: "x"
                tip: qsTr("Cancel (Esc)")
                palette.buttonText: Theme.panelText
                onClicked: root.cancelled()
            }
        }
    }

    Shortcut { sequence: "Escape"; onActivated: root.cancelled() }
}
