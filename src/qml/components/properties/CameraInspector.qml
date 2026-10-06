import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

// The scene camera: one viewpoint the whole sequence is seen through. A camera clip has no pixels
// of its own, so this is its entire inspector.
//
// It stores no properties of its own either — pan, dolly, pitch, yaw, roll and the lens are the
// clip's ordinary transform tracks read with camera meanings (see engine/SceneCamera3d.h), which is
// what gives the camera keyframes, the keyframe graph, easing and undo with no new machinery. The
// property keys below are therefore the familiar ones; only the labels change.
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

    readonly property var propPanX: { "key": "x", "label": qsTr("Pan X"), "def": 0.0, "decimals": 0 }
    readonly property var propPanY: { "key": "y", "label": qsTr("Pan Y"), "def": 0.0, "decimals": 0 }
    readonly property var propDolly: { "key": "z", "label": qsTr("Dolly"), "def": 0.0, "decimals": 0 }
    readonly property var propPitch: { "key": "rotationX", "label": qsTr("Pitch"), "def": 0.0, "decimals": 1 }
    readonly property var propYaw: { "key": "rotationY", "label": qsTr("Yaw"), "def": 0.0, "decimals": 1 }
    readonly property var propRoll: { "key": "rotation", "label": qsTr("Roll"), "def": 0.0, "decimals": 1 }
    readonly property var propLens: { "key": "perspective", "label": qsTr("Lens"), "def": 2000.0, "decimals": 0 }

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
            color: Theme.mutedForeground
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
                           + "lasts. A camera at rest looks exactly like no camera at all, so the "
                           + "numbers below are all offsets from the normal view.")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                wrapMode: Text.WordWrap
            }

            // Position. Pan reads across the frame whichever way the camera is facing, and Dolly
            // moves it along the direction it looks — so these stay intuitive after an orbit.
            PropertyKeyframeRow {
                width: parent.width
                propDef: root.propPanX
                keyframeList: root.keysFor("x")
                useSlider: true
                sliderFrom: -2000
                sliderTo: 2000
                unit: "px"
            }
            PropertyKeyframeRow {
                width: parent.width
                propDef: root.propPanY
                keyframeList: root.keysFor("y")
                useSlider: true
                sliderFrom: -2000
                sliderTo: 2000
                unit: "px"
            }
            PropertyKeyframeRow {
                width: parent.width
                propDef: root.propDolly
                keyframeList: root.keysFor("z")
                useSlider: true
                sliderFrom: -1500
                sliderTo: 4000
                unit: "px"
            }

            // Orientation. With no pan or dolly these orbit the canvas plane rather than spinning
            // the camera in place, which is the turntable behaviour the preview shows.
            PropertyKeyframeRow {
                width: parent.width
                propDef: root.propPitch
                keyframeList: root.keysFor("rotationX")
                useSlider: true
                sliderFrom: -180
                sliderTo: 180
                unit: "°"
            }
            PropertyKeyframeRow {
                width: parent.width
                propDef: root.propYaw
                keyframeList: root.keysFor("rotationY")
                useSlider: true
                sliderFrom: -180
                sliderTo: 180
                unit: "°"
            }
            PropertyKeyframeRow {
                width: parent.width
                propDef: root.propRoll
                keyframeList: root.keysFor("rotation")
                useSlider: true
                sliderFrom: -180
                sliderTo: 180
                unit: "°"
            }

            PropertyKeyframeRow {
                width: parent.width
                propDef: root.propLens
                keyframeList: root.keysFor("perspective")
                useSlider: true
                sliderFrom: 200
                sliderTo: 8000
                unit: "px"
            }

            Text {
                width: parent.width
                text: qsTr("A short lens exaggerates depth; a long one flattens it. While this "
                           + "camera is running it replaces each clip's own Perspective value, "
                           + "because a scene has one viewer.")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                wrapMode: Text.WordWrap
            }

            ThemedButton {
                text: qsTr("Reset camera position")
                tooltip: qsTr("Back to rest: no pan, dolly or turn, framing the canvas head on. The lens is kept.")
                onClicked: EditorState.resetSceneCamera(EditorState.selectedTrack, EditorState.selectedClip)
            }
        }
    }
}
