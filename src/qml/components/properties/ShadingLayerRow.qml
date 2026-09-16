import QtQuick
import Drift
import ".."

// One row of a text or shape clip's shading stack. The list is shown front-most first, so
// `position` counts from the front while the stored order (what moveStyleLayer takes) counts
// from the back.
Column {
    id: row

    property var layerData: ({})
    // The style map the layer belongs to; its `keyframes` feed the diamonds.
    property var styleData: ({})
    // "text" or "shape": the keyframe prop prefix the clip's style answers to.
    property string keyPrefix: "text"
    property int position: 0
    property int count: 0
    // Owned by the inspector, keyed by layer id, so it survives reorders and delegate rebuilds.
    property bool expanded: false

    signal toggleRequested()

    readonly property string layerId: (layerData && layerData.id) || ""
    readonly property int storedIndex: count - 1 - position
    readonly property string kind: (layerData && layerData.kind) || "fill"
    readonly property var paint: (layerData && layerData.paint) || ({})
    readonly property string paintKind: paint.kind || "solid"
    readonly property bool layerEnabled: !!layerData && layerData.enabled !== false
    readonly property var kindLabels: ({ "fill": qsTr("Fill"), "stroke": qsTr("Stroke"), "shadow": qsTr("Shadow"),
                                         "glow": qsTr("Glow"), "extrude": qsTr("Extrude") })
    readonly property var paintLabels: ({ "solid": qsTr("Solid"), "gradient": qsTr("Gradient"),
                                          "texture": qsTr("Texture"), "effect": qsTr("Effect") })
    readonly property var kindGlyphs: ({ "fill": Theme.icons.type, "stroke": Theme.icons.shapes,
                                         "shadow": Theme.icons.moon, "glow": Theme.icons.sun,
                                         "extrude": Theme.icons.layers })
    readonly property var blendModes: ["normal", "multiply", "screen", "overlay", "add", "darken", "lighten"]
    readonly property var blendLabels: [qsTr("Normal"), qsTr("Multiply"), qsTr("Screen"), qsTr("Overlay"),
                                        qsTr("Add"), qsTr("Darken"), qsTr("Lighten")]
    readonly property var strokeAligns: ["center", "outside", "inside"]
    readonly property var strokeAlignLabels: [qsTr("Centre"), qsTr("Outside"), qsTr("Inside")]
    readonly property var dashes: ["solid", "dash", "dot", "dashdot"]
    readonly property var dashLabels: [qsTr("Solid"), qsTr("Dashed"), qsTr("Dotted"), qsTr("Dash-dot")]
    readonly property bool sketchy: Number(row.layerData.sketchLength) > 0 || Number(row.layerData.sketchDeviation) > 0
    property bool sketchExpanded: false

    width: parent ? parent.width : 200
    spacing: 6

    function setLayer(patch) {
        EditorState.setStyleLayer(EditorState.selectedTrack, EditorState.selectedClip, row.layerId, patch)
    }
    function keyframes(field) {
        const keys = row.styleData && row.styleData.keyframes
        const entry = keys && keys["layer." + row.layerId + "." + field]
        return (entry && entry.points) || []
    }
    function prop(field, label, decimals) {
        return { "key": row.keyPrefix + ".layer." + row.layerId + "." + field, "label": label,
                 "def": Number(row.layerData[field]) || 0, "decimals": decimals }
    }

    Rectangle {
        width: parent.width
        height: Math.max(Theme.controlHeightSm, header.implicitHeight + 8)
        radius: Theme.radiusSm
        color: Theme.panelAccent

        Row {
            id: header
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 2
            anchors.rightMargin: 4
            spacing: 2

            IconButton {
                glyph: row.expanded ? Theme.icons.chevronDown : Theme.icons.chevronRight
                variant: "ghost"
                buttonSize: 22
                iconSize: 12
                anchors.verticalCenter: parent.verticalCenter
                tooltip: row.expanded ? qsTr("Collapse layer") : qsTr("Expand layer")
                onClicked: row.toggleRequested()
            }
            IconGlyph {
                glyph: row.kindGlyphs[row.kind] || Theme.icons.layers
                iconSize: 12
                iconColor: row.layerEnabled ? Theme.panelForeground : Theme.mutedForeground
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: (row.kindLabels[row.kind] || row.kind) + " · " + (row.paintLabels[row.paintKind] || row.paintKind)
                color: row.layerEnabled ? Theme.panelForeground : Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                font.weight: Font.Medium
                width: Math.max(20, parent.width - 22 * 6 - 12 - 4 - parent.spacing * 7)
                elide: Text.ElideRight
                anchors.verticalCenter: parent.verticalCenter

                MouseArea {
                    anchors.fill: parent
                    onClicked: row.toggleRequested()
                }
            }
            IconButton {
                glyph: Theme.icons.chevronUp
                variant: "ghost"
                buttonSize: 22
                iconSize: 12
                enabled: row.position > 0
                tooltip: qsTr("Bring forward")
                onClicked: EditorState.moveStyleLayer(EditorState.selectedTrack, EditorState.selectedClip,
                                                     row.layerId, row.storedIndex + 1)
            }
            IconButton {
                glyph: Theme.icons.chevronDown
                variant: "ghost"
                buttonSize: 22
                iconSize: 12
                enabled: row.position < row.count - 1
                tooltip: qsTr("Send backward")
                onClicked: EditorState.moveStyleLayer(EditorState.selectedTrack, EditorState.selectedClip,
                                                     row.layerId, row.storedIndex - 1)
            }
            IconButton {
                glyph: row.layerEnabled ? Theme.icons.eye : Theme.icons.eyeOff
                variant: "ghost"
                buttonSize: 22
                iconSize: 12
                tooltip: row.layerEnabled ? qsTr("Hide layer") : qsTr("Show layer")
                onClicked: row.setLayer({ "enabled": !row.layerEnabled })
            }
            IconButton {
                glyph: Theme.icons.copy
                variant: "ghost"
                buttonSize: 22
                iconSize: 12
                tooltip: qsTr("Duplicate layer")
                onClicked: EditorState.duplicateStyleLayer(EditorState.selectedTrack, EditorState.selectedClip,
                                                          row.layerId)
            }
            IconButton {
                glyph: Theme.icons.x
                variant: "ghost"
                buttonSize: 22
                iconSize: 12
                tooltip: qsTr("Remove layer")
                onClicked: EditorState.removeStyleLayer(EditorState.selectedTrack, EditorState.selectedClip,
                                                       row.layerId)
            }
        }
    }

    Column {
        width: parent.width
        spacing: Theme.spacingMd
        visible: row.expanded
        opacity: row.layerEnabled ? 1 : 0.45
        leftPadding: Theme.spacingSm
        rightPadding: Theme.spacingSm

        Row {
            width: parent.width - parent.leftPadding - parent.rightPadding
            spacing: 8

            PropertyKeyframeRow {
                width: (parent.width - parent.spacing) / 2
                propDef: row.prop("opacity", qsTr("Opacity"), 2)
                keyframeList: row.keyframes("opacity")
                useSlider: true
                sliderFrom: 0
                sliderTo: 1
            }

            Column {
                width: (parent.width - parent.spacing) / 2
                spacing: 4
                Text {
                    text: qsTr("Blend")
                    color: Theme.mutedForeground
                    font.pixelSize: Theme.fontSizeXs
                    font.family: Theme.fontFamily
                }
                ThemedComboBox {
                    width: parent.width
                    model: row.blendLabels
                    currentIndex: Math.max(0, row.blendModes.indexOf(row.layerData.blend || "normal"))
                    onActivated: row.setLayer({ "blend": row.blendModes[currentIndex] })
                }
            }
        }

        // Fill and stroke carry a full paint; the back layers are colour only.
        PaintEditor {
            width: parent.width - parent.leftPadding - parent.rightPadding
            visible: row.kind === "fill" || row.kind === "stroke"
            layerId: row.layerId
            paint: row.paint
            styleData: row.styleData
            keyPrefix: row.keyPrefix
        }

        Row {
            width: parent.width - parent.leftPadding - parent.rightPadding
            spacing: 8
            visible: row.kind === "stroke"

            PropertyKeyframeRow {
                width: (parent.width - parent.spacing) / 2
                propDef: row.prop("width", qsTr("Width"), 1)
                keyframeList: row.keyframes("width")
                useSlider: true
                sliderFrom: 0
                sliderTo: 100
                unit: "px"
            }

            Column {
                width: (parent.width - parent.spacing) / 2
                spacing: 4
                Text {
                    text: qsTr("Placement")
                    color: Theme.mutedForeground
                    font.pixelSize: Theme.fontSizeXs
                    font.family: Theme.fontFamily
                }
                ThemedComboBox {
                    width: parent.width
                    model: row.strokeAlignLabels
                    tooltip: qsTr("Centre the stroke on the outline, grow it outward, or keep it inside")
                    currentIndex: Math.max(0, row.strokeAligns.indexOf(row.layerData.strokeAlign || "outside"))
                    onActivated: row.setLayer({ "strokeAlign": row.strokeAligns[currentIndex] })
                }
            }
        }

        Row {
            width: parent.width - parent.leftPadding - parent.rightPadding
            spacing: 8
            visible: row.kind === "stroke"

            Column {
                width: (parent.width - parent.spacing) / 2
                spacing: 4
                Text {
                    text: qsTr("Dash")
                    color: Theme.mutedForeground
                    font.pixelSize: Theme.fontSizeXs
                    font.family: Theme.fontFamily
                }
                ThemedComboBox {
                    width: parent.width
                    model: row.dashLabels
                    currentIndex: Math.max(0, row.dashes.indexOf(row.layerData.dash || "solid"))
                    onActivated: row.setLayer({ "dash": row.dashes[currentIndex] })
                }
            }

            PropertyKeyframeRow {
                width: (parent.width - parent.spacing) / 2
                visible: (row.layerData.dash || "solid") !== "solid"
                propDef: row.prop("dashOffset", qsTr("Dash offset"), 2)
                keyframeList: row.keyframes("dashOffset")
                useSlider: true
                sliderFrom: -20
                sliderTo: 20
            }
        }

        Row {
            width: parent.width - parent.leftPadding - parent.rightPadding
            spacing: 8
            visible: row.kind === "stroke"

            PropertyKeyframeRow {
                width: (parent.width - parent.spacing) / 2
                propDef: row.prop("trimStart", qsTr("Trim start"), 2)
                keyframeList: row.keyframes("trimStart")
                useSlider: true
                sliderFrom: 0
                sliderTo: 1
                percent: true
            }
            PropertyKeyframeRow {
                width: (parent.width - parent.spacing) / 2
                propDef: { "key": row.keyPrefix + ".layer." + row.layerId + ".trimEnd", "label": qsTr("Trim end"),
                           "def": row.layerData.trimEnd !== undefined ? Number(row.layerData.trimEnd) : 1, "decimals": 2 }
                keyframeList: row.keyframes("trimEnd")
                useSlider: true
                sliderFrom: 0
                sliderTo: 1
                percent: true
            }
        }

        // Hand-drawn jitter on the outline, for fills and strokes.
        Column {
            width: parent.width - parent.leftPadding - parent.rightPadding
            spacing: Theme.spacingSm
            visible: row.kind === "stroke" || row.kind === "fill"

            Row {
                spacing: 4
                IconButton {
                    glyph: row.sketchExpanded || row.sketchy ? Theme.icons.chevronDown : Theme.icons.chevronRight
                    variant: "ghost"
                    buttonSize: 20
                    iconSize: 11
                    anchors.verticalCenter: parent.verticalCenter
                    onClicked: row.sketchExpanded = !row.sketchExpanded
                }
                Text {
                    text: qsTr("Sketchy")
                    anchors.verticalCenter: parent.verticalCenter
                    color: row.sketchy ? Theme.panelForeground : Theme.mutedForeground
                    font.pixelSize: Theme.fontSizeXs
                    font.family: Theme.fontFamily
                    MouseArea {
                        anchors.fill: parent
                        onClicked: row.sketchExpanded = !row.sketchExpanded
                    }
                }
            }

            Column {
                width: parent.width
                spacing: Theme.spacingMd
                visible: row.sketchExpanded || row.sketchy

                Row {
                    width: parent.width
                    spacing: 8
                    PropertyKeyframeRow {
                        width: (parent.width - parent.spacing) / 2
                        propDef: row.prop("sketchLength", qsTr("Segment"), 1)
                        keyframeList: row.keyframes("sketchLength")
                        useSlider: true
                        sliderFrom: 0
                        sliderTo: 60
                        unit: "px"
                    }
                    PropertyKeyframeRow {
                        width: (parent.width - parent.spacing) / 2
                        propDef: row.prop("sketchDeviation", qsTr("Wobble"), 1)
                        keyframeList: row.keyframes("sketchDeviation")
                        useSlider: true
                        sliderFrom: 0
                        sliderTo: 30
                        unit: "px"
                    }
                }

                Column {
                    width: (parent.width - 8) / 2
                    spacing: 4
                    Text {
                        text: qsTr("Seed")
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                    ThemedNumberField {
                        id: sketchSeedField
                        width: parent.width
                        decimals: 0
                        step: 1
                        from: 0
                        to: 9999
                        Binding on value {
                            when: !sketchSeedField.activeFocus
                            value: Number(row.layerData.sketchSeed) || 0
                        }
                        onEdited: v => row.setLayer({ "sketchSeed": Math.round(v) })
                    }
                }
            }
        }

        Column {
            width: parent.width - parent.leftPadding - parent.rightPadding
            spacing: 4
            visible: row.kind === "shadow" || row.kind === "glow" || row.kind === "extrude"
            Text {
                text: qsTr("Colour")
                color: Theme.mutedForeground
                font.pixelSize: Theme.fontSizeXs
                font.family: Theme.fontFamily
            }
            ColorSwatchField {
                hex: row.paint.color || "#ff000000"
                tooltip: qsTr("Choose the layer colour")
                onEdited: value => row.setLayer({ "paint": { "color": value } })
            }
        }

        Row {
            width: parent.width - parent.leftPadding - parent.rightPadding
            spacing: 8
            visible: row.kind === "shadow"

            PropertyKeyframeRow {
                width: (parent.width - parent.spacing) / 2
                propDef: row.prop("offsetX", qsTr("Offset X"), 1)
                keyframeList: row.keyframes("offsetX")
                useSlider: true
                sliderFrom: -500
                sliderTo: 500
                unit: "px"
            }
            PropertyKeyframeRow {
                width: (parent.width - parent.spacing) / 2
                propDef: row.prop("offsetY", qsTr("Offset Y"), 1)
                keyframeList: row.keyframes("offsetY")
                useSlider: true
                sliderFrom: -500
                sliderTo: 500
                unit: "px"
            }
        }

        Row {
            width: parent.width - parent.leftPadding - parent.rightPadding
            spacing: 8
            visible: row.kind === "shadow" || row.kind === "glow"

            PropertyKeyframeRow {
                width: (parent.width - parent.spacing) / 2
                propDef: row.prop("blur", row.kind === "glow" ? qsTr("Radius") : qsTr("Blur"), 1)
                keyframeList: row.keyframes("blur")
                useSlider: true
                sliderFrom: 0
                sliderTo: 200
                unit: "px"
            }
            PropertyKeyframeRow {
                width: (parent.width - parent.spacing) / 2
                propDef: row.prop("spread", qsTr("Spread"), 1)
                keyframeList: row.keyframes("spread")
                useSlider: true
                sliderFrom: -50
                sliderTo: 100
                unit: "px"
            }
        }

        Row {
            width: parent.width - parent.leftPadding - parent.rightPadding
            spacing: 8
            visible: row.kind === "extrude"

            PropertyKeyframeRow {
                width: (parent.width - parent.spacing) / 2
                propDef: row.prop("width", qsTr("Depth"), 1)
                keyframeList: row.keyframes("width")
                useSlider: true
                sliderFrom: 0
                sliderTo: 100
                unit: "px"
            }

            Column {
                width: (parent.width - parent.spacing) / 2
                spacing: 4
                Text {
                    text: qsTr("Angle")
                    color: Theme.mutedForeground
                    font.pixelSize: Theme.fontSizeXs
                    font.family: Theme.fontFamily
                }
                ThemedNumberField {
                    id: extrudeAngleField
                    width: parent.width
                    decimals: 0
                    step: 5
                    from: -360
                    to: 360
                    unit: "°"
                    Binding on value {
                        when: !extrudeAngleField.activeFocus
                        value: Number(row.layerData.extrudeAngle) || 0
                    }
                    onEdited: v => row.setLayer({ "extrudeAngle": v })
                }
            }
        }

        Column {
            width: parent.width - parent.leftPadding - parent.rightPadding
            spacing: 4
            visible: row.kind === "extrude"
            Text {
                text: qsTr("Darken")
                HoverHandler { id: darkenHover }
                ThemedToolTip {
                    text: qsTr("How much the extruded side fades toward black")
                    visible: darkenHover.hovered
                }
                color: Theme.mutedForeground
                font.pixelSize: Theme.fontSizeXs
                font.family: Theme.fontFamily
            }
            ThemedSlider {
                id: darkenSlider
                width: parent.width
                label: qsTr("Darken")
                from: 0
                to: 1
                stepSize: 0.01
                Binding on value {
                    when: !darkenSlider.pressed
                    value: Number(row.layerData.extrudeDarken) || 0
                }
                onMoved: {
                    if (pressed)
                        EditorState.previewSetStyleLayer(EditorState.selectedTrack, EditorState.selectedClip,
                                                        row.layerId, { "extrudeDarken": value })
                    else
                        row.setLayer({ "extrudeDarken": value })
                }
                onPressedChanged: {
                    if (pressed)
                        EditorState.beginPreviewDrag(qsTr("Adjust extrude"))
                    else
                        EditorState.commitPreviewDrag()
                }
            }
        }
    }
}
