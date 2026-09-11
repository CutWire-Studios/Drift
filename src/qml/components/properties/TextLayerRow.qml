import QtQuick
import Drift
import ".."

// One row of the text shading stack. The list is shown front-most first, so `position` counts
// from the front while the stored order (what moveTextLayer takes) counts from the back.
Column {
    id: row

    property var layerData: ({})
    property var textStyle: ({})
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

    width: parent ? parent.width : 200
    spacing: 6

    function setLayer(patch) {
        EditorState.setTextLayer(EditorState.selectedTrack, EditorState.selectedClip, row.layerId, patch)
    }
    function keyframes(field) {
        const keys = row.textStyle && row.textStyle.keyframes
        const entry = keys && keys["layer." + row.layerId + "." + field]
        return (entry && entry.points) || []
    }
    function prop(field, label, decimals) {
        return { "key": "text.layer." + row.layerId + "." + field, "label": label,
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
                onClicked: EditorState.moveTextLayer(EditorState.selectedTrack, EditorState.selectedClip,
                                                     row.layerId, row.storedIndex + 1)
            }
            IconButton {
                glyph: Theme.icons.chevronDown
                variant: "ghost"
                buttonSize: 22
                iconSize: 12
                enabled: row.position < row.count - 1
                tooltip: qsTr("Send backward")
                onClicked: EditorState.moveTextLayer(EditorState.selectedTrack, EditorState.selectedClip,
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
                onClicked: EditorState.duplicateTextLayer(EditorState.selectedTrack, EditorState.selectedClip,
                                                          row.layerId)
            }
            IconButton {
                glyph: Theme.icons.x
                variant: "ghost"
                buttonSize: 22
                iconSize: 12
                tooltip: qsTr("Remove layer")
                onClicked: EditorState.removeTextLayer(EditorState.selectedTrack, EditorState.selectedClip,
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
        TextPaintEditor {
            width: parent.width - parent.leftPadding - parent.rightPadding
            visible: row.kind === "fill" || row.kind === "stroke"
            layerId: row.layerId
            paint: row.paint
            textStyle: row.textStyle
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
                ThemedToggleButton {
                    text: qsTr("Outside")
                    checked: row.layerData.strokeOutside === true
                    tooltip: qsTr("Grow the stroke outward so it never eats into the letter")
                    onClicked: row.setLayer({ "strokeOutside": !(row.layerData.strokeOutside === true) })
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
                        EditorState.previewSetTextLayer(EditorState.selectedTrack, EditorState.selectedClip,
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
