import QtQuick
import QtQuick.Controls
import Guckste

// A Lucide icon on its own, tinted to `color`. Drawn by a disabled button,
// so it never takes clicks and works with every renderer.
ToolButton {
    id: icon

    required property string name
    property color color: Window.window ? Window.window.palette.text : "black"
    property int size: 16

    enabled: false
    focusPolicy: Qt.NoFocus
    padding: 0
    background: null
    display: AbstractButton.IconOnly
    icon.source: Icons.source(name)
    icon.color: color
    icon.width: size
    icon.height: size
    implicitWidth: size
    implicitHeight: size
}
