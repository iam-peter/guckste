pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Guckste

// Any color, picked by hue, saturation and value or typed as hex. A popup
// inside the window instead of a dialog: the capture overlay bypasses the
// window manager and stays on top, so a dialog window would open hidden
// behind it, and Qt centers its in-window dialog on the overlay, which
// spans every screen.
Popup {
    id: picker

    property color current: "#ff3b30"
    signal picked(color color)

    property real hue: 0
    property real saturation: 1
    property real value: 1
    readonly property color chosen: Qt.hsva(hue, saturation, value, 1)

    popupType: Popup.Item
    modal: false
    focus: true
    margins: 8
    padding: 10
    closePolicy: Popup.CloseOnPressOutside

    function apply() {
        picker.picked(chosen)
        close()
    }

    function setFromHex(text: string): bool {
        if (!/^#?[0-9a-fA-F]{6}$/.test(text))
            return false
        const c = Qt.color(text.startsWith("#") ? text : "#" + text)
        hue = c.hsvHue >= 0 ? c.hsvHue : hue
        saturation = c.hsvSaturation
        value = c.hsvValue
        return true
    }

    function hex(c: color): string {
        const h = n => Math.round(n * 255).toString(16).padStart(2, "0")
        return "#" + h(c.r) + h(c.g) + h(c.b)
    }

    onAboutToShow: {
        hue = current.hsvHue >= 0 ? current.hsvHue : 0
        saturation = current.hsvSaturation
        value = current.hsvValue
        hexField.text = hex(current)
        hexField.forceActiveFocus()
        hexField.selectAll()
    }

    background: Rectangle {
        color: Theme.panel
        radius: 8
        border.color: "#55ffffff"
        border.width: 1
    }

    contentItem: FocusScope {
        implicitWidth: column.implicitWidth
        implicitHeight: column.implicitHeight

        // Escape and Enter belong to the picker while it is open, not to
        // the window's shortcuts.
        Keys.onShortcutOverride: event => event.accepted = event.key === Qt.Key_Escape
                                 || event.key === Qt.Key_Return || event.key === Qt.Key_Enter
        Keys.onPressed: event => {
            if (event.key === Qt.Key_Escape) {
                picker.close()
                event.accepted = true
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                picker.apply()
                event.accepted = true
            }
        }

        ColumnLayout {
            id: column
            spacing: 8

            // Saturation left to right, value top to bottom.
            Rectangle {
                id: field
                Layout.preferredWidth: 200
                Layout.preferredHeight: 140
                radius: 3
                color: Qt.hsva(picker.hue, 1, 1, 1)
                Rectangle {
                    anchors.fill: parent
                    radius: 3
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0; color: "white" }
                        GradientStop { position: 1; color: "transparent" }
                    }
                }
                Rectangle {
                    anchors.fill: parent
                    radius: 3
                    gradient: Gradient {
                        GradientStop { position: 0; color: "transparent" }
                        GradientStop { position: 1; color: "black" }
                    }
                }
                Rectangle {
                    x: picker.saturation * field.width - width / 2
                    y: (1 - picker.value) * field.height - height / 2
                    width: 12
                    height: 12
                    radius: 6
                    color: "transparent"
                    border.color: picker.value > 0.5 ? "black" : "white"
                    border.width: 2
                }
                MouseArea {
                    anchors.fill: parent
                    preventStealing: true
                    function pick(x: real, y: real) {
                        picker.saturation = Math.max(0, Math.min(1, x / width))
                        picker.value = Math.max(0, Math.min(1, 1 - y / height))
                        hexField.text = picker.hex(picker.chosen)
                    }
                    onPressed: mouse => pick(mouse.x, mouse.y)
                    onPositionChanged: mouse => pick(mouse.x, mouse.y)
                    onDoubleClicked: picker.apply()
                }
            }

            // Hue.
            Rectangle {
                id: hueBar
                Layout.preferredWidth: 200
                Layout.preferredHeight: 14
                radius: 3
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0 / 6; color: "#ff0000" }
                    GradientStop { position: 1 / 6; color: "#ffff00" }
                    GradientStop { position: 2 / 6; color: "#00ff00" }
                    GradientStop { position: 3 / 6; color: "#00ffff" }
                    GradientStop { position: 4 / 6; color: "#0000ff" }
                    GradientStop { position: 5 / 6; color: "#ff00ff" }
                    GradientStop { position: 6 / 6; color: "#ff0000" }
                }
                Rectangle {
                    x: picker.hue * hueBar.width - width / 2
                    anchors.verticalCenter: parent.verticalCenter
                    width: 6
                    height: hueBar.height + 4
                    radius: 2
                    color: "transparent"
                    border.color: "white"
                    border.width: 2
                }
                MouseArea {
                    anchors.fill: parent
                    preventStealing: true
                    function pick(x: real) {
                        picker.hue = Math.max(0, Math.min(0.999, x / width))
                        hexField.text = picker.hex(picker.chosen)
                    }
                    onPressed: mouse => pick(mouse.x)
                    onPositionChanged: mouse => pick(mouse.x)
                }
            }

            RowLayout {
                spacing: 6
                Rectangle {
                    Layout.preferredWidth: 24
                    Layout.preferredHeight: 24
                    radius: 4
                    color: picker.chosen
                    border.color: "#80ffffff"
                    border.width: 1
                }
                TextField {
                    id: hexField
                    Layout.preferredWidth: 90
                    color: Theme.panelText
                    selectByMouse: true
                    maximumLength: 7
                    font.family: "monospace"
                    background: Rectangle {
                        color: "#3a3a44"
                        radius: 4
                        border.color: hexField.activeFocus ? Theme.accent : "transparent"
                    }
                    Keys.onShortcutOverride: event => event.accepted = event.key === Qt.Key_Escape
                                             || event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                    onTextEdited: picker.setFromHex(text)
                    onAccepted: picker.apply()
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("Use")
                    focusPolicy: Qt.NoFocus
                    onClicked: picker.apply()
                }
            }
        }
    }
}
