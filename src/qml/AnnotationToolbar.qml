pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
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
    // A text's font size in pixels, set instead of the stroke size while
    // `textMode` is on, as with the text tool, or a text selected.
    property real textSize: 20
    property bool textMode: false
    readonly property bool showsTextSize: textMode || selectedKind === Document.Text

    // The selected mark takes what is picked here: color, stroke size, a
    // text's size. Images have none of them. Selecting a mark shows its
    // own in the bar, through the same signals.
    readonly property int selectedMark: {
        if (!document || document.revision < 0)
            return -1
        const index = document.selectedIndex
        return index >= 0 && document.isShown(index) && document.annotationTool(index) !== Document.Image ? index : -1
    }
    readonly property int selectedKind: selectedMark >= 0 ? document.annotationTool(selectedMark) : -1

    function pickColor(picked: color) {
        colorPicked(picked)
        if (selectedMark >= 0)
            document.setAnnotationColor(selectedMark, picked)
    }

    function pickSize(picked: real) {
        sizePicked(picked)
        if (selectedMark >= 0 && selectedKind !== Document.Text)
            document.setAnnotationSize(selectedMark, picked)
    }

    function pickTextSize(picked: real) {
        textSizePicked(picked)
        if (selectedKind === Document.Text)
            document.setAnnotationSize(selectedMark, picked)
    }

    Connections {
        target: bar.document
        function onSelectedIndexChanged() {
            const index = bar.selectedMark
            if (index < 0)
                return
            bar.colorPicked(bar.document.annotationColor(index))
            if (bar.selectedKind === Document.Text)
                bar.textSizePicked(bar.document.annotationSize(index))
            else
                bar.sizePicked(bar.document.annotationSize(index))
        }
    }
    property Document document
    // The overlay's floating look: a dark panel with light icons.
    property bool dark: false
    default property alias actions: actionsRow.data
    // Narrower than its content, the bar moves what does not fit, from the
    // end, into a menu behind a "more" button. For bars across a window;
    // a floating bar sizes itself to fit instead.
    property bool overflow: false
    // The width of everything in one row, overflowed or not.
    property real fullWidth: 0
    // The width of what went behind the more button, in one row.
    property real overflowWidth: 0

    // The last color from the picker that is not in the palette.
    property color customColor: "transparent"
    function noteCustomColor() {
        if (!Theme.palette.some(c => Qt.colorEqual(c, color)))
            customColor = color
    }
    onColorChanged: noteCustomColor()

    component Swatch: Rectangle {
        id: swatch
        property color swatchColor
        readonly property bool current: Qt.colorEqual(bar.color, swatchColor)
        property alias hover: hoverHandler
        width: 18
        height: 18
        radius: 9
        color: swatchColor
        border.color: current ? Theme.accent : (bar.dark ? "#80ffffff" : "#80000000")
        border.width: current ? 2 : 1
        HoverHandler { id: hoverHandler; cursorShape: Qt.PointingHandCursor }
        // Takes the press, so it does not fall through the bar.
        TapHandler {
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: bar.pickColor(swatch.swatchColor)
        }
    }

    signal toolPicked(int tool)
    signal colorPicked(color color)
    signal sizePicked(real size)
    signal textSizePicked(real size)

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

    // Everything in one row sizes the bar.
    contentWidth: fullWidth
    contentHeight: row.implicitHeight

    // In order: the groups of the bar, then each of the window's actions.
    // What fits stays in the row, the rest goes behind the more button.
    property var units: []
    readonly property int unitSpacing: 2

    function relayout() {
        if (units.length === 0)
            return
        let all = 0
        for (let i = 0; i < units.length; ++i)
            all += units[i].implicitWidth + (i > 0 ? unitSpacing : 0)
        fullWidth = all
        let fit = units.length
        if (overflow && all > availableWidth) {
            const room = availableWidth - moreButton.implicitWidth - unitSpacing
            let total = 0
            fit = 0
            for (let i = 0; i < units.length; ++i) {
                const w = units[i].implicitWidth + (i > 0 ? unitSpacing : 0)
                if (total + w > room)
                    break
                total += w
                ++fit
            }
            // The tools always stay.
            fit = Math.max(fit, 1)
        }
        // Parked first, then placed in order, so both keep the bar's order.
        for (const unit of units)
            unit.parent = stash
        moreButton.parent = stash
        for (let i = 0; i < units.length; ++i)
            units[i].parent = i < fit ? row : moreFlow
        moreButton.parent = row
        moreButton.visible = fit < units.length
        let rest = 0
        for (let i = fit; i < units.length; ++i)
            rest += units[i].implicitWidth + (i > fit ? unitSpacing : 0)
        overflowWidth = rest
        if (fit === units.length)
            morePopup.close()
    }

    function relayoutLater() {
        Qt.callLater(relayout)
    }

    onAvailableWidthChanged: if (overflow) relayoutLater()

    Component.onCompleted: {
        noteCustomColor()
        const list = [toolsGroup, colorsGroup, sizeGroup, historyGroup]
        if (actionsRow.children.length > 0)
            list.push(actionsSeparator)
        for (const child of Array.from(actionsRow.children))
            list.push(child)
        units = list
        for (const unit of units)
            unit.implicitWidthChanged.connect(relayoutLater)
        relayout()
    }

    // Where units wait while the row is rebuilt, and where the window's
    // actions are declared.
    Item {
        id: stash
        visible: false
        Row {
            id: actionsRow
        }
        ToolSeparator {
            id: actionsSeparator
        }
    }

    RowLayout {
        id: row
        spacing: bar.unitSpacing
    }

    IconButton {
        id: moreButton
        objectName: "moreButton"
        visible: false
        iconName: "chevrons-right"
        tip: qsTr("More")
        checked: morePopup.opened
        onClicked: morePopup.opened ? morePopup.close() : morePopup.open()

        Popup {
            id: morePopup
            objectName: "morePopup"
            // A press on the button itself is left to it, so it closes the
            // menu instead of closing and opening it again.
            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
            y: moreButton.height + 6
            x: moreButton.width - width
            // As wide as what it holds, in one row if the bar is wide enough.
            width: Math.min(bar.width - 8, bar.overflowWidth + leftPadding + rightPadding)
            padding: 6
            background: Rectangle {
                color: bar.dark ? Theme.panel : bar.palette.window
                radius: 8
                border.color: bar.dark ? "#55ffffff" : "#40000000"
                border.width: 1
            }
            contentItem: Flow {
                id: moreFlow
                spacing: bar.unitSpacing
            }
        }
    }

    RowLayout {
        id: toolsGroup
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
    }

    RowLayout {
        id: colorsGroup
        spacing: 2

        ToolSeparator {}

        Repeater {
            model: Theme.palette
            Swatch {
                required property color modelData
                swatchColor: modelData
            }
        }
        // The color from the picker, until a palette color is taken.
        Swatch {
            swatchColor: bar.customColor
            visible: bar.customColor.a > 0
            ToolTip.text: qsTr("Your color %1").arg(bar.customColor.toString())
            ToolTip.visible: hover.hovered
            ToolTip.delay: 600
        }
        IconButton {
            id: paletteButton
            iconName: "palette"
            tip: qsTr("Any color…")
            checked: colorPicker.opened
            onClicked: {
                if (colorPicker.opened) {
                    colorPicker.close()
                    return
                }
                // Below the button, or above it near the window's bottom.
                const below = paletteButton.mapToItem(null, 0, paletteButton.height).y
                const room = paletteButton.Window.height - below
                colorPicker.y = room >= colorPicker.implicitHeight + 14
                    ? paletteButton.height + 6 : -colorPicker.implicitHeight - 6
                colorPicker.open()
            }
            ColorPicker {
                id: colorPicker
                x: paletteButton.width / 2 - implicitWidth / 2
                current: bar.color
                onPicked: picked => bar.pickColor(picked)
            }
        }
    }

    RowLayout {
        id: sizeGroup
        spacing: 2

        ToolSeparator {}

        Rectangle {
            visible: !bar.showsTextSize
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
        SpinBox {
            id: textSizeBox
            visible: bar.showsTextSize
            Layout.preferredWidth: 110
            from: 4
            to: 400
            editable: true
            value: Math.round(bar.textSize)
            textFromValue: (value, locale) => qsTr("%1 px").arg(value)
            valueFromText: (text, locale) => parseInt(text) || value
            onValueModified: bar.pickTextSize(value)
            ToolTip.text: qsTr("Text size in pixels")
            ToolTip.visible: hovered
            ToolTip.delay: 600
        }
        Slider {
            id: sizeSlider
            visible: !bar.showsTextSize
            Layout.preferredWidth: 90
            from: 1
            to: 20
            stepSize: 1
            value: bar.size
            onMoved: bar.pickSize(value)
            ToolTip.text: qsTr("Size: %1 px").arg(Math.round(value))
            ToolTip.visible: hovered || pressed
        }
    }

    RowLayout {
        id: historyGroup
        spacing: 2

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
    }
}
