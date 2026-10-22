pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Guckste
import Guckste.Core

// Every layer of a document, topmost first, the screenshot it opened with
// at the bottom: pick one to select it, lock or unlock it, raise, lower
// or remove it. Double-clicking a text edits it. A locked layer stays put
// on the canvas, and clicks there go to what is below.
Pane {
    id: panel

    required property Document document
    // A layer was picked: the window switches to the select tool.
    signal picked(int index)
    // A text layer was double-clicked.
    signal editRequested(int index)

    padding: 0


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
                required property bool locked

                readonly property bool current: !isBase && layerIndex === panel.document.selectedIndex
                readonly property int count: panel.document.annotationCount
                // Shown on the row under the pointer and the selected one.
                // They always take their room, so nothing moves when they
                // show.
                readonly property bool showButtons: hovered || current

                width: ListView.view.width
                height: 32
                topPadding: 5
                bottomPadding: 5
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
                        name: row.isBase ? "image" : Icons.forTool(row.kind)
                        color: row.palette.text
                        opacity: 0.8
                    }
                    Rectangle {
                        visible: !row.isBase
                        opacity: row.kind !== Document.Image && row.kind !== Document.Video ? 1 : 0
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
                        opacity: row.locked ? 0.7 : 1
                    }
                    // Always shown while locked, so a locked layer says so.
                    IconButton {
                        objectName: "lockButton"
                        visible: !row.isBase
                        opacity: row.locked || row.showButtons ? 1 : 0
                        iconName: row.locked ? "lock" : "lock-open"
                        tip: row.locked ? qsTr("Unlock, to move, resize or remove it") : qsTr("Lock in place")
                        enabled: row.locked || row.showButtons
                        implicitWidth: 22
                        implicitHeight: 22
                        padding: 3
                        onClicked: panel.document.setLocked(row.layerIndex, !row.locked)
                    }
                    // Up the list is up the stack.
                    IconButton {
                        visible: !row.isBase
                        opacity: row.showButtons ? 1 : 0
                        iconName: "chevron-up"
                        tip: qsTr("Raise")
                        enabled: row.showButtons && row.layerIndex < row.count - 1
                        implicitWidth: 22
                        implicitHeight: 22
                        padding: 3
                        onClicked: panel.document.moveLayer(row.layerIndex, row.layerIndex + 1)
                    }
                    IconButton {
                        visible: !row.isBase
                        opacity: row.showButtons ? 1 : 0
                        iconName: "chevron-down"
                        tip: qsTr("Lower")
                        enabled: row.showButtons && row.layerIndex > 0
                        implicitWidth: 22
                        implicitHeight: 22
                        padding: 3
                        onClicked: panel.document.moveLayer(row.layerIndex, row.layerIndex - 1)
                    }
                    IconButton {
                        visible: !row.isBase
                        opacity: row.showButtons ? 1 : 0
                        iconName: "trash-2"
                        tip: row.locked ? qsTr("Unlock it to remove it") : qsTr("Remove (Delete)")
                        enabled: row.showButtons && !row.locked
                        implicitWidth: 22
                        implicitHeight: 22
                        padding: 3
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
            text: qsTr("Marks, images and videos show up here.")
        }
    }
}
