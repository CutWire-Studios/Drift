import QtQuick
import QtQuick.Controls.Basic
import Drift
// 3D model clip inspector: which model or splat it is, which animation plays, and the lighting knobs. The
// model is placed like any 3D layer, from the Transform tab.
Item {
    id: root

    property int clipDataRevision: 0
    readonly property var clipData: {
        void clipDataRevision
        return EditorState.selectedClipData
    }
    readonly property bool hasSelection: !!clipData && Object.keys(clipData).length > 0
    readonly property bool hasModel: hasSelection && clipData.kind === "model3d" && !!clipData.model3d
    readonly property var model3d: hasModel ? clipData.model3d : ({
                                                 "path": "", "animations": [], "animation": 0,
                                                 "loop": "loop", "offset": 0, "ambient": 0.35
                                             })
    readonly property bool isSplat: !!(root.model3d && root.model3d.isSplat)
    readonly property var animations: (root.model3d && root.model3d.animations) || []
    readonly property var keyframes: (root.model3d && root.model3d.keyframes) || ({})

    function knob(key, label, decimals) {
        return { "key": "model3d." + key, "label": label, "def": Number(root.model3d[key]) || 0, "decimals": decimals }
    }
    function knobKeyframes(key) {
        const entry = root.keyframes[key]
        return (entry && entry.points) || []
    }

    function refresh() {
        if (offsetField && !offsetField.activeFocus)
            offsetField.value = root.model3d.offset
    }

    function setOption(key, value) {
        const patch = {}
        patch[key] = value
        const error = EditorState.setModel3dOptions(EditorState.selectedTrack, EditorState.selectedClip, patch)
        if (error.length > 0)
            EditorState.setLastMessage(error, "error")
    }

    function replaceModel() {
        const url = FileDialogs.openFile(qsTr("Replace 3D Model"), [qsTr("3D models and Gaussian splats (*.glb *.sog *.ply *.zip meta.json lod-meta.json)")])
        if (url == "")
            return
        const path = url.toString().replace(/^file:\/\//, "")
        const reply = EditorState.setModel3dSource(EditorState.selectedTrack, EditorState.selectedClip, path, {})
        if (!reply.ok)
            EditorState.setLastMessage(reply.error || qsTr("Could not load the model"), "error")
    }

    height: contentCol.height
    implicitHeight: contentCol.height

    Connections {
        target: EditorState
        function onSelectionChanged() { root.clipDataRevision++; root.refresh() }
        function onSelectedClipDataChanged() { root.clipDataRevision++; root.refresh() }
        function onTracksChanged() { root.clipDataRevision++; root.refresh() }
    }

    Component.onCompleted: refresh()

    Column {
        id: contentCol
        width: root.width
        spacing: Theme.spacingXl

        // ----- File --------------------------------------------------------
        Column {
            width: parent.width
            spacing: Theme.spacingXs

            ThemedLabel {
                text: qsTr("3D model")
                SearchAnchor {
                    searchId: "model3d-model"
                    title: qsTr("3D model")
                    keywords: "mesh"
                }
            }

            ThemedLabel {
                width: parent.width
                opacity: 0.8
                elide: Text.ElideMiddle
                text: {
                    const n = root.animations.length
                    if (root.isSplat)
                        return [qsTr("Gaussian splat"), root.model3d.path].filter(s => s && s.length > 0).join(" · ")
                    const anims = n > 0 ? qsTr("%n animation(s)", "", n) : qsTr("static")
                    return [anims, root.model3d.path].filter(s => s && s.length > 0).join(" · ")
                }
            }

            ThemedButton {
                text: qsTr("Replace model…")
                tooltip: qsTr("Load another model or splat; position, length, pose and lighting stay")
                onClicked: root.replaceModel()
            }

            Row {
                width: parent.width
                spacing: Theme.spacingXs
                visible: !!root.model3d.warning
                IconGlyph {
                    glyph: Theme.icons.warning
                    iconSize: 12
                    iconColor: Theme.c.model3DInspector.iconWarning
                    anchors.verticalCenter: parent.verticalCenter
                }
                ThemedLabel {
                    width: parent.width - 12 - parent.spacing
                    opacity: 0.8
                    wrapMode: Text.Wrap
                    text: root.model3d.warning || ""
                }
            }
        }

        // ----- Playback ----------------------------------------------------
        Column {
            width: parent.width
            spacing: Theme.spacingSm
            visible: root.animations.length > 0

            ThemedLabel { text: qsTr("Playback") }

            Row {
                width: parent.width
                spacing: 8

                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 4
                    Text {
                        text: qsTr("Animation")
                        color: Theme.c.model3DInspector.foregroundMuted
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                    ThemedComboBox {
                        width: parent.width
                        model: root.animations.map((a, i) => a.name && a.name.length > 0
                                                               ? a.name : qsTr("Animation %1").arg(i + 1))
                        tooltip: qsTr("Which of the file's animations plays")
                        currentIndex: Math.max(0, Math.min(root.animations.length - 1, root.model3d.animation))
                        onActivated: root.setOption("animation", currentIndex)
                    }
                }

                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 4
                    Text {
                        text: qsTr("After the end")
                        color: Theme.c.model3DInspector.foregroundMuted
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                    ThemedComboBox {
                        width: parent.width
                        readonly property var ids: ["hold", "loop", "pingpong", "hide"]
                        model: [qsTr("Hold last frame"), qsTr("Loop"), qsTr("Ping-pong"), qsTr("Hide")]
                        tooltip: qsTr("What plays once the animation has run its length")
                        currentIndex: Math.max(0, ids.indexOf(root.model3d.loop))
                        onActivated: root.setOption("loop", ids[currentIndex])
                    }
                }
            }

            Column {
                width: parent.width
                spacing: 4
                Text {
                    text: qsTr("Start offset")
                    color: Theme.c.model3DInspector.foregroundMuted
                    font.pixelSize: Theme.fontSizeXs
                    font.family: Theme.fontFamily
                }
                ThemedNumberField {
                    id: offsetField
                    width: parent.width
                    unit: "s"
                    decimals: 2
                    step: 0.1
                    from: -3600
                    to: 3600
                    onEdited: v => root.setOption("offset", v)
                }
            }
        }

        ThemedLabel {
            width: parent.width
            opacity: 0.8
            wrapMode: Text.Wrap
            text: qsTr("Position, size, rotation and depth occlusion are on the Transform tab.")
        }

        // ----- Lighting ----------------------------------------------------
        // Four lights placed like 3D Relight's: across the clip's rect, near or far of the model's
        // centre. The 3D view draws them and drags them; these are the same keyframeable values.
        Column {
            width: parent.width
            spacing: Theme.spacingMd
            visible: !root.isSplat

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
    }
}
