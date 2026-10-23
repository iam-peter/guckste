import QtQuick
import QtQuick.Controls
import Guckste

// A tool button with a Lucide icon, tinted like its text, and a tooltip.
ToolButton {
    id: button

    property string iconName
    property string tip
    // The tooltip under the button instead of over it.
    property bool tipBelow: false

    icon.source: iconName !== "" ? Icons.source(iconName) : ""
    icon.color: palette.buttonText
    icon.width: 16
    icon.height: 16
    display: text === "" ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
    focusPolicy: Qt.NoFocus

    Tip {
        text: button.tip
        visible: button.tip !== "" && button.hovered
        delay: 600
        below: button.tipBelow
    }
}
