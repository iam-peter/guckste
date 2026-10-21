import QtQuick
import QtQuick.Controls
import Guckste

// A tool button with a Lucide icon, tinted like its text, and a tooltip.
ToolButton {
    id: button

    property string iconName
    property string tip

    icon.source: iconName !== "" ? Icons.source(iconName) : ""
    icon.color: palette.buttonText
    icon.width: 16
    icon.height: 16
    display: text === "" ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
    focusPolicy: Qt.NoFocus
    ToolTip.text: tip
    ToolTip.visible: tip !== "" && hovered
    ToolTip.delay: 600
}
