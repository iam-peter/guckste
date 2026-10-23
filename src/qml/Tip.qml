import QtQuick
import QtQuick.Controls

// A tooltip in a window of its own, so it can go past the edge of the
// window it is in, like the recording panel's, instead of being pushed
// over what it is about. Wayland places such windows only as menus, so
// there it stays in the window. Above its item, or below with `below`.
ToolTip {
    id: tip

    property bool below: false

    popupType: Qt.platform.pluginName !== "wayland" ? Popup.Window : Popup.Item
    y: below ? (parent ? parent.height : 0) + 3 : -implicitHeight - 3
}
