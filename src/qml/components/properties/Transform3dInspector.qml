pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Drift
// The selected 3D layer or model placed in 3D: Position (where its anchor point sits, in canvas
// px), Anchor, Rotation and Scale as X/Y/Z fields. Everything writes the clip's ordinary tracks,
// so the keyframes, graph, easing and undo are the ones the Transform tab uses. Position is the
// anchor's place in the scene; the anchor is in frame px (the clip's unscaled size).
Item {
    id: root

    property int clipDataRevision: 0
    property int liveRevision: 0
    readonly property var clipData: {
        void clipDataRevision
        return EditorState.selectedClipData
    }
    readonly property bool hasSelection: !!clipData && Object.keys(clipData).length > 0
    readonly property string clipKind: hasSelection ? (clipData.kind || "") : ""
    readonly property bool isCamera: clipKind === "adjustment" && clipData.adjustmentKind === "camera"
    readonly property bool isModel3d: clipKind === "model3d"
    readonly property bool isText: clipKind === "text" || clipKind === "subtitle"
    readonly property bool is3d: hasSelection && !isCamera && (isModel3d || !!clipData.layer3d)
    readonly property bool canMake3d: hasSelection && !isCamera && !isModel3d && !clipData.layer3d
                                      && clipKind !== "adjustment" && clipKind !== "audio"
    readonly property bool blendIsNormal: !hasSelection || (clipData.blendMode || "normal") === "normal"
    readonly property bool projectHasCamera: {
        void EditorState.tracksRevision
        return EditorState.preview.projectHasCamera()
    }
    readonly property int canvasW: {
        void EditorState.tracksRevision
        return Math.max(1, EditorState.projectFile.projectWidth())
    }
    readonly property int canvasH: {
        void EditorState.tracksRevision
        return Math.max(1, EditorState.projectFile.projectHeight())
    }

    function valueAt(key, def) {
        void clipDataRevision
        void liveRevision
        return EditorState.propertyValueAt(EditorState.selectedTrack, EditorState.selectedClip, key,
                                           EditorState.inspectorPlayheadSeconds, def)
    }

    readonly property real frameW: hasSelection && clipData.frameW > 0 ? clipData.frameW : canvasW
    readonly property real frameH: hasSelection && clipData.frameH > 0 ? clipData.frameH : canvasH
    // Anchor in px from the frame's top-left corner (the frame's centre until it is set); position
    // is where the anchor lands on the canvas, with depth added to the anchor's Z.
    readonly property var anchorNow: [valueAt("anchorX", frameW / 2), valueAt("anchorY", frameH / 2), valueAt("anchorZ", 0)]
    readonly property var positionNow: [valueAt("positionX", canvasW / 2), valueAt("positionY", canvasH / 2),
                                        valueAt("z", 0) + anchorNow[2]]
    readonly property var rotationNow: [valueAt("rotationX", 0), valueAt("rotationY", 0), valueAt("rotation", 0)]
    readonly property real scaleNow: {
        void clipDataRevision
        void liveRevision
        return valueAt("scaleX", 1)
    }
    readonly property real depthScaleNow: valueAt("model3d.modelScaleZ", Number((clipData.model3d || {}).modelScaleZ) || 1)
    readonly property var positions: {
        void clipDataRevision
        void liveRevision
        void EditorState.inspectorPlayheadSeconds
        void EditorState.tracksRevision
        return is3d ? EditorState.preview.selectionPositions() : ({})
    }

    function keysOf(name) {
        const entry = name.startsWith("model3d.")
                      ? ((clipData.model3d || {}).keyframes || {})[name.slice(8)]
                      : (clipData.keyframes || {})[name]
        return (entry && entry.points) || []
    }
    function keyedAt(names) {
        const t = EditorState.inspectorPlayheadSeconds
        return names.every(n => keysOf(n).some(p => Math.abs(p.seconds - t) <= 1 / 30))
    }
    function anyKeys(names) {
        return names.some(n => keysOf(n).length > 0)
    }
    function setKey(name, value) {
        EditorState.showKeyframeGraphProperty(name)
        EditorState.setClipKeyframe(EditorState.selectedTrack, EditorState.selectedClip, name,
                                    EditorState.playheadSeconds, value)
    }
    function toggleKeys(names) {
        if (keyedAt(names))
            EditorState.keyframes.removeKeysAt(EditorState.selectedTrack, EditorState.selectedClip, names,
                                               EditorState.inspectorPlayheadSeconds)
        else
            EditorState.keyframes.addKeysAt(EditorState.selectedTrack, EditorState.selectedClip, names,
                                            EditorState.inspectorPlayheadSeconds)
    }
    function setValues(undoText, names, values) {
        EditorState.beginPreviewDrag(undoText)
        for (let i = 0; i < names.length; ++i)
            EditorState.previewSetClipKeyframe(EditorState.selectedTrack, EditorState.selectedClip, names[i],
                                               EditorState.playheadSeconds, values[i])
        EditorState.commitPreviewDrag()
    }
    function setScale(percent) {
        const sx = valueAt("scaleX", 1)
        if (sx === 0)
            return
        const k = percent / 100
        EditorState.beginPreviewDrag(qsTr("Scale clip"))
        EditorState.previewSetClipScale(EditorState.selectedTrack, EditorState.selectedClip,
                                        k, k * valueAt("scaleY", 1) / sx)
        EditorState.commitPreviewDrag()
    }

    Connections {
        target: EditorState
        function onSelectionChanged() { root.clipDataRevision++ }
        function onSelectedClipDataChanged() { root.clipDataRevision++ }
        function onTracksChanged() { root.clipDataRevision++ }
        function onClipPropertiesPreviewed() { root.liveRevision++ }
    }

    // A titled row of X/Y/Z-style fields with one key button for the whole group.
    component FieldGroup: Column {
        id: group

        property string title
        property var labels: ["X", "Y", "Z"]
        property var values: []
        property var keyNames: []
        property int decimals: 1
        property real from: -1e6
        property real to: 1e6
        property real step: 1
        property string unit: ""
        property string resetTip
        property bool resettable: true
        signal edited(int axis, real value)
        signal reset()

        spacing: Theme.spacingXs

        Row {
            width: parent.width
            spacing: Theme.spacingXs

            Text {
                id: titleText
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - keyButton.width - resetButton.width - parent.spacing * 2
                text: group.title
                elide: Text.ElideRight
                color: root.anyKeys(group.keyNames) ? Theme.c.transform3dPanel.foreground
                                                    : Theme.c.transform3dPanel.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                font.weight: Font.Medium
            }
            IconButton {
                id: keyButton
                anchors.verticalCenter: parent.verticalCenter
                visible: group.keyNames.length > 0
                width: visible ? buttonSize : 0
                buttonSize: 24
                iconSize: 16
                active: {
                    void root.clipDataRevision
                    return root.keyedAt(group.keyNames)
                }
                glyph: active ? Theme.icons.diamondMinus : Theme.icons.diamondPlus
                tooltip: active ? qsTr("Remove the %1 keyframes at the playhead").arg(group.title)
                                : qsTr("Add %1 keyframes at the playhead").arg(group.title)
                onClicked: root.toggleKeys(group.keyNames)
            }
            IconButton {
                id: resetButton
                anchors.verticalCenter: parent.verticalCenter
                visible: group.resettable
                width: visible ? buttonSize : 0
                buttonSize: 24
                iconSize: 14
                glyph: Theme.icons.reset
                tooltip: group.resetTip
                onClicked: group.reset()
            }
        }

        Row {
            width: parent.width
            spacing: Theme.spacingXs

            Repeater {
                model: group.values.length

                delegate: Row {
                    id: cell
                    required property int index
                    width: (group.width - (group.values.length - 1) * Theme.spacingXs) / group.values.length
                    spacing: Theme.spacingXs

                    Text {
                        id: axisLabel
                        anchors.verticalCenter: parent.verticalCenter
                        text: group.labels[cell.index]
                        color: Theme.c.transform3dPanel.foregroundMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                    }
                    ThemedNumberField {
                        id: field
                        width: cell.width - axisLabel.implicitWidth - cell.spacing
                        from: group.from
                        to: group.to
                        step: group.step
                        decimals: group.decimals
                        unit: group.unit
                        Binding on value {
                            when: !field.activeFocus
                            value: group.values[cell.index]
                        }
                        onEdited: v => group.edited(cell.index, v)
                    }
                }
            }
        }
    }

    implicitHeight: content.implicitHeight

    Column {
        id: content
        width: root.width
        spacing: Theme.spacingMd

        Text {
            width: parent.width
            visible: !root.is3d
            text: root.isCamera ? qsTr("A camera has no transform of its own. Position, rotation and zoom are in the Camera panel.")
                  : root.canMake3d ? qsTr("This clip is flat. Make it a 3D layer to rotate it and move it in depth.")
                  : qsTr("Select a 3D layer, a model or a splat.")
            color: Theme.c.transform3dPanel.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            wrapMode: Text.WordWrap
        }

        ThemedButton {
            visible: root.canMake3d
            text: qsTr("Make 3D layer")
            onClicked: EditorState.setClipLayer3d(EditorState.selectedTrack, EditorState.selectedClip, true)
        }

        Column {
            width: parent.width
            visible: root.is3d
            spacing: Theme.spacingMd

            Row {
                visible: ShellBridge.phoneShell
                spacing: Theme.spacingXs
                Repeater {
                    model: [
                        { value: "move", glyph: Theme.icons.move3d, label: qsTr("Move") },
                        { value: "rotate", glyph: Theme.icons.rotate3d, label: qsTr("Rotate") },
                        { value: "scale", glyph: Theme.icons.scale3d, label: qsTr("Scale") }
                    ]
                    delegate: IconButton {
                        required property var modelData
                        glyph: modelData.glyph
                        tooltip: modelData.label
                        active: EditorState.preview.gizmoTool === modelData.value
                        onClicked: EditorState.preview.gizmoTool = modelData.value
                    }
                }
            }

            Row {
                spacing: Theme.spacingXs
                Repeater {
                    model: [
                        { value: "world", label: qsTr("World"), tip: qsTr("Gizmo axes follow the scene") },
                        { value: "local", label: qsTr("Local"), tip: qsTr("Gizmo axes follow the selected object (scale always follows the object's own edges)") },
                        { value: "view", label: qsTr("View"), tip: qsTr("Gizmo axes follow the screen") }
                    ]
                    delegate: ThemedChip {
                        required property var modelData
                        text: modelData.label
                        tooltip: modelData.tip
                        selected: EditorState.preview.gizmoOrientation === modelData.value
                        onClicked: EditorState.preview.gizmoOrientation = modelData.value
                    }
                }
            }

            Column {
                width: parent.width
                spacing: 2
                SearchAnchor {
                    searchId: "transform3d-readout"
                    title: qsTr("World and local position")
                    keywords: "3d anchor point coordinates scene"
                }
                Repeater {
                    model: [
                        { label: qsTr("World"), prefix: "world" },
                        { label: qsTr("Local"), prefix: "local" }
                    ]
                    delegate: Text {
                        required property var modelData
                        width: content.width
                        text: root.positions.worldX === undefined ? ""
                              : qsTr("%1  X %2  Y %3  Z %4").arg(modelData.label)
                                    .arg(Math.round(root.positions[modelData.prefix + "X"]))
                                    .arg(Math.round(root.positions[modelData.prefix + "Y"]))
                                    .arg(Math.round(root.positions[modelData.prefix + "Z"]))
                        visible: text.length > 0
                        color: Theme.c.transform3dPanel.foregroundMuted
                        font.family: Theme.monoFontFamily
                        font.pixelSize: Theme.fontSizeXs
                        elide: Text.ElideRight
                    }
                }
            }

            FieldGroup {
                width: parent.width
                title: qsTr("Position (px)")
                values: root.positionNow
                keyNames: ["positionX", "positionY", "z"]
                decimals: 0
                resetTip: qsTr("Reset position & size")
                SearchAnchor {
                    searchId: "transform3d-position"
                    title: qsTr("3D position")
                    keywords: "move x y z depth"
                }
                onEdited: (axis, v) => root.setKey(keyNames[axis], axis === 2 ? v - root.anchorNow[2] : v)
                onReset: EditorState.resetClipTransform(EditorState.selectedTrack, EditorState.selectedClip)
            }

            FieldGroup {
                width: parent.width
                title: qsTr("Anchor point (px)")
                values: root.anchorNow
                keyNames: ["anchorX", "anchorY", "anchorZ"]
                decimals: 0
                resetTip: qsTr("Put the anchor point back at the centre of the clip")
                SearchAnchor {
                    searchId: "transform3d-anchor"
                    title: qsTr("3D anchor point")
                    keywords: "pivot origin center rotate around"
                }
                onEdited: (axis, v) => root.setKey(keyNames[axis], v)
                onReset: root.setValues(qsTr("Reset anchor"), keyNames, [root.frameW / 2, root.frameH / 2, 0])
            }

            FieldGroup {
                width: parent.width
                title: qsTr("Rotation (°)")
                values: root.rotationNow
                keyNames: ["rotationX", "rotationY", "rotation"]
                decimals: 1
                from: -3600
                to: 3600
                unit: "°"
                resetTip: qsTr("Reset rotation")
                SearchAnchor {
                    searchId: "transform3d-rotation"
                    title: qsTr("3D rotation")
                    keywords: "tilt pitch yaw roll orientation x y z"
                }
                onEdited: (axis, v) => root.setKey(keyNames[axis], v)
                onReset: root.setValues(qsTr("Reset rotation"), keyNames, [0, 0, 0])
            }

            FieldGroup {
                width: parent.width
                title: root.isModel3d ? qsTr("Scale / depth scale (%)") : qsTr("Scale (%)")
                labels: ["", "Z"]
                values: root.isModel3d ? [Math.round(root.scaleNow * 100), Math.round(root.depthScaleNow * 100)]
                                       : [Math.round(root.scaleNow * 100)]
                keyNames: root.isModel3d ? ["scaleX", "scaleY", "model3d.modelScaleZ"] : ["scaleX", "scaleY"]
                decimals: 0
                from: 1
                to: 1000
                unit: "%"
                resetTip: qsTr("Reset scale to 100%")
                SearchAnchor {
                    searchId: "transform3d-scale"
                    title: qsTr("3D scale")
                    keywords: "size resize depth z model"
                }
                onEdited: (axis, v) => {
                    if (axis === 0)
                        root.setScale(v)
                    else
                        root.setKey("model3d.modelScaleZ", v / 100)
                }
                onReset: {
                    root.setScale(100)
                    if (root.isModel3d)
                        root.setKey("model3d.modelScaleZ", 1)
                }
            }

            ThemedSwitch {
                enabled: root.blendIsNormal
                text: qsTr("Occlude by depth")
                tooltip: root.blendIsNormal
                         ? qsTr("Let depth decide what covers this clip, instead of its track position. Only clips with this on sort against each other; everything else still draws in track order.")
                         : qsTr("Needs the Normal blend mode — the others read the canvas behind the clip, which depth cannot sort.")
                SearchAnchor {
                    searchId: "transform3d-occlude"
                    title: qsTr("Occlude by depth")
                    keywords: "3d depth sort cover"
                }
                Binding on checked {
                    value: {
                        void root.clipDataRevision
                        return !!root.clipData.depthOcclude
                    }
                }
                onToggled: EditorState.setClipDepthOcclude(EditorState.selectedTrack,
                                                           EditorState.selectedClip, checked)
            }

            PropertyKeyframeRow {
                visible: !root.projectHasCamera
                width: parent.width
                propDef: ({ "key": "perspective", "label": qsTr("Perspective"),
                            "def": Math.round(root.canvasH / 2 / 0.24), "decimals": 0 })
                keyframeList: root.keysOf("perspective")
                useSlider: true
                sliderFrom: 200
                sliderTo: 8000
                unit: "px"
                SearchAnchor {
                    searchId: "transform3d-perspective"
                    title: qsTr("Perspective")
                    keywords: "lens fov focal length 3d"
                }
            }
        }
    }
}
