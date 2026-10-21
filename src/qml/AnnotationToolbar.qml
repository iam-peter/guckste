pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Guckste
import Guckste.Core

// The drawing tools, the color, the stroke size and undo, shared by the
// capture overlay and the editor. Whatever a window adds (copy, save, zoom)
// goes into `actions`.
ToolBar {
    id: bar

    property int tool: Document.Select
    property color color: "#ff3b30"
    property real size: 3
    property Document document
    // The overlay's floating look: a dark panel with light icons.
    property bool dark: false
    default property alias actions: actionsRow.data

    signal toolPicked(int tool)
    signal colorPicked(color color)
    signal sizePicked(real size)

    readonly property var tools: [
        { tool: Document.Select, icon: "mouse-pointer-2", tip: qsTr("Select and move (V)") },
        { tool: Document.Pen, icon: "pencil", tip: qsTr("Pen (P)") },
        { tool: Document.Line, icon: "slash", tip: qsTr("Line (L) — Shift snaps to 45°") },
        { tool: Document.Arrow, icon: "move-up-right", tip: qsTr("Arrow (A) — Shift snaps to 45°") },
        { tool: Document.Rectangle, icon: "square", tip: qsTr("Rectangle (R) — Shift for a square") },
        { tool: Document.Ellipse, icon: "circle", tip: qsTr("Ellipse (O) — Shift for a circle") },
        { tool: Document.Marker, icon: "highlighter", tip: qsTr("Marker (M)") },
        { tool: Document.Text, icon: "type", tip: qsTr("Text (T)") },
        { tool: Document.Pixelate, icon: "grid-2x2", tip: qsTr("Pixelate (X)") },
        { tool: Document.Counter, icon: "hash", tip: qsTr("Numbered marker (N)") },
        { tool: Document.Measure, icon: "ruler", tip: qsTr("Measure pixels (D) — Alt for any angle") },
    ]

    padding: 4
    palette.buttonText: dark ? Theme.panelText : undefined
    palette.windowText: dark ? Theme.panelText : undefined
    palette.text: dark ? Theme.panelText : undefined
    palette.button: dark ? "#3a3a44" : undefined
    palette.window: dark ? "#2a2a32" : undefined
    palette.highlight: Theme.accent

    background: Rectangle {
        color: bar.dark ? Theme.panel : bar.palette.window
        radius: bar.dark ? 8 : 0
        border.color: bar.dark ? "#55ffffff" : "transparent"
        border.width: bar.dark ? 1 : 0

        // Nothing pressed on the bar reaches what is below it.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            onWheel: wheel => wheel.accepted = true
        }
    }

    // The row alone sizes the bar.
    contentWidth: row.implicitWidth
    contentHeight: row.implicitHeight

    ColorDialog {
        id: colorDialog
        selectedColor: bar.color
        options: ColorDialog.DontUseNativeDialog
        onAccepted: bar.colorPicked(selectedColor)
    }

    RowLayout {
        id: row
        spacing: 2

        Repeater {
            model: bar.tools
            IconButton {
                required property var modelData
                iconName: modelData.icon
                tip: modelData.tip
                checkable: true
                checked: bar.tool === modelData.tool
                onClicked: bar.toolPicked(modelData.tool)
            }
        }

        ToolSeparator {}

        Repeater {
            model: Theme.palette
            Rectangle {
                id: swatch
                required property color modelData
                readonly property bool current: Qt.colorEqual(bar.color, modelData)
                width: 18
                height: 18
                radius: 9
                color: modelData
                border.color: current ? Theme.accent : (bar.dark ? "#80ffffff" : "#80000000")
                border.width: current ? 2 : 1
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                // Takes the press, so it does not fall through the bar.
                TapHandler {
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: bar.colorPicked(swatch.modelData)
                }
            }
        }
        IconButton {
            iconName: "palette"
            tip: qsTr("Any color…")
            onClicked: colorDialog.open()
        }

        ToolSeparator {}

        Rectangle {
            Layout.preferredWidth: 20
            Layout.preferredHeight: 20
            color: "transparent"
            Rectangle {
                anchors.centerIn: parent
                width: Math.min(20, bar.size)
                height: width
                radius: width / 2
                color: bar.color
                border.color: bar.dark ? "#80ffffff" : "#80000000"
                border.width: 1
            }
        }
        Slider {
            id: sizeSlider
            Layout.preferredWidth: 90
            from: 1
            to: 20
            stepSize: 1
            value: bar.size
            onMoved: bar.sizePicked(value)
            ToolTip.text: qsTr("Size: %1 px").arg(Math.round(value))
            ToolTip.visible: hovered || pressed
        }

        ToolSeparator {}

        IconButton {
            iconName: "undo-2"
            tip: qsTr("Undo (Ctrl+Z)")
            enabled: bar.document ? bar.document.canUndo : false
            onClicked: bar.document.undo()
        }
        IconButton {
            iconName: "redo-2"
            tip: qsTr("Redo (Ctrl+Shift+Z)")
            enabled: bar.document ? bar.document.canRedo : false
            onClicked: bar.document.redo()
        }

        ToolSeparator {
            visible: actionsRow.children.length > 0
        }

        Row {
            id: actionsRow
            spacing: 2
        }
    }
}
