pragma Singleton

import QtQuick
import Guckste.Core

// Lucide icons (https://lucide.dev, ISC license), drawn in white so they
// can be tinted to any color.
QtObject {
    function source(name: string): url {
        return Qt.resolvedUrl(`../icons/${name}.svg`)
    }

    // The icon of a kind of mark, a Document.Tool.
    function forTool(tool: int): string {
        switch (tool) {
        case Document.Pen: return "pencil"
        case Document.Line: return "slash"
        case Document.Arrow: return "move-up-right"
        case Document.Rectangle: return "square"
        case Document.Ellipse: return "circle"
        case Document.Marker: return "highlighter"
        case Document.Text: return "type"
        case Document.Pixelate: return "grid-2x2"
        case Document.Counter: return "hash"
        case Document.Measure: return "ruler"
        case Document.Image: return "image"
        case Document.Video: return "film"
        default: return "square"
        }
    }
}
