import QtQuick
import Drift
// The lights of a model clip: an ambient level and four lights placed like 3D Relight's, across the
// clip's rect, near or far of the model's centre. The 3D view draws them and drags them; these are
// the same keyframeable values. Shared by the 3D Model inspector and the View & Lights panel.
Column {
    id: root

    property int clipDataRevision: 0
    readonly property var clipData: {
        void clipDataRevision
        return EditorState.selectedClipData
    }
    readonly property bool hasModel: !!clipData && clipData.kind === "model3d" && !!clipData.model3d
    readonly property var model3d: hasModel ? clipData.model3d : ({})
    readonly property var keyframes: (root.model3d && root.model3d.keyframes) || ({})

    function knob(key, label, decimals) {
        return { "key": "model3d." + key, "label": label, "def": Number(root.model3d[key]) || 0, "decimals": decimals }
    }
    function knobKeyframes(key) {
        const entry = root.keyframes[key]
        return (entry && entry.points) || []
    }

    function setOption(key, value) {
        const patch = {}
        patch[key] = value
        const error = EditorState.setModel3dOptions(EditorState.selectedTrack, EditorState.selectedClip, patch)
        if (error.length > 0)
            EditorState.setLastMessage(error, "error")
    }

    spacing: Theme.spacingMd

    Connections {
        target: EditorState
        function onSelectionChanged() { root.clipDataRevision++ }
        function onSelectedClipDataChanged() { root.clipDataRevision++ }
        function onTracksChanged() { root.clipDataRevision++ }
    }

    ThemedLabel { text: qsTr("Lighting") }

    ThemedLabel {
        width: parent.width
        opacity: 0.8
        wrapMode: Text.Wrap
        text: qsTr("Drag the lights in the 3D view, or set them here. They move and scale with the clip; turning the model turns it under them.")
    }

    PropertyKeyframeRow {
        width: parent.width
        propDef: root.knob("ambient", qsTr("Ambient"), 2)
        keyframeList: root.knobKeyframes("ambient")
        useSlider: true
        sliderFrom: 0
        sliderTo: 1
        percent: true
    }

    Repeater {
        model: 4

        delegate: Column {
            id: lightGroup
            required property int index
            readonly property string prefix: "light" + (index + 1) + "_"
            readonly property bool on: !!root.model3d[prefix + "enabled"]
            readonly property bool spot: (Number(root.model3d[prefix + "cone"]) || 180) < 179.5

            width: parent.width
            spacing: Theme.spacingXs

            Row {
                width: parent.width
                spacing: 8

                Text {
                    width: parent.width - lightSwatch.width - lightSwitch.width - 2 * parent.spacing
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Light %1").arg(lightGroup.index + 1)
                    color: Theme.c.model3DInspector.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm
                    font.weight: Font.Medium
                }
                ColorSwatchField {
                    id: lightSwatch
                    anchors.verticalCenter: parent.verticalCenter
                    hex: root.model3d[lightGroup.prefix + "color"] || "#ffffff"
                    tooltip: qsTr("Light %1 colour").arg(lightGroup.index + 1)
                    onEdited: value => root.setOption(lightGroup.prefix + "color", value)
                }
                ThemedSwitch {
                    id: lightSwitch
                    anchors.verticalCenter: parent.verticalCenter
                    checked: lightGroup.on
                    onToggled: root.setOption(lightGroup.prefix + "enabled", checked)
                }
            }

            Grid {
                visible: lightGroup.on
                width: parent.width
                columns: 2
                columnSpacing: 8
                rowSpacing: Theme.spacingXs
                readonly property real cellWidth: (width - columnSpacing) / 2

                PropertyKeyframeRow {
                    width: parent.cellWidth
                    propDef: root.knob(lightGroup.prefix + "x", qsTr("Left / right"), 2)
                    keyframeList: root.knobKeyframes(lightGroup.prefix + "x")
                    useSlider: true
                    sliderFrom: -1
                    sliderTo: 2
                }
                PropertyKeyframeRow {
                    width: parent.cellWidth
                    propDef: root.knob(lightGroup.prefix + "y", qsTr("Up / down"), 2)
                    keyframeList: root.knobKeyframes(lightGroup.prefix + "y")
                    useSlider: true
                    sliderFrom: -1
                    sliderTo: 2
                }
                PropertyKeyframeRow {
                    width: parent.cellWidth
                    propDef: root.knob(lightGroup.prefix + "z", qsTr("Near / far"), 2)
                    keyframeList: root.knobKeyframes(lightGroup.prefix + "z")
                    useSlider: true
                    sliderFrom: -3
                    sliderTo: 3
                }
                PropertyKeyframeRow {
                    width: parent.cellWidth
                    propDef: root.knob(lightGroup.prefix + "intensity", qsTr("Brightness"), 2)
                    keyframeList: root.knobKeyframes(lightGroup.prefix + "intensity")
                    useSlider: true
                    sliderFrom: 0
                    sliderTo: 4
                }
                PropertyKeyframeRow {
                    width: parent.cellWidth
                    propDef: root.knob(lightGroup.prefix + "radius", qsTr("Reach"), 2)
                    keyframeList: root.knobKeyframes(lightGroup.prefix + "radius")
                    useSlider: true
                    sliderFrom: 0.05
                    sliderTo: 5
                }
                PropertyKeyframeRow {
                    width: parent.cellWidth
                    propDef: root.knob(lightGroup.prefix + "cone", qsTr("Spot angle"), 0)
                    keyframeList: root.knobKeyframes(lightGroup.prefix + "cone")
                    useSlider: true
                    sliderFrom: 10
                    sliderTo: 180
                    unit: "°"
                }
                PropertyKeyframeRow {
                    visible: lightGroup.spot
                    width: parent.cellWidth
                    propDef: root.knob(lightGroup.prefix + "aimX", qsTr("Aim left / right"), 2)
                    keyframeList: root.knobKeyframes(lightGroup.prefix + "aimX")
                    useSlider: true
                    sliderFrom: 0
                    sliderTo: 1
                }
                PropertyKeyframeRow {
                    visible: lightGroup.spot
                    width: parent.cellWidth
                    propDef: root.knob(lightGroup.prefix + "aimY", qsTr("Aim up / down"), 2)
                    keyframeList: root.knobKeyframes(lightGroup.prefix + "aimY")
                    useSlider: true
                    sliderFrom: 0
                    sliderTo: 1
                }
            }
        }
    }
}
