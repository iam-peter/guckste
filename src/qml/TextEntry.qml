import QtQuick
import QtQuick.Controls
import Guckste

// Where a Text annotation is typed: an editor at the clicked point, in the
// font the annotation will be drawn with. Enter accepts, Shift+Enter breaks
// the line, Escape drops it. Accepting an empty text is left to whoever
// listens: nothing for a new text, removing it for an edited one.
Item {
    id: entry

    property real pixelSize: 16
    property color color: "red"
    // The image point the text goes to, kept for whoever accepts it.
    property point imagePoint

    signal accepted(string text)
    signal cancelled()

    visible: false
    width: Math.max(80, area.contentWidth + 16)
    height: area.contentHeight + 8

    function openAt(viewX: real, viewY: real, imageX: real, imageY: real, initialText: string) {
        imagePoint = Qt.point(imageX, imageY)
        x = viewX - 4
        y = viewY - 4
        area.text = initialText
        visible = true
        area.forceActiveFocus()
        area.cursorPosition = area.length
    }

    function commit() {
        if (!visible)
            return
        const text = area.text
        visible = false
        area.text = ""
        accepted(text.trim() === "" ? "" : text)
    }

    function cancel() {
        if (!visible)
            return
        visible = false
        area.text = ""
        cancelled()
    }

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.color: Theme.accent
        border.width: 1
    }

    TextArea {
        id: area
        x: 4
        y: 4
        width: Math.max(72, contentWidth + 8)
        // All of it, or the style's extra left padding moves the text
        // off where it is drawn once accepted.
        padding: 0
        leftPadding: 0
        rightPadding: 0
        topPadding: 0
        bottomPadding: 0
        font.pixelSize: entry.pixelSize
        color: entry.color
        placeholderText: qsTr("Text")
        placeholderTextColor: Qt.rgba(entry.color.r, entry.color.g, entry.color.b, 0.5)
        wrapMode: TextEdit.NoWrap
        background: null
        selectByMouse: true
        Keys.onPressed: event => {
            if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                    && !(event.modifiers & Qt.ShiftModifier)) {
                entry.commit()
                event.accepted = true
            } else if (event.key === Qt.Key_Escape) {
                entry.cancel()
                event.accepted = true
            }
        }
        onActiveFocusChanged: if (!activeFocus) entry.commit()
    }
}
