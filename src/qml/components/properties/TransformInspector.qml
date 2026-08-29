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
    readonly property int canvasW: {
        void EditorState.tracks
        return Math.max(1, EditorState.projectWidth())
    }
    readonly property int canvasH: {
        void EditorState.tracks
        return Math.max(1, EditorState.projectHeight())
    }

    readonly property var propOpacity: { "key": "opacity", "label": qsTr("Opacity"), "def": 1.0, "decimals": 2 }
    readonly property var propX: { "key": "x", "label": "X", "def": 0.0, "decimals": 0 }
    readonly property var propY: { "key": "y", "label": "Y", "def": 0.0, "decimals": 0 }
    readonly property var propWidth: { "key": "width", "label": qsTr("Width"), "def": root.canvasW, "decimals": 0 }
    readonly property var propHeight: { "key": "height", "label": qsTr("Height"), "def": root.canvasH, "decimals": 0 }
    readonly property var propRotation: { "key": "rotation", "label": qsTr("Angle"), "def": 0.0, "decimals": 1 }

    height: contentCol.height
    implicitHeight: contentCol.height

    function refreshFields() {}

    Connections {
        target: EditorState
        function onSelectionChanged() { root.clipDataRevision++ }
        function onSelectedClipDataChanged() { root.clipDataRevision++ }
        function onTracksChanged() { root.clipDataRevision++ }
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
                text: qsTr("Position (px)")
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

            Text {
                text: qsTr("Size (px)")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                font.weight: Font.Medium
            }

            PropertyKeyframeRow {
                width: parent.width
                propDef: root.propWidth
                keyframeList: (root.clipData.keyframes && root.clipData.keyframes.width && root.clipData.keyframes.width.points) || []
                useSlider: true
                sliderFrom: 1
                sliderTo: Math.max(root.canvasW * 2, 2)
                unit: "px"
            }
            PropertyKeyframeRow {
                width: parent.width
                propDef: root.propHeight
                keyframeList: (root.clipData.keyframes && root.clipData.keyframes.height && root.clipData.keyframes.height.points) || []
                useSlider: true
                sliderFrom: 1
                sliderTo: Math.max(root.canvasH * 2, 2)
                unit: "px"
            }

            Text {
                text: qsTr("Opacity & rotation")
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
                width: parent.width
                propDef: root.propRotation
                keyframeList: (root.clipData.keyframes && root.clipData.keyframes.rotation && root.clipData.keyframes.rotation.points) || []
                useSlider: true
                sliderFrom: -180
                sliderTo: 180
                unit: "°"
            }

            Text {
                text: qsTr("Rotate 90°")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                font.weight: Font.Medium
            }

            Flow {
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
                            void EditorState.playheadSeconds
                            const cur = Number(root.clipData.rotationAtPlayhead || 0)
                            return Math.abs(cur - modelData) < 0.5
                        }
                        onClicked: EditorState.setClipRotationSnap(
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
                text: qsTr("Reset position & size")
                onClicked: EditorState.resetClipTransform(
                               EditorState.selectedTrack, EditorState.selectedClip)
            }
        }
    }
}
