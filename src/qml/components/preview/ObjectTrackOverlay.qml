import QtQuick
import Drift

// The tracking circle, drawn in the selected video clip's layout. Dragging it
// sets the seed the next scan starts from; once a track exists and is current,
// the circle rides the baked point so you can scrub and see what was followed.
Item {
    id: root

    property int revision: 0
    readonly property var clipData: {
        void revision
        return EditorState.selectedClipData
    }
    readonly property bool videoSelected: !!clipData && clipData.kind === "video"
    readonly property real sx: width / Math.max(1, EditorState.projectWidth())
    readonly property real sy: height / Math.max(1, EditorState.projectHeight())

    property var box: null
    property real nx: 0.5
    property real ny: 0.5
    property real radius: 0.12
    property bool dragging: false

    function refresh() {
        revision++
        if (!videoSelected) {
            box = null
            return
        }
        const list = EditorState.previewClipsAtPlayhead()
        let found = null
        for (let i = 0; i < list.length; ++i) {
            const entry = list[i]
            if (entry.track === EditorState.selectedTrack && entry.clip === EditorState.selectedClip) {
                found = entry
                break
            }
        }
        box = found
        if (dragging || !found)
            return

        let x = clipData.objectTrackX !== undefined ? clipData.objectTrackX : 0.5
        let y = clipData.objectTrackY !== undefined ? clipData.objectTrackY : 0.5
        if (clipData.hasObjectTrack && !clipData.objectTrackStale) {
            const sample = EditorState.objectTrackSample(EditorState.selectedTrack, EditorState.selectedClip)
            if (sample && sample.valid) {
                x = sample.x
                y = sample.y
            }
        }
        nx = x
        ny = y
        radius = clipData.objectTrackRadius !== undefined ? clipData.objectTrackRadius : 0.12
        placeRing()
    }

    function placeRing() {
        if (!box || !frame || !ring)
            return
        const rad = Math.max(8, radius * frame.width)
        ring.x = nx * frame.width - rad
        ring.y = ny * frame.height - rad
    }

    function commit() {
        dragging = false
        if (!videoSelected)
            return
        const x = Math.max(0, Math.min(1, nx))
        const y = Math.max(0, Math.min(1, ny))
        const r = Math.max(0.03, Math.min(0.4, radius))
        nx = x
        ny = y
        radius = r
        EditorState.setObjectTrackSeed(EditorState.selectedTrack, EditorState.selectedClip, x, y, r)
    }

    Component.onCompleted: refresh()

    Connections {
        target: EditorState
        function onPlayheadSecondsChanged() { root.refresh() }
        function onSelectionChanged() { root.refresh() }
        function onSelectedClipDataChanged() { if (!root.dragging) root.refresh() }
        function onTracksChanged() { if (!root.dragging) root.refresh() }
    }

    onWidthChanged: refresh()
    onHeightChanged: refresh()

    visible: videoSelected && box && box.width > 2 && box.height > 2
             && !EditorState.playing && !EditorState.scrubbing
             && !EditorState.canvasCropMode && !EditorState.maskEditActive
             && EditorState.guideEditSetId === ""

    Item {
        id: frame
        x: root.box ? root.box.x * root.sx : 0
        y: root.box ? root.box.y * root.sy : 0
        width: root.box ? root.box.width * root.sx : 0
        height: root.box ? root.box.height * root.sy : 0
        onWidthChanged: if (!root.dragging) root.placeRing()
        onHeightChanged: if (!root.dragging) root.placeRing()
        rotation: (root.box && !posed) ? root.box.rotation : 0
        transformOrigin: Item.Center

        readonly property bool posed: !!root.box && (root.box.parentActive || root.box.layer3d
                                                     || root.box.cameraActive)

        transform: Matrix4x4 {
            matrix: frame.posed && root.box
                    ? EditorState.previewClipPoseMatrix({
                                                            "canvasWidth": root.box.canvasWidth,
                                                            "canvasHeight": root.box.canvasHeight,
                                                            "rotationX": root.box.rotationX,
                                                            "rotationY": root.box.rotationY,
                                                            "z": root.box.z,
                                                            "perspective": root.box.perspective,
                                                            "parentActive": root.box.parentActive,
                                                            "parent": root.box.parent
                                                        }, root.box.x, root.box.y, root.box.width,
                                                        root.box.height, root.box.rotation,
                                                        root.sx, root.sy)
                    : Qt.matrix4x4()
        }

        readonly property real rad: Math.max(8, root.radius * width)

        Rectangle {
            id: ring
            width: frame.rad * 2
            height: frame.rad * 2
            radius: width / 2
            color: "transparent"
            border.width: 2
            border.color: Theme.primary

            Rectangle {
                anchors.centerIn: parent
                width: 6
                height: 6
                radius: 3
                color: Theme.primary
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.SizeAllCursor
                preventStealing: true
                drag.target: ring
                drag.minimumX: -frame.rad
                drag.maximumX: frame.width - frame.rad
                drag.minimumY: -frame.rad
                drag.maximumY: frame.height - frame.rad
                onPressed: root.dragging = true
                onPositionChanged: {
                    if (!root.dragging)
                        return
                    root.nx = (ring.x + frame.rad) / Math.max(1, frame.width)
                    root.ny = (ring.y + frame.rad) / Math.max(1, frame.height)
                }
                onReleased: root.commit()
                onCanceled: root.commit()
            }
        }

        Rectangle {
            id: radiusHandle
            width: 12
            height: 12
            radius: 2
            color: Theme.primary
            x: ring.x + ring.width - width / 2
            y: ring.y + ring.height / 2 - height / 2

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.SizeHorCursor
                preventStealing: true
                onPressed: root.dragging = true
                onPositionChanged: (mouse) => {
                    if (!root.dragging)
                        return
                    const local = mapToItem(frame, mouse.x, mouse.y)
                    const cx = root.nx * frame.width
                    const cy = root.ny * frame.height
                    root.radius = Math.max(0.03, Math.min(0.4,
                                                          Math.abs(local.x - cx) / Math.max(1, frame.width)))
                    const rad = Math.max(8, root.radius * frame.width)
                    ring.x = cx - rad
                    ring.y = cy - rad
                }
                onReleased: root.commit()
                onCanceled: root.commit()
            }
        }
    }
}
