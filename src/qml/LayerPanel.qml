pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Guckste
import Guckste.Core

// Every mark and added image of a document, topmost first, with the
// screenshot at the bottom: pick one to select it, raise, lower or remove
// it. Double-clicking a text edits it.
Pane {
    id: panel

    required property Document document
    // A layer was picked: the window switches to the select tool.
    signal picked(int index)
    // A text layer was double-clicked.
    signal editRequested(int index)

    padding: 0

    readonly property var icons: ({
        [Document.Pen]: "pencil",
        [Document.Line]: "slash",
        [Document.Arrow]: "move-up-right",
        [Document.Rectangle]: "square",
        [Document.Ellipse]: "circle",
        [Document.Marker]: "highlighter",
        [Document.Text]: "type",
        [Document.Pixelate]: "grid-2x2",
        [Document.Counter]: "hash",
        [Document.Measure]: "ruler",
        [Document.Image]: "image",
    })

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Label {
            Layout.fillWidth: true
            Layout.margins: 8
            text: qsTr("Layers")
            font.bold: true
        }

        ListView {
            id: list
            objectName: "layerList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            model: LayerModel { document: panel.document }

            delegate: ItemDelegate {
                id: row

                required property int index
                required property int layerIndex
                required property int kind
                required property string label
                required property color layerColor
                required property bool isBase

                readonly property bool current: !isBase && layerIndex === panel.document.selectedIndex
                readonly property int count: panel.document.annotationCount

                width: ListView.view.width
                height: 32
                highlighted: current
                hoverEnabled: true
                focusPolicy: Qt.NoFocus
                onClicked: {
                    if (isBase)
                        return
                    panel.document.selectedIndex = layerIndex
                    panel.picked(layerIndex)
                }
                onDoubleClicked: if (kind === Document.Text) panel.editRequested(layerIndex)

                contentItem: RowLayout {
                    spacing: 6
                    Icon {
                        name: row.isBase ? "lock" : panel.icons[row.kind] ?? "square"
                        color: row.palette.text
                        opacity: 0.8
                    }
                    Rectangle {
                        visible: !row.isBase && row.kind !== Document.Image
                        implicitWidth: 10
                        implicitHeight: 10
                        radius: 5
                        color: row.layerColor
                        border.color: "#80808080"
                    }
                    Label {
                        Layout.fillWidth: true
                        text: row.label
                        elide: Text.ElideRight
                        font.pixelSize: 12
                        opacity: row.isBase ? 0.7 : 1
                    }
                    // Up the list is up the stack.
                    IconButton {
                        visible: !row.isBase && (row.hovered || row.current)
                        iconName: "chevron-up"
                        tip: qsTr("Raise")
                        enabled: row.layerIndex < row.count - 1
                        implicitWidth: 24
                        implicitHeight: 24
                        onClicked: panel.document.moveLayer(row.layerIndex, row.layerIndex + 1)
                    }
                    IconButton {
                        visible: !row.isBase && (row.hovered || row.current)
                        iconName: "chevron-down"
                        tip: qsTr("Lower")
                        enabled: row.layerIndex > 0
                        implicitWidth: 24
                        implicitHeight: 24
                        onClicked: panel.document.moveLayer(row.layerIndex, row.layerIndex - 1)
                    }
                    IconButton {
                        visible: !row.isBase && (row.hovered || row.current)
                        iconName: "trash-2"
                        tip: qsTr("Remove (Delete)")
                        implicitWidth: 24
                        implicitHeight: 24
                        onClicked: panel.document.removeAnnotation(row.layerIndex)
                    }
                }
            }
        }

        Label {
            Layout.fillWidth: true
            Layout.margins: 8
            visible: panel.document.annotationCount === 0
            wrapMode: Text.WordWrap
            font.pixelSize: 12
            opacity: 0.7
            text: qsTr("Marks and added images show up here.")
        }
    }
}
