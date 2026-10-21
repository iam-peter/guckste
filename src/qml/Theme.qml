pragma Singleton

import QtQuick

// Colors of the capture overlay and the annotation palette.
QtObject {
    readonly property color accent: "#ff8a3d"
    readonly property color panel: "#f0202026"
    readonly property color panelText: "#f2f2f2"
    readonly property color dim: "#66000000"
    readonly property color selectionBorder: "#ffffff"
    readonly property list<color> palette: [
        "#ff3b30", "#ff9500", "#ffcc00", "#34c759", "#00c7be",
        "#007aff", "#af52de", "#ff2d55", "#ffffff", "#000000"
    ]
}
