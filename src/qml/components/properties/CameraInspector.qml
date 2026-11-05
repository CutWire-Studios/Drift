import QtQuick
import QtQuick.Controls.Basic
import Drift
// The scene camera: one viewpoint the whole sequence is seen through. A camera clip has no pixels
// of its own, so this is its entire inspector.
//
// It stores no properties of its own either — position (the eye, world px), rotation, point of
// interest and zoom are the clip's ordinary tracks read with camera meanings (see
// engine/render3d/scene/SceneCamera3d.h), which is what gives the camera keyframes, the keyframe
// graph, easing and undo with no new machinery. The property keys below are therefore the familiar
// ones: x/y/z, rotationX/rotationY/rotation, anchorX/Y/Z and perspective (the zoom).
Item {
    id: root

    property int clipDataRevision: 0
    readonly property var clipData: {
        void clipDataRevision
        return EditorState.selectedClipData
    }
    readonly property bool hasSelection: !!clipData && Object.keys(clipData).length > 0
    readonly property bool isCamera: hasSelection && clipData.kind === "adjustment"
                                     && clipData.adjustmentKind === "camera"

    readonly property int canvasW: {
        void EditorState.tracksRevision
        return Math.max(1, EditorState.projectFile.projectWidth())
    }
    readonly property int canvasH: {
        void EditorState.tracksRevision
        return Math.max(1, EditorState.projectFile.projectHeight())
    }
    readonly property real defaultZoom: canvasH * 0.5 / 0.24
    readonly property bool aiming: hasSelection && !!clipData.aimAtTarget

    readonly property var propPosX: { "key": "x", "label": qsTr("Position X"), "def": root.canvasW / 2, "decimals": 0 }
    readonly property var propPosY: { "key": "y", "label": qsTr("Position Y"), "def": root.canvasH / 2, "decimals": 0 }
    readonly property var propPosZ: { "key": "z", "label": qsTr("Position Z"), "def": -root.defaultZoom, "decimals": 0 }
    readonly property var propRotX: { "key": "rotationX", "label": qsTr("Rotation X"), "def": 0.0, "decimals": 1 }
    readonly property var propRotY: { "key": "rotationY", "label": qsTr("Rotation Y"), "def": 0.0, "decimals": 1 }
    readonly property var propRotZ: { "key": "rotation", "label": qsTr("Rotation Z"), "def": 0.0, "decimals": 1 }
    readonly property var propPoiX: { "key": "anchorX", "label": qsTr("Point of Interest X"), "def": root.canvasW / 2, "decimals": 0 }
    readonly property var propPoiY: { "key": "anchorY", "label": qsTr("Point of Interest Y"), "def": root.canvasH / 2, "decimals": 0 }
    readonly property var propPoiZ: { "key": "anchorZ", "label": qsTr("Point of Interest Z"), "def": 0.0, "decimals": 0 }
    readonly property var propZoom: { "key": "perspective", "label": qsTr("Zoom"), "def": root.defaultZoom, "decimals": 0 }

    // The zoom at the playhead, which the focal length and field of view fields show in their units.
    readonly property real zoomNow: {
        void clipDataRevision
        void EditorState.tracksRevision
        if (!isCamera)
            return root.defaultZoom
        return EditorState.propertyValueAt(EditorState.selectedTrack, EditorState.selectedClip,
                                           "perspective", EditorState.inspectorPlayheadSeconds,
                                           root.defaultZoom)
    }

    function setZoom(zoom) {
        EditorState.showKeyframeGraphProperty("perspective")
        EditorState.setClipKeyframe(EditorState.selectedTrack, EditorState.selectedClip, "perspective",
                                    EditorState.playheadSeconds, zoom)
    }

    function keysFor(name) {
        return (clipData.keyframes && clipData.keyframes[name] && clipData.keyframes[name].points) || []
    }

    implicitHeight: column.implicitHeight

    Column {
        id: column
        width: parent.width
        spacing: Theme.spacingMd

        Text {
            width: parent.width
            visible: !root.isCamera
            text: qsTr("Select a camera clip to frame the shot.")
            color: Theme.c.cameraInspector.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            wrapMode: Text.WordWrap
        }

        Column {
            width: parent.width
            visible: root.isCamera
            spacing: Theme.spacingMd

            Text {
                width: parent.width
                text: qsTr("Everything on the timeline is seen through this camera while the clip "
                           + "lasts. Position is the camera's eye in the same 3D space as the clips; "
                           + "a camera at rest looks exactly like no camera at all.")
                SearchAnchor {
                    searchId: "camera-position"
                    title: qsTr("Camera position")
                    keywords: "3d eye"
                }
                color: Theme.c.cameraInspector.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                wrapMode: Text.WordWrap
            }

            PropertyKeyframeRow {
                width: parent.width
                propDef: root.propPosX
                keyframeList: root.keysFor("x")
                useSlider: true
                sliderFrom: -root.canvasW * 2
                sliderTo: root.canvasW * 3
                unit: "px"
            }
            PropertyKeyframeRow {
                width: parent.width
                propDef: root.propPosY
                keyframeList: root.keysFor("y")
                useSlider: true
                sliderFrom: -root.canvasH * 2
                sliderTo: root.canvasH * 3
                unit: "px"
            }
            PropertyKeyframeRow {
                width: parent.width
                propDef: root.propPosZ
                keyframeList: root.keysFor("z")
                useSlider: true
                sliderFrom: -8000
                sliderTo: 4000
                unit: "px"
            }

            // Turns the camera about its own eye, yaw first, so the horizon stays level at zero roll.
            SearchAnchor {
                searchId: "camera-rotation"
                title: qsTr("Camera rotation")
                keywords: "3d pitch yaw roll orientation"
                target: cameraRotationXRow
            }
            PropertyKeyframeRow {
                id: cameraRotationXRow
                width: parent.width
                propDef: root.propRotX
                keyframeList: root.keysFor("rotationX")
                useSlider: true
                sliderFrom: -180
                sliderTo: 180
                unit: "°"
            }
            PropertyKeyframeRow {
                width: parent.width
                propDef: root.propRotY
                keyframeList: root.keysFor("rotationY")
                useSlider: true
                sliderFrom: -180
                sliderTo: 180
                unit: "°"
            }
            PropertyKeyframeRow {
                width: parent.width
                propDef: root.propRotZ
                keyframeList: root.keysFor("rotation")
                useSlider: true
                sliderFrom: -180
                sliderTo: 180
                unit: "°"
            }

            ThemedSwitch {
                text: qsTr("Aim at target")
                tooltip: qsTr("Keep the camera facing its point of interest. Rotation then turns the camera relative to that aim.")
                SearchAnchor {
                    searchId: "camera-aim-at-target"
                    title: qsTr("Aim at target")
                    keywords: "3d point of interest look at two-node"
                }
                Binding on checked {
                    value: {
                        void root.clipDataRevision
                        return root.aiming
                    }
                }
                onToggled: EditorState.setCameraAimAtTarget(EditorState.selectedTrack,
                                                            EditorState.selectedClip, checked)
            }
            PropertyKeyframeRow {
                visible: root.aiming
                width: parent.width
                propDef: root.propPoiX
                keyframeList: root.keysFor("anchorX")
                useSlider: true
                sliderFrom: -root.canvasW * 2
                sliderTo: root.canvasW * 3
                unit: "px"
            }
            PropertyKeyframeRow {
                visible: root.aiming
                width: parent.width
                propDef: root.propPoiY
                keyframeList: root.keysFor("anchorY")
                useSlider: true
                sliderFrom: -root.canvasH * 2
                sliderTo: root.canvasH * 3
                unit: "px"
            }
            PropertyKeyframeRow {
                visible: root.aiming
                width: parent.width
                propDef: root.propPoiZ
                keyframeList: root.keysFor("anchorZ")
                useSlider: true
                sliderFrom: -4000
                sliderTo: 4000
                unit: "px"
            }

            SearchAnchor {
                searchId: "camera-zoom"
                title: qsTr("Camera zoom")
                keywords: "3d lens focal length fov field of view"
                target: cameraZoomRow
            }
            PropertyKeyframeRow {
                id: cameraZoomRow
                width: parent.width
                propDef: root.propZoom
                keyframeList: root.keysFor("perspective")
                useSlider: true
                sliderFrom: 200
                sliderTo: 8000
                unit: "px"
            }
            Row {
                width: parent.width
                spacing: Theme.spacingMd

                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 4
                    Text {
                        text: qsTr("Focal length")
                        color: Theme.c.cameraInspector.foregroundMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                    }
                    ThemedNumberField {
                        id: focalField
                        width: parent.width
                        from: 1
                        to: 2000
                        decimals: 1
                        step: 1
                        unit: "mm"
                        Binding on value {
                            when: !focalField.activeFocus
                            value: root.zoomNow * 24 / root.canvasH
                        }
                        onEdited: v => root.setZoom(EditorState.cameraZoomFromFocalLength(v))
                    }
                }
                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 4
                    Text {
                        text: qsTr("Field of view")
                        color: Theme.c.cameraInspector.foregroundMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                    }
                    ThemedNumberField {
                        id: fovField
                        width: parent.width
                        from: 1
                        to: 170
                        decimals: 1
                        step: 1
                        unit: "°"
                        Binding on value {
                            when: !fovField.activeFocus
                            value: 2 * Math.atan(root.canvasH * 0.5 / Math.max(1, root.zoomNow)) * 180 / Math.PI
                        }
                        onEdited: v => root.setZoom(EditorState.cameraZoomFromFov(v))
                    }
                }
            }

            Text {
                width: parent.width
                text: qsTr("Zoom, focal length (36 × 24 mm film) and field of view are one setting in "
                           + "three units. Zooming does not move the camera. While this camera is "
                           + "running it replaces each clip's own Perspective value, because a scene "
                           + "has one viewer.")
                color: Theme.c.cameraInspector.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                wrapMode: Text.WordWrap
            }

            ThemedButton {
                text: qsTr("Reset camera")
                tooltip: qsTr("Back to rest: the default position, no rotation, no target and the default zoom, framing the canvas head on.")
                onClicked: EditorState.preview.resetSceneCamera(EditorState.selectedTrack, EditorState.selectedClip)
            }
        }
    }
}
