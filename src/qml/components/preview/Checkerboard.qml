import QtQuick
import Drift

// Lives *behind* PreviewItem. Drawing the chequers into the composited frame
// would bake them into export; this is preview chrome only.
Item {
    id: root
    visible: EditorState.background && EditorState.background.kind === "transparent"
    clip: true

    readonly property int cell: 8
    readonly property color light: "#c8c8c8"
    readonly property color dark: "#8c8c8c"

    Canvas {
        id: canvas
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")
            const s = root.cell
            for (let y = 0; y < height; y += s) {
                for (let x = 0; x < width; x += s) {
                    ctx.fillStyle = ((Math.floor(x / s) + Math.floor(y / s)) % 2 === 0)
                                    ? root.light : root.dark
                    ctx.fillRect(x, y, s, s)
                }
            }
        }
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
    }
}
