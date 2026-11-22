import QtQuick
import Drift

// Crop editor over the picture. Drag state is local; the recipe is written when the drag ends.
Item {
    id: root

    // The picture's painted size inside this item, and its aspect (display space, so a rotated
    // source is already accounted for).
    required property real paintedWidth
    required property real paintedHeight
    property bool editing: true

    readonly property var recipe: EditorState.sourceLab.recipe
    // Width over height of the crop box in real px; 0 when the shape is free.
    readonly property real lockedRatio: root.aspectValue(root.recipe.aspect ?? "free")

    property real cropX: 0
    property real cropY: 0
    property real cropW: 1
    property real cropH: 1

    // The source's px, turned to match how the picture is painted.
    readonly property size sourceSize: {
        const w = EditorState.sourceLab.width
        const h = EditorState.sourceLab.height
        if (w <= 0 || h <= 0)
            return Qt.size(root.paintedWidth, root.paintedHeight)
        return (w > h) === (root.paintedWidth > root.paintedHeight) ? Qt.size(w, h) : Qt.size(h, w)
    }

    function aspectValue(text) {
        if (text === "free")
            return 0
        const parts = text.split(":")
        const a = parseFloat(parts[0])
        const b = parts.length > 1 ? parseFloat(parts[1]) : 1
        return a > 0 && b > 0 ? a / b : 0
    }

    function loadFromRecipe() {
        const c = root.recipe.crop
        if (!c)
            return
        root.cropX = c.x
        root.cropY = c.y
        root.cropW = c.width
        root.cropH = c.height
    }

    function commitCrop() {
        EditorState.sourceLab.setRecipeValue("crop", Qt.rect(root.cropX, root.cropY, root.cropW, root.cropH))
    }

    onRecipeChanged: loadFromRecipe()
    Component.onCompleted: loadFromRecipe()

    CropFrame {
        x: (root.width - root.paintedWidth) / 2
        y: (root.height - root.paintedHeight) / 2
        width: root.paintedWidth
        height: root.paintedHeight
        spaceSize: Qt.size(1, 1)
        contentSize: root.sourceSize
        crop: Qt.rect(root.cropX, root.cropY, root.cropW, root.cropH)
        aspect: root.lockedRatio
        minSize: 0.08
        editable: root.editing
        undoText: qsTr("Crop")
        onEdited: (c) => {
            root.cropX = c.x
            root.cropY = c.y
            root.cropW = c.width
            root.cropH = c.height
        }
        onCommitted: root.commitCrop()
    }
}
