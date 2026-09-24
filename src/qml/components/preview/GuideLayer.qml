import QtQuick
import Drift

// Draws the active guide sets over the canvas. Positions arrive as fractions
// of the canvas, so this only has to fill the canvas rect.
Item {
    id: root

    visible: EditorState.guidesEnabled

    Repeater {
        model: EditorState.guideItems

        Rectangle {
            required property var modelData
            readonly property string kind: modelData.kind
            readonly property bool outline: kind === "rect" || kind === "aspect"
            // Largest frame of the item's aspect that fits the canvas, centred.
            readonly property real frameW: Math.min(root.width, root.height * modelData.aspect)
            readonly property real frameH: Math.min(root.height, root.width / modelData.aspect)

            x: kind === "v" ? root.width * modelData.pos
               : kind === "rect" ? root.width * modelData.left
               : kind === "aspect" ? (root.width - frameW) / 2 : 0
            y: kind === "h" ? root.height * modelData.pos
               : kind === "rect" ? root.height * modelData.top
               : kind === "aspect" ? (root.height - frameH) / 2 : 0
            width: kind === "v" ? 1
                   : kind === "rect" ? root.width * (1 - modelData.left - modelData.right)
                   : kind === "aspect" ? frameW : root.width
            height: kind === "h" ? 1
                    : kind === "rect" ? root.height * (1 - modelData.top - modelData.bottom)
                    : kind === "aspect" ? frameH : root.height
            color: outline ? "transparent" : modelData.color
            border.width: outline ? 1 : 0
            border.color: modelData.color
            opacity: modelData.opacity
        }
    }
}
