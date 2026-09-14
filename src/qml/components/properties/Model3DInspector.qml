import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

// 3D model clip inspector: which .glb it is, which animation plays, and the pose and lighting
// knobs. Position is on the Transform tab (the model sits at the clip's x/y centre).
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
                                                 "loop": "loop", "offset": 0,
                                                 "scale": 0.5, "depth": 0.5,
                                                 "rotX": 0, "rotY": 0, "rotZ": 0,
                                                 "lightYaw": 30, "lightPitch": 20,
                                                 "lightIntensity": 1, "ambient": 0.35
                                             })
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
        const url = FileDialogs.openFile(qsTr("Replace 3D Model"), [qsTr("glTF binary (*.glb)")])
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

            ThemedLabel { text: qsTr("3D model") }

            ThemedLabel {
                width: parent.width
                opacity: 0.8
                elide: Text.ElideMiddle
                text: {
                    const n = root.animations.length
                    const anims = n > 0 ? qsTr("%n animation(s)", "", n) : qsTr("static")
                    return [anims, root.model3d.path].filter(s => s && s.length > 0).join(" · ")
                }
            }

            ThemedButton {
                text: qsTr("Replace model…")
                tooltip: qsTr("Load another .glb; position, length, pose and lighting stay")
                onClicked: root.replaceModel()
            }

            Row {
                width: parent.width
                spacing: Theme.spacingXs
                visible: !!root.model3d.warning
                IconGlyph {
                    glyph: Theme.icons.warning
                    iconSize: 12
                    iconColor: Theme.warning
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
                        color: Theme.mutedForeground
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
                        color: Theme.mutedForeground
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
                    color: Theme.mutedForeground
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

        // ----- Pose --------------------------------------------------------
        Column {
            width: parent.width
            spacing: Theme.spacingMd

            ThemedLabel { text: qsTr("Pose") }
            ThemedLabel {
                width: parent.width
                opacity: 0.8
                wrapMode: Text.Wrap
                text: qsTr("Rotations follow the model's own axes: X tilts, Y then spins about the tilted up axis, Z rolls after both.")
            }

            Row {
                width: parent.width
                spacing: 8
                PropertyKeyframeRow {
                    width: (parent.width - parent.spacing) / 2
                    propDef: root.knob("scale", qsTr("Size"), 2)
                    keyframeList: root.knobKeyframes("scale")
                    useSlider: true
                    sliderFrom: 0.05
                    sliderTo: 3
                }
                PropertyKeyframeRow {
                    width: (parent.width - parent.spacing) / 2
                    propDef: root.knob("depth", qsTr("Depth"), 2)
                    keyframeList: root.knobKeyframes("depth")
                    useSlider: true
                    sliderFrom: 0
                    sliderTo: 1
                    percent: true
                }
            }

            PropertyKeyframeRow {
                width: parent.width
                propDef: root.knob("rotX", qsTr("Rotation X"), 0)
                keyframeList: root.knobKeyframes("rotX")
                useSlider: true
                sliderFrom: -180
                sliderTo: 180
                unit: "°"
            }
            PropertyKeyframeRow {
                width: parent.width
                propDef: root.knob("rotY", qsTr("Rotation Y"), 0)
                keyframeList: root.knobKeyframes("rotY")
                useSlider: true
                sliderFrom: -180
                sliderTo: 180
                unit: "°"
            }
            PropertyKeyframeRow {
                width: parent.width
                propDef: root.knob("rotZ", qsTr("Rotation Z"), 0)
                keyframeList: root.knobKeyframes("rotZ")
                useSlider: true
                sliderFrom: -180
                sliderTo: 180
                unit: "°"
            }
        }

        // ----- Lighting ----------------------------------------------------
        Column {
            width: parent.width
            spacing: Theme.spacingMd

            ThemedLabel { text: qsTr("Lighting") }

            // The light orb: a sphere seen from the camera with the key light as a dot on it.
            // Dragging the dot orbits the light, writing the same keyframeable yaw/pitch the
            // sliders below edit. The front hemisphere is the face of the disk; "Behind" puts
            // the light on the far side for rim lighting.
            Row {
                width: parent.width
                spacing: Theme.spacingMd

                Item {
                    id: lightOrb
                    width: 96
                    height: 96
                    readonly property real radius: width / 2 - 4
                    readonly property real yaw: {
                        void root.clipDataRevision
                        void EditorState.playheadSeconds
                        return orbDrag.dragging ? orbDrag.liveYaw
                                                : EditorState.propertyValueAt(EditorState.selectedTrack, EditorState.selectedClip,
                                                                              "model3d.lightYaw", EditorState.playheadSeconds,
                                                                              Number(root.model3d.lightYaw) || 0)
                    }
                    readonly property real pitch: {
                        void root.clipDataRevision
                        void EditorState.playheadSeconds
                        return orbDrag.dragging ? orbDrag.livePitch
                                                : EditorState.propertyValueAt(EditorState.selectedTrack, EditorState.selectedClip,
                                                                              "model3d.lightPitch", EditorState.playheadSeconds,
                                                                              Number(root.model3d.lightPitch) || 0)
                    }
                    // Same mapping as the renderer's screenLightDir: +x right, +y up, +z toward
                    // the camera.
                    readonly property real dirX: Math.sin(yaw * Math.PI / 180) * Math.cos(pitch * Math.PI / 180)
                    readonly property real dirY: Math.sin(pitch * Math.PI / 180)
                    readonly property real dirZ: Math.cos(yaw * Math.PI / 180) * Math.cos(pitch * Math.PI / 180)
                    readonly property bool behind: dirZ < 0

                    onDirXChanged: sphere.requestPaint()
                    onDirYChanged: sphere.requestPaint()
                    onDirZChanged: sphere.requestPaint()

                    Canvas {
                        id: sphere
                        anchors.fill: parent
                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.reset()
                            const cx = width / 2
                            const cy = height / 2
                            const r = lightOrb.radius
                            // Highlight where the light hits the front of the sphere; a light
                            // behind it only leaves a faint rim.
                            const hx = cx + lightOrb.dirX * r * 0.6
                            const hy = cy - lightOrb.dirY * r * 0.6
                            const g = ctx.createRadialGradient(hx, hy, 2, cx, cy, r)
                            const bright = lightOrb.behind ? 0.25 : 1.0
                            g.addColorStop(0, Qt.rgba(0.85 * bright + 0.1, 0.85 * bright + 0.1, 0.9 * bright + 0.1, 1))
                            g.addColorStop(0.7, Qt.rgba(0.3, 0.32, 0.36, 1))
                            g.addColorStop(1, Qt.rgba(0.12, 0.13, 0.15, 1))
                            ctx.fillStyle = g
                            ctx.beginPath()
                            ctx.arc(cx, cy, r, 0, Math.PI * 2)
                            ctx.fill()
                            if (lightOrb.behind) {
                                ctx.strokeStyle = Qt.rgba(0.9, 0.9, 0.95, 0.8)
                                ctx.lineWidth = 2
                                ctx.beginPath()
                                ctx.arc(cx, cy, r - 1, 0, Math.PI * 2)
                                ctx.stroke()
                            }
                        }
                        Component.onCompleted: requestPaint()
                    }

                    // The light itself.
                    Rectangle {
                        width: 12
                        height: 12
                        radius: 6
                        x: parent.width / 2 + lightOrb.dirX * lightOrb.radius - width / 2
                        y: parent.height / 2 - lightOrb.dirY * lightOrb.radius - height / 2
                        color: lightOrb.behind ? "transparent" : Theme.primary
                        border.width: 2
                        border.color: lightOrb.behind ? Theme.primary : Theme.onMedia
                    }

                    MouseArea {
                        id: orbDrag
                        anchors.fill: parent
                        preventStealing: true
                        cursorShape: Qt.CrossCursor
                        property bool dragging: false
                        property real liveYaw: 0
                        property real livePitch: 0

                        function apply(mx, my) {
                            const r = lightOrb.radius
                            let nx = (mx - width / 2) / r
                            let ny = -(my - height / 2) / r
                            const len = Math.sqrt(nx * nx + ny * ny)
                            if (len > 1) {
                                nx /= len
                                ny /= len
                            }
                            const nz = Math.sqrt(Math.max(0, 1 - nx * nx - ny * ny)) * (behindChip.selected ? -1 : 1)
                            liveYaw = Math.atan2(nx, nz) * 180 / Math.PI
                            livePitch = Math.asin(Math.max(-1, Math.min(1, ny))) * 180 / Math.PI
                            EditorState.previewSetClipKeyframe(EditorState.selectedTrack, EditorState.selectedClip,
                                                               "model3d.lightYaw", EditorState.playheadSeconds, liveYaw)
                            EditorState.previewSetClipKeyframe(EditorState.selectedTrack, EditorState.selectedClip,
                                                               "model3d.lightPitch", EditorState.playheadSeconds, livePitch)
                        }
                        onPressed: mouse => {
                            dragging = true
                            EditorState.beginPreviewDrag(qsTr("Move light"))
                            apply(mouse.x, mouse.y)
                        }
                        onPositionChanged: mouse => { if (dragging) apply(mouse.x, mouse.y) }
                        onReleased: {
                            if (!dragging)
                                return
                            EditorState.commitPreviewDrag()
                            dragging = false
                        }
                    }
                }

                Column {
                    width: parent.width - lightOrb.width - parent.spacing
                    spacing: Theme.spacingXs
                    anchors.verticalCenter: parent.verticalCenter
                    ThemedLabel {
                        width: parent.width
                        opacity: 0.8
                        wrapMode: Text.Wrap
                        text: qsTr("Drag the light around the sphere. The light stays fixed to the camera, not the model.")
                    }
                    ThemedChip {
                        id: behindChip
                        text: qsTr("Behind")
                        tooltip: qsTr("Put the light on the far side of the model for a rim light")
                        selected: lightOrb.behind
                        onClicked: {
                            // Mirror through the disk: same x/y on the sphere, opposite side.
                            const yaw = lightOrb.yaw
                            const flipped = yaw > 0 ? 180 - yaw : -180 - yaw
                            EditorState.beginPreviewDrag(qsTr("Move light"))
                            EditorState.previewSetClipKeyframe(EditorState.selectedTrack, EditorState.selectedClip,
                                                               "model3d.lightYaw", EditorState.playheadSeconds, flipped)
                            EditorState.commitPreviewDrag()
                        }
                    }
                }
            }

            Row {
                width: parent.width
                spacing: 8
                PropertyKeyframeRow {
                    width: (parent.width - parent.spacing) / 2
                    propDef: root.knob("lightYaw", qsTr("Direction"), 0)
                    keyframeList: root.knobKeyframes("lightYaw")
                    useSlider: true
                    sliderFrom: -180
                    sliderTo: 180
                    unit: "°"
                }
                PropertyKeyframeRow {
                    width: (parent.width - parent.spacing) / 2
                    propDef: root.knob("lightPitch", qsTr("Elevation"), 0)
                    keyframeList: root.knobKeyframes("lightPitch")
                    useSlider: true
                    sliderFrom: -90
                    sliderTo: 90
                    unit: "°"
                }
            }

            Row {
                width: parent.width
                spacing: 8
                PropertyKeyframeRow {
                    width: (parent.width - parent.spacing) / 2
                    propDef: root.knob("lightIntensity", qsTr("Intensity"), 2)
                    keyframeList: root.knobKeyframes("lightIntensity")
                    useSlider: true
                    sliderFrom: 0
                    sliderTo: 3
                }
                PropertyKeyframeRow {
                    width: (parent.width - parent.spacing) / 2
                    propDef: root.knob("ambient", qsTr("Ambient"), 2)
                    keyframeList: root.knobKeyframes("ambient")
                    useSlider: true
                    sliderFrom: 0
                    sliderTo: 1
                    percent: true
                }
            }
        }
    }
}
