import QtQuick
import Drift
import ".."

// Paint editor for a fill or stroke layer: solid colour, gradient, image texture or a shader
// effect. Every write goes through setTextLayer with a partial paint patch.
Column {
    id: root

    property string layerId: ""
    property var paint: ({})
    property var textStyle: ({})

    readonly property string kind: (paint && paint.kind) || "solid"
    readonly property var effects: EditorState.textPaintEffects()
    readonly property var effect: (paint && paint.effect) || ({})
    readonly property var texture: (paint && paint.texture) || ({})
    readonly property var effectSpec: {
        for (let i = 0; i < root.effects.length; ++i) {
            if (root.effects[i].id === root.effect.id)
                return root.effects[i]
        }
        return root.effects.length > 0 ? root.effects[0] : null
    }
    // The stored effect params are typed ({id: {type, value}}); the slot editor takes plain values.
    readonly property var effectValues: {
        const out = {}
        const params = root.effect.params || {}
        for (const key in params) {
            const entry = params[key]
            out[key] = entry && entry.value !== undefined ? entry.value : entry
        }
        return out
    }

    spacing: Theme.spacingMd

    function setPaint(patch) {
        EditorState.setTextLayer(EditorState.selectedTrack, EditorState.selectedClip, root.layerId,
                                 { "paint": patch })
    }
    function previewPaint(patch) {
        EditorState.previewSetTextLayer(EditorState.selectedTrack, EditorState.selectedClip, root.layerId,
                                        { "paint": patch })
    }
    function effectParamPatch(id, value) {
        let type = "scalar"
        const specs = root.effectSpec ? root.effectSpec.params : []
        for (let i = 0; i < specs.length; ++i) {
            if (specs[i].id === id)
                type = specs[i].type
        }
        const params = {}
        params[id] = { "type": type, "value": value }
        return { "effect": { "id": root.effectSpec ? root.effectSpec.id : root.effect.id, "params": params } }
    }
    function chooseTexture() {
        const url = FileDialogs.openFile(qsTr("Texture Image"),
                                         [qsTr("Images (*.png *.jpg *.jpeg *.webp)")])
        if (url != "")
            root.setPaint({ "texture": { "path": url.toString().replace(/^file:\/\//, "") } })
    }

    Row {
        width: parent.width
        spacing: 8

        Column {
            width: (parent.width - parent.spacing) / 2
            spacing: 4
            Text {
                text: qsTr("Paint")
                color: Theme.mutedForeground
                font.pixelSize: Theme.fontSizeXs
                font.family: Theme.fontFamily
            }
            ThemedComboBox {
                width: parent.width
                readonly property var kinds: ["solid", "gradient", "texture", "effect"]
                model: [qsTr("Solid"), qsTr("Gradient"), qsTr("Texture"), qsTr("Effect")]
                currentIndex: Math.max(0, kinds.indexOf(root.kind))
                onActivated: root.setPaint({ "kind": kinds[currentIndex] })
            }
        }

        Column {
            width: (parent.width - parent.spacing) / 2
            spacing: 4
            visible: root.kind === "solid"
            Text {
                text: qsTr("Colour")
                color: Theme.mutedForeground
                font.pixelSize: Theme.fontSizeXs
                font.family: Theme.fontFamily
            }
            ColorSwatchField {
                hex: (root.paint && root.paint.color) || "#ffffffff"
                tooltip: qsTr("Choose the paint colour")
                onEdited: value => root.setPaint({ "color": value })
            }
        }
    }

    GradientStopEditor {
        width: parent.width
        visible: root.kind === "gradient"
        layerId: root.layerId
        gradient: (root.paint && root.paint.gradient) || ({})
        textStyle: root.textStyle
    }

    Column {
        width: parent.width
        spacing: Theme.spacingMd
        visible: root.kind === "texture"

        Row {
            width: parent.width
            spacing: 8
            ThemedButton {
                glyph: Theme.icons.image
                text: root.texture.path ? qsTr("Change image…") : qsTr("Choose image…")
                onClicked: root.chooseTexture()
            }
            Text {
                width: parent.width - parent.spacing - 130
                anchors.verticalCenter: parent.verticalCenter
                text: root.texture.path ? String(root.texture.path).split("/").pop() : qsTr("No image")
                elide: Text.ElideMiddle
                color: Theme.mutedForeground
                font.pixelSize: Theme.fontSizeXs
                font.family: Theme.fontFamily
            }
        }

        Row {
            width: parent.width
            spacing: 8

            Column {
                width: (parent.width - parent.spacing) / 2
                spacing: 4
                Text {
                    text: qsTr("Fit")
                    color: Theme.mutedForeground
                    font.pixelSize: Theme.fontSizeXs
                    font.family: Theme.fontFamily
                }
                Row {
                    spacing: Theme.spacingSm
                    ThemedToggleButton {
                        text: qsTr("Tile")
                        checked: root.texture.tile === true
                        tooltip: qsTr("Repeat the image across the text")
                        onClicked: root.setPaint({ "texture": { "tile": true } })
                    }
                    ThemedToggleButton {
                        text: qsTr("Cover")
                        checked: root.texture.tile !== true
                        tooltip: qsTr("Stretch one copy of the image over the text")
                        onClicked: root.setPaint({ "texture": { "tile": false } })
                    }
                }
            }

            Column {
                width: (parent.width - parent.spacing) / 2
                spacing: 4
                Text {
                    text: qsTr("Scale")
                    color: Theme.mutedForeground
                    font.pixelSize: Theme.fontSizeXs
                    font.family: Theme.fontFamily
                }
                ThemedNumberField {
                    id: textureScaleField
                    width: parent.width
                    decimals: 2
                    step: 0.1
                    from: 0.01
                    to: 100
                    unit: "x"
                    Binding on value {
                        when: !textureScaleField.activeFocus
                        value: root.texture.scale !== undefined ? Number(root.texture.scale) : 1
                    }
                    onEdited: v => root.setPaint({ "texture": { "scale": v } })
                }
            }
        }

        Column {
            width: (parent.width - 8) / 2
            spacing: 4
            Text {
                text: qsTr("Angle")
                color: Theme.mutedForeground
                font.pixelSize: Theme.fontSizeXs
                font.family: Theme.fontFamily
            }
            ThemedNumberField {
                id: textureAngleField
                width: parent.width
                decimals: 0
                step: 5
                from: -360
                to: 360
                unit: "°"
                Binding on value {
                    when: !textureAngleField.activeFocus
                    value: Number(root.texture.angle) || 0
                }
                onEdited: v => root.setPaint({ "texture": { "angle": v } })
            }
        }
    }

    Column {
        width: parent.width
        spacing: Theme.spacingMd
        visible: root.kind === "effect"

        Column {
            width: parent.width
            spacing: 4
            Text {
                text: qsTr("Effect")
                color: Theme.mutedForeground
                font.pixelSize: Theme.fontSizeXs
                font.family: Theme.fontFamily
            }
            ThemedComboBox {
                width: parent.width
                model: root.effects.map(e => e.label)
                currentIndex: {
                    for (let i = 0; i < root.effects.length; ++i) {
                        if (root.effects[i].id === root.effect.id)
                            return i
                    }
                    return 0
                }
                onActivated: root.setPaint({ "effect": { "id": root.effects[currentIndex].id, "params": ({}) } })
            }
        }

        TextParamSlots {
            width: parent.width
            specs: root.effectSpec ? root.effectSpec.params : []
            values: root.effectValues
            onChanged: (id, value) => root.setPaint(root.effectParamPatch(id, value))
            onPreviewChanged: (id, value) => root.previewPaint(root.effectParamPatch(id, value))
            onDragStarted: EditorState.beginPreviewDrag(qsTr("Adjust paint effect"))
            onDragEnded: EditorState.commitPreviewDrag()
        }
    }
}
