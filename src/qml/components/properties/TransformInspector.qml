import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

Item {
    id: root

    property int clipDataRevision: 0
    readonly property var clipData: {
        void clipDataRevision
        return EditorState.selectedClipData
    }
    readonly property bool hasSelection: !!clipData && Object.keys(clipData).length > 0
    readonly property string clipKind: hasSelection ? (clipData.kind || "") : ""
    // A model clip is a full-canvas layer placed by its camera: x/y shift the model, the box
    // size and spin mean nothing (its own size and rotation live on the 3D Model tab).
    readonly property bool isModel3d: clipKind === "model3d"
    readonly property int canvasW: {
        void EditorState.tracksRevision
        return Math.max(1, EditorState.projectWidth())
    }
    readonly property int canvasH: {
        void EditorState.tracksRevision
        return Math.max(1, EditorState.projectHeight())
    }

    readonly property var propOpacity: { "key": "opacity", "label": qsTr("Opacity"), "def": 1.0, "decimals": 2 }
    readonly property var propX: { "key": "x", "label": "X", "def": 0.0, "decimals": 0 }
    readonly property var propY: { "key": "y", "label": "Y", "def": 0.0, "decimals": 0 }
    readonly property var propWidth: { "key": "width", "label": qsTr("Width"), "def": root.canvasW, "decimals": 0 }
    readonly property var propHeight: { "key": "height", "label": qsTr("Height"), "def": root.canvasH, "decimals": 0 }
    readonly property var propRotation: { "key": "rotation", "label": qsTr("Angle"), "def": 0.0, "decimals": 1 }
    readonly property var propRotationX: { "key": "rotationX", "label": qsTr("Tilt X"), "def": 0.0, "decimals": 1 }
    readonly property var propRotationY: { "key": "rotationY", "label": qsTr("Tilt Y"), "def": 0.0, "decimals": 1 }
    readonly property var propZ: { "key": "z", "label": qsTr("Depth"), "def": 0.0, "decimals": 0 }
    readonly property var propPerspective: { "key": "perspective", "label": qsTr("Perspective"), "def": 2000.0, "decimals": 0 }

    // One slider that scales width and height together about the box centre; the link button
    // swaps it for the separate Width/Height rows.
    property bool sizeLinked: true
    readonly property bool isText: clipKind === "text" || clipKind === "subtitle"

    // 100% is the box that fills the canvas on its tighter axis — what Reset and a fitted
    // import both produce — whatever the clip's own aspect ratio.
    property int liveRevision: 0
    readonly property real scaleAtPlayhead: {
        void clipDataRevision
        void liveRevision
        if (!hasSelection)
            return 1
        const w = transformAt("width", canvasW)
        const h = transformAt("height", canvasH)
        return Math.max(w / canvasW, h / canvasH)
    }

    property var scaleDragStart: null

    function transformAt(key, def) {
        return EditorState.propertyValueAt(EditorState.selectedTrack, EditorState.selectedClip,
                                           key, EditorState.inspectorPlayheadSeconds, def)
    }

    function beginScaleDrag() {
        scaleDragStart = {
            "x": transformAt("x", 0),
            "y": transformAt("y", 0),
            "w": transformAt("width", canvasW),
            "h": transformAt("height", canvasH),
            "scale": scaleAtPlayhead,
            "pixelSize": (clipData.textStyle && clipData.textStyle.pixelSize) || 64
        }
    }

    function applyScale(scale) {
        const s = scaleDragStart
        if (!s || s.scale <= 0)
            return
        const k = scale / s.scale
        const w = Math.max(1, s.w * k)
        const h = Math.max(1, s.h * k)
        const x = s.x + (s.w - w) / 2
        const y = s.y + (s.h - h) / 2
        if (isText) {
            EditorState.previewSetTextRect(EditorState.selectedTrack, EditorState.selectedClip,
                                           x, y, w, h, Math.round(s.pixelSize * k))
        } else {
            EditorState.previewSetClipRect(EditorState.selectedTrack, EditorState.selectedClip,
                                           x, y, w, h)
        }
    }

    height: contentCol.height
    implicitHeight: contentCol.height

    function refreshFields() {}

    Connections {
        target: EditorState
        function onSelectionChanged() { root.clipDataRevision++ }
        function onSelectedClipDataChanged() { root.clipDataRevision++ }
        function onTracksChanged() { root.clipDataRevision++ }
        function onClipPropertiesPreviewed(trackIndex, clipIndex, keys) {
            if (!scaleSlider.pressed && (keys.indexOf("width") >= 0 || keys.indexOf("height") >= 0))
                root.liveRevision++
        }
    }

    Column {
        id: contentCol
        width: root.width
        spacing: Theme.spacingXl

        EmptyState {
            visible: root.clipKind === "audio"
            width: parent.width
            compact: true
            glyph: Theme.icons.film
            title: qsTr("Video only")
            hint: qsTr("This tab does not apply to audio clips.")
        }

        Column {
            width: root.width
            spacing: 10
            visible: root.clipKind !== "audio"

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("Move to a time, set a value, then click the diamond to add a keyframe. With Auto keyframes on, dragging a slider or the preview also creates them.")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            ThemedChip {
                text: qsTr("Auto keyframes")
                selected: EditorState.autoKeyEnabled
                onClicked: EditorState.autoKeyEnabled = !EditorState.autoKeyEnabled
            }

            Text {
                text: root.isModel3d ? qsTr("Offset (px)") : qsTr("Position (px)")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                font.weight: Font.Medium
            }

            PropertyKeyframeRow {
                width: parent.width
                propDef: root.propX
                keyframeList: (root.clipData.keyframes && root.clipData.keyframes.x && root.clipData.keyframes.x.points) || []
                useSlider: true
                sliderFrom: -root.canvasW
                sliderTo: root.canvasW * 2
                unit: "px"
            }
            PropertyKeyframeRow {
                width: parent.width
                propDef: root.propY
                keyframeList: (root.clipData.keyframes && root.clipData.keyframes.y && root.clipData.keyframes.y.points) || []
                useSlider: true
                sliderFrom: -root.canvasH
                sliderTo: root.canvasH * 2
                unit: "px"
            }

            Item {
                visible: !root.isModel3d
                width: parent.width
                height: sizeLinkButton.height

                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.sizeLinked ? qsTr("Scale") : qsTr("Size (px)")
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    font.weight: Font.Medium
                }

                IconButton {
                    id: sizeLinkButton
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    buttonSize: 24
                    iconSize: 14
                    glyph: root.sizeLinked ? Theme.icons.linkTwo : Theme.icons.unlink
                    active: root.sizeLinked
                    tooltip: root.sizeLinked ? qsTr("Edit width and height separately")
                                             : qsTr("Scale width and height together")
                    onClicked: root.sizeLinked = !root.sizeLinked
                }
            }

            Row {
                visible: !root.isModel3d && root.sizeLinked
                width: parent.width
                spacing: 8

                ThemedSlider {
                    id: scaleSlider
                    lockWhilePlaying: true
                    label: qsTr("Scale")
                    width: parent.width - scaleReadout.width - parent.spacing
                    anchors.verticalCenter: parent.verticalCenter
                    from: 0.05
                    to: 4
                    valueFormatter: function (v) { return Math.round(v * 100) + "%" }
                    Binding on value {
                        when: !scaleSlider.pressed
                        value: root.scaleAtPlayhead
                    }
                    onPressedChanged: {
                        if (pressed) {
                            root.beginScaleDrag()
                            EditorState.beginPreviewDrag(qsTr("Scale clip"))
                        } else {
                            EditorState.commitPreviewDrag()
                        }
                    }
                    onMoved: root.applyScale(value)
                }

                Text {
                    id: scaleReadout
                    width: 48
                    anchors.verticalCenter: parent.verticalCenter
                    horizontalAlignment: Text.AlignRight
                    text: Math.round((scaleSlider.pressed ? scaleSlider.value : root.scaleAtPlayhead) * 100) + "%"
                    color: Theme.panelForeground
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.fontSizeSm
                }
            }

            PropertyKeyframeRow {
                visible: !root.isModel3d && !root.sizeLinked
                width: parent.width
                propDef: root.propWidth
                keyframeList: (root.clipData.keyframes && root.clipData.keyframes.width && root.clipData.keyframes.width.points) || []
                useSlider: true
                sliderFrom: 1
                sliderTo: Math.max(root.canvasW * 2, 2)
                unit: "px"
            }
            PropertyKeyframeRow {
                visible: !root.isModel3d && !root.sizeLinked
                width: parent.width
                propDef: root.propHeight
                keyframeList: (root.clipData.keyframes && root.clipData.keyframes.height && root.clipData.keyframes.height.points) || []
                useSlider: true
                sliderFrom: 1
                sliderTo: Math.max(root.canvasH * 2, 2)
                unit: "px"
            }

            Text {
                text: root.isModel3d ? qsTr("Opacity") : qsTr("Opacity & rotation")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                font.weight: Font.Medium
            }

            PropertyKeyframeRow {
                width: parent.width
                propDef: root.propOpacity
                keyframeList: (root.clipData.keyframes && root.clipData.keyframes.opacity && root.clipData.keyframes.opacity.points) || []
                useSlider: true
                sliderFrom: 0
                sliderTo: 1
                percent: true
            }

            PropertyKeyframeRow {
                visible: !root.isModel3d
                width: parent.width
                propDef: root.propRotation
                keyframeList: (root.clipData.keyframes && root.clipData.keyframes.rotation && root.clipData.keyframes.rotation.points) || []
                useSlider: true
                sliderFrom: -180
                sliderTo: 180
                unit: "°"
            }

            Text {
                visible: !root.isModel3d
                text: qsTr("Rotate 90°")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                font.weight: Font.Medium
            }

            Flow {
                visible: !root.isModel3d
                width: parent.width
                spacing: 6
                Repeater {
                    // Plain ints avoid JS-object model role quirks (e.g. "value").
                    model: [0, 90, 180, -90]
                    delegate: ThemedChip {
                        required property int modelData
                        text: modelData + "°"
                        selected: {
                            void root.clipDataRevision
                            void EditorState.inspectorPlayheadSeconds
                            const cur = Number(root.clipData.rotationAtPlayhead || 0)
                            return Math.abs(cur - modelData) < 0.5
                        }
                        onClicked: EditorState.setClipRotationSnap(
                                       EditorState.selectedTrack, EditorState.selectedClip,
                                       modelData)
                    }
                }
            }

            ThemedSwitch {
                visible: !root.isModel3d
                text: qsTr("3D layer")
                tooltip: qsTr("Tilt the clip and push it in depth, with 3D grips on the preview. Turning it off flattens the clip again.")
                // A Binding rather than a plain one: the click itself assigns `checked`, which
                // would otherwise sever it and leave the switch stale on the next clip.
                Binding on checked {
                    value: {
                        void root.clipDataRevision
                        return !!root.clipData.layer3d
                    }
                }
                onToggled: EditorState.setClipLayer3d(EditorState.selectedTrack,
                                                      EditorState.selectedClip, checked)
            }

            // The preview gizmo's tool and the axes its handles follow. Editor preferences shared by
            // every clip, not stored on this one.
            Row {
                visible: !root.isModel3d && !!root.clipData.layer3d
                spacing: 6

                Repeater {
                    model: [
                        { value: "move", glyph: Theme.icons.move3d, label: qsTr("Move"), action: "gizmoMove",
                          tip: qsTr("Arrows on the preview move the clip along each axis") },
                        { value: "rotate", glyph: Theme.icons.rotate3d, label: qsTr("Rotate"), action: "gizmoRotate",
                          tip: qsTr("Rings on the preview turn the clip about each axis") },
                        { value: "scale", glyph: Theme.icons.scale3d, label: qsTr("Scale"), action: "gizmoScale",
                          tip: qsTr("Handles on the preview stretch the clip along its own edges") }
                    ]
                    delegate: IconButton {
                        required property var modelData
                        readonly property string key: EditorState.shortcutFor(modelData.action)
                        glyph: modelData.glyph
                        variant: "text"
                        tooltip: (key.length > 0 ? qsTr("%1 (%2)").arg(modelData.label).arg(key)
                                                 : modelData.label) + "\n" + modelData.tip
                        Accessible.name: modelData.label
                        active: EditorState.gizmoTool === modelData.value
                        onClicked: EditorState.gizmoTool = modelData.value
                    }
                }
            }

            Flow {
                visible: !root.isModel3d && !!root.clipData.layer3d
                width: parent.width
                spacing: 6

                Repeater {
                    model: [
                        { value: "global", label: qsTr("Global"),
                          tip: qsTr("Gizmo follows the camera: X across, Y down, Z toward you") },
                        { value: "local", label: qsTr("Local"),
                          tip: qsTr("Gizmo follows the clip's own edges and face, however it is turned") }
                    ]
                    delegate: ThemedChip {
                        required property var modelData
                        text: modelData.label
                        tooltip: modelData.tip
                        selected: EditorState.gizmoOrientation === modelData.value
                        onClicked: EditorState.gizmoOrientation = modelData.value
                    }
                }
            }

            PropertyKeyframeRow {
                visible: !root.isModel3d && !!root.clipData.layer3d
                width: parent.width
                propDef: root.propRotationX
                keyframeList: (root.clipData.keyframes && root.clipData.keyframes.rotationX && root.clipData.keyframes.rotationX.points) || []
                useSlider: true
                sliderFrom: -180
                sliderTo: 180
                unit: "°"
            }
            PropertyKeyframeRow {
                visible: !root.isModel3d && !!root.clipData.layer3d
                width: parent.width
                propDef: root.propRotationY
                keyframeList: (root.clipData.keyframes && root.clipData.keyframes.rotationY && root.clipData.keyframes.rotationY.points) || []
                useSlider: true
                sliderFrom: -180
                sliderTo: 180
                unit: "°"
            }
            PropertyKeyframeRow {
                visible: !root.isModel3d && !!root.clipData.layer3d
                width: parent.width
                propDef: root.propZ
                keyframeList: (root.clipData.keyframes && root.clipData.keyframes.z && root.clipData.keyframes.z.points) || []
                useSlider: true
                sliderFrom: -4000
                sliderTo: 1500
                unit: "px"
            }
            PropertyKeyframeRow {
                visible: !root.isModel3d && !!root.clipData.layer3d
                width: parent.width
                propDef: root.propPerspective
                keyframeList: (root.clipData.keyframes && root.clipData.keyframes.perspective && root.clipData.keyframes.perspective.points) || []
                useSlider: true
                sliderFrom: 200
                sliderTo: 8000
                unit: "px"
            }

            Text {
                visible: root.clipKind === "video"
                text: qsTr("Fix orientation")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                font.weight: Font.Medium
            }

            Text {
                visible: root.clipKind === "video"
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("Corrects the source's own rotation losslessly — unlike Angle above, this changes decoding, not just the on-screen box.")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            Flow {
                visible: root.clipKind === "video"
                width: parent.width
                spacing: 6
                Repeater {
                    model: [0, 90, 180, 270]
                    delegate: ThemedChip {
                        required property int modelData
                        text: modelData + "°"
                        selected: {
                            void root.clipDataRevision
                            return Number(root.clipData.orientation) === modelData
                        }
                        onClicked: EditorState.setClipOrientation(
                                       EditorState.selectedTrack, EditorState.selectedClip,
                                       modelData)
                    }
                }
            }

            Text {
                text: qsTr("Flip")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                font.weight: Font.Medium
            }

            Flow {
                width: parent.width
                spacing: 6
                ThemedChip {
                    text: qsTr("Flip H")
                    selected: {
                        void root.clipDataRevision
                        return !!root.clipData.flipH
                    }
                    onClicked: EditorState.setClipFlip(
                                   EditorState.selectedTrack, EditorState.selectedClip,
                                   !root.clipData.flipH, !!root.clipData.flipV)
                }
                ThemedChip {
                    text: qsTr("Flip V")
                    selected: {
                        void root.clipDataRevision
                        return !!root.clipData.flipV
                    }
                    onClicked: EditorState.setClipFlip(
                                   EditorState.selectedTrack, EditorState.selectedClip,
                                   !!root.clipData.flipH, !root.clipData.flipV)
                }
            }

            ThemedButton {
                text: root.isModel3d ? qsTr("Reset position") : qsTr("Reset position & size")
                onClicked: EditorState.resetClipTransform(
                               EditorState.selectedTrack, EditorState.selectedClip)
            }
        }
    }
}
