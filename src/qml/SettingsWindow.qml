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
            text: qsTr("Include the pointer in screenshots and recordings")
            checked: Preferences.includeCursor
            onToggled: Preferences.includeCursor = checked
        }
        CheckBox {
            visible: Preferences.showKeysSupported
            text: qsTr("Show the keys pressed along the bottom of recordings")
            checked: Preferences.showKeys
            onToggled: Preferences.showKeys = checked
        }
        CheckBox {
            visible: Preferences.showKeysSupported
            text: qsTr("Keep lossless stills before and after each click while recording")
            checked: Preferences.stillsOnClicks
            onToggled: Preferences.stillsOnClicks = checked
        }
        CheckBox {
            text: qsTr("Keep lossless stills of what the screen settles on after it changes")
            checked: Preferences.stillsOnChanges
            onToggled: Preferences.stillsOnChanges = checked
        }
        CheckBox {
            text: qsTr("Keep only the stills of a recording, without the video")
            checked: Preferences.stillsOnly
            onToggled: Preferences.stillsOnly = checked
        }
        Label {
            visible: Preferences.showKeysSupported && Qt.platform.os === "osx"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: palette.placeholderText
            font.pixelSize: 12
            text: qsTr("macOS asks for the Accessibility permission for these: System Settings → Privacy & Security.")
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
        RowLayout {
            spacing: 8
            Label { text: qsTr("Recording frame rate") }
            ComboBox {
                id: frameRateBox
                model: [15, 24, 30, 60]
                currentIndex: Math.max(0, model.indexOf(Preferences.frameRate))
                onActivated: Preferences.frameRate = model[currentIndex]
            }
            Label { text: qsTr("fps") }
        }
        RowLayout {
            spacing: 8
            Label { text: qsTr("Recording quality") }
            ComboBox {
                model: [qsTr("Low"), qsTr("Medium"), qsTr("High"), qsTr("Very high")]
                currentIndex: Preferences.recordingQuality
                onActivated: Preferences.recordingQuality = currentIndex
            }
            Label {
                text: qsTr("Very high is for crisp text; the file grows with it.")
                color: palette.placeholderText
                font.pixelSize: 12
            }
        }
        CheckBox {
            text: qsTr("Open a recording in the editor to draw on it and trim it before saving")
            checked: Preferences.editAfterRecording
            onToggled: Preferences.editAfterRecording = checked
        }
        CheckBox {
            text: qsTr("Record the microphone with the screen")
            checked: Preferences.recordMicrophone
            onToggled: Preferences.recordMicrophone = checked
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
            text: qsTr("Bind keys to “guckste gui” and “guckste record” in your desktop's keyboard settings "
                       + "to take screenshots and start or stop recordings with them. “guckste full” "
                       + "captures all screens without asking.")
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
