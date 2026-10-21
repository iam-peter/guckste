import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Guckste
import Guckste.Core

// Where screenshots go and how Guckste starts.
Window {
    id: root

    width: 560
    height: layout.implicitHeight + 40
    minimumWidth: 420
    minimumHeight: layout.implicitHeight + 40
    title: qsTr("Guckste Settings")
    color: palette.window

    FolderDialog {
        id: folderDialog
        currentFolder: "file://" + Preferences.saveDirectory
        onAccepted: Preferences.saveDirectory = selectedFolder.toString().replace(/^file:\/\//, "")
    }

    ColumnLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12

        GridLayout {
            columns: 3
            columnSpacing: 8
            rowSpacing: 8
            Layout.fillWidth: true

            Label { text: qsTr("Save to") }
            TextField {
                id: directoryField
                Layout.fillWidth: true
                text: Preferences.saveDirectory
                onEditingFinished: Preferences.saveDirectory = text
            }
            Button {
                text: qsTr("Browse…")
                onClicked: folderDialog.open()
            }

            Label { text: qsTr("File name") }
            TextField {
                id: patternField
                Layout.fillWidth: true
                text: Preferences.filenamePattern
                onEditingFinished: Preferences.filenamePattern = text
            }
            Label {
                text: Preferences.previewFileName(patternField.text)
                color: palette.placeholderText
                elide: Text.ElideMiddle
                Layout.maximumWidth: 180
            }
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: palette.placeholderText
            font.pixelSize: 12
            text: qsTr("%Y year, %m month, %d day, %H hour, %M minute, %S second, %b month name, %a weekday. "
                       + "A number is appended when the name is taken.")
        }

        CheckBox {
            text: qsTr("Copy the file path to the clipboard after saving")
            checked: Preferences.copyPathAfterSave
            onToggled: Preferences.copyPathAfterSave = checked
        }
        CheckBox {
            text: qsTr("Show a pixel grid in the editor from 800 % zoom")
            checked: Preferences.showGrid
            onToggled: Preferences.showGrid = checked
        }
        CheckBox {
            visible: Preferences.launchAtLoginSupported
            text: qsTr("Launch Guckste in the tray at login")
            checked: Preferences.launchAtLogin
            onToggled: Preferences.launchAtLogin = checked
        }

        Label {
            Layout.fillWidth: true
            Layout.topMargin: 8
            wrapMode: Text.WordWrap
            color: palette.placeholderText
            font.pixelSize: 12
            text: qsTr("Bind a key to “guckste gui” in your desktop's keyboard settings to take screenshots "
                       + "with it, like Print. “guckste full” captures all screens without asking.")
        }

        RowLayout {
            Layout.topMargin: 8
            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("Close")
                onClicked: root.close()
            }
        }
    }

    Shortcut { sequences: ["Escape", "Ctrl+W"]; onActivated: root.close() }
}
