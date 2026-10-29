import QtQuick
import Drift

SourceLabSection {
    id: root

    readonly property var lab: EditorState.sourceLab
    readonly property var recipe: root.lab.recipe
    readonly property var crop: root.recipe.crop ?? Qt.rect(0, 0, 1, 1)
    readonly property bool rotated: {
        const index = AssetLibrary.indexOfId(root.lab.currentAssetId)
        return index >= 0 && AssetLibrary.assetAt(index).effectiveRotation % 180 === 90
    }
    // The picture as the viewer shows it, so a rotated source is measured upright.
    readonly property real sourceW: root.rotated ? root.lab.height : root.lab.width
    readonly property real sourceH: root.rotated ? root.lab.width : root.lab.height

    title: qsTr("Crop / Reframe")
    dirty: (root.recipe.aspect ?? "free") !== "free" || root.crop.x !== 0 || root.crop.y !== 0
           || root.crop.width !== 1 || root.crop.height !== 1
    onResetRequested: root.lab.resetRecipe("crop")
    SearchAnchor {
        searchId: "sourcelab-crop"
        title: qsTr("Crop / reframe source")
        keywords: "crop reframe reframing aspect ratio 9:16 portrait presets"
    }

    function setPixels(x, y, w, h) {
        const sw = Math.max(1, root.sourceW)
        const sh = Math.max(1, root.sourceH)
        const nw = Math.max(2 / sw, Math.min(1, w / sw))
        const nh = Math.max(2 / sh, Math.min(1, h / sh))
        const nx = Math.max(0, Math.min(1 - nw, x / sw))
        const ny = Math.max(0, Math.min(1 - nh, y / sh))
        root.lab.setRecipeValue("aspect", "free")
        root.lab.setRecipeValue("crop", Qt.rect(nx, ny, nw, nh))
    }

    Flow {
        width: parent.width
        spacing: Theme.spacingSm

        Repeater {
            model: [
                { id: "free", label: qsTr("Free") },
                { id: "16:9", label: "16:9" },
                { id: "9:16", label: "9:16" },
                { id: "1:1", label: "1:1" },
                { id: "4:5", label: "4:5" },
                { id: "2.39:1", label: "2.39" }
            ]
            ThemedChip {
                required property var modelData
                text: modelData.label
                selected: (root.recipe.aspect ?? "free") === modelData.id
                onClicked: root.lab.setRecipeValue("aspect", modelData.id)
            }
        }
    }

    Grid {
        width: parent.width
        columns: 2
        columnSpacing: Theme.spacingMd
        rowSpacing: Theme.spacingSm

        Repeater {
            model: [
                { key: "x", label: qsTr("X") },
                { key: "y", label: qsTr("Y") },
                { key: "w", label: qsTr("W") },
                { key: "h", label: qsTr("H") }
            ]
            Row {
                id: cell
                required property var modelData
                width: (root.width - Theme.spacingMd) / 2
                spacing: Theme.spacingSm

                ThemedLabel {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.spacingXl * 1.5
                    text: cell.modelData.label
                }
                ThemedNumberField {
                    id: field
                    width: parent.width - Theme.spacingXl * 1.5 - parent.spacing
                    unit: "px"
                    from: 0
                    to: Math.max(1, cell.modelData.key === "x" || cell.modelData.key === "w"
                                 ? root.sourceW : root.sourceH)
                    readonly property real current: {
                        const c = root.crop
                        switch (cell.modelData.key) {
                        case "x": return c.x * root.sourceW
                        case "y": return c.y * root.sourceH
                        case "w": return c.width * root.sourceW
                        }
                        return c.height * root.sourceH
                    }
                    Binding on value {
                        when: !field.activeFocus
                        value: Math.round(field.current)
                    }
                    onEdited: v => {
                        const c = root.crop
                        const px = [c.x * root.sourceW, c.y * root.sourceH,
                                    c.width * root.sourceW, c.height * root.sourceH]
                        px["xywh".indexOf(cell.modelData.key)] = v
                        root.setPixels(px[0], px[1], px[2], px[3])
                    }
                }
            }
        }
    }

    ThemedButton {
        variant: "ghost"
        text: qsTr("Reset crop")
        enabled: root.dirty
        onClicked: root.lab.resetRecipe("crop")
    }
}
