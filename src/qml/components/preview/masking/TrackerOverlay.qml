pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes
import Drift

// Draws the motion trackers over the target clip's frame: each point's pattern box and trail, and
// a planar tracker's outline. Also takes the clicks that place new trackers.
Item {
    id: root

    property bool interacting: false

    // Snapshot from PreviewController::maskingFrame(): the selection's frame, and whether it is under the
    // playhead. Nothing is placed or accepted while it is not.
    property var editorState: ({})
    readonly property bool hasFrame: !!editorState.hasFrame
    property var target: ({ kind: "none" })
    readonly property bool hasClip: target.kind === "clip"
    readonly property string trackerId: ShellBridge.selectedTrackerId
    readonly property bool hasTracker: hasClip && trackerId !== ""
    readonly property bool busy: EditorState.tracking.busy
    readonly property string placeMode: ShellBridge.trackerPlaceMode
    readonly property bool canEdit: hasTracker && !busy && placeMode === ""

    property string clipId: ""
    property int trackerCount: 0
    property real minConfidence: 0.5

    // Planar placement: corners in clip-frame pixels, in the order clicked.
    PolylineDraft {
        id: planarDraft
        minPoints: 4
    }

    readonly property real canvasW: Math.max(1, editorState.canvasWidth || 1)
    readonly property real canvasH: Math.max(1, editorState.canvasHeight || 1)
    readonly property real sx: root.width / canvasW
    readonly property real sy: root.height / canvasH

    function clamp01(v) {
        return Math.max(0, Math.min(1, v))
    }

    function normAt(mx, my) {
        return { "x": clamp01(mx / Math.max(1, clipFrame.width)), "y": clamp01(my / Math.max(1, clipFrame.height)) }
    }

    function refresh() {
        if (!visible || interacting)
            return
        target = ShellBridge.trackerTarget()
        clipId = hasClip ? EditorState.masking.clipIdAt(target.track, target.clip) : ""
        if (!EditorState.playing)
            editorState = ShellBridge.trackerFrame(target)
        refreshTracker()
        refreshFrame()
    }

    function refreshTracker() {
        const list = hasClip ? EditorState.tracking.trackers(clipId) : []
        trackerCount = list.length
        const tracker = list.find(t => t.id === ShellBridge.selectedTrackerId)
        minConfidence = tracker ? tracker.minConfidence : 0.5
    }

    function refreshFrame() {
        if (!visible || interacting)
            return
        markers.seconds = EditorState.playheadSeconds
        markers.refresh()
    }

    function placeClick(mouse) {
        const n = normAt(mouse.x, mouse.y)
        if (hasClip) {
            if (placeMode === "point") {
                const id = EditorState.tracking.addPointTracker(clipId, n.x, n.y)
                if (id !== "")
                    ShellBridge.selectedTrackerId = id
                else
                    Toasts.info(qsTr("Trackers need a video clip"))
            } else if (placeMode === "follow") {
                const id = EditorState.tracking.followWithNewTracker(clipId, ShellBridge.trackerFollowerId, n.x, n.y)
                if (id !== "") {
                    ShellBridge.selectedTrackerId = id
                    ShellBridge.trackBoth(clipId, id)
                } else {
                    Toasts.info(qsTr("Trackers need a video clip"))
                }
            } else if (placeMode === "addPoint") {
                EditorState.tracking.addTrackerPoint(clipId, ShellBridge.trackerPlaceId, n.x, n.y)
            }
        }
        ShellBridge.trackerPlaceMode = ""
    }

    function planarPress(mx, my) {
        const press = Qt.point(mx, my)
        if (planarDraft.nearFirst(press, 8)) {
            finishPlanar()
            return
        }
        planarDraft.add(press, false)
    }

    function finishPlanar() {
        if (planarDraft.count < 4 || !hasClip)
            return
        const id = EditorState.tracking.addPlanarTracker(clipId, planarDraft.normalized(clipFrame.width, clipFrame.height, true))
        ShellBridge.trackerPlaceMode = ""
        if (id !== "")
            ShellBridge.selectedTrackerId = id
        else
            Toasts.info(qsTr("Trackers need a video clip"))
    }

    function endDrag() {
        if (!interacting)
            return
        interacting = false
        if (markers.endDrag() === "point")
            EditorState.commitPreviewDrag()
        refreshFrame()
    }

    function hintText() {
        if (!hasClip)
            return qsTr("Select a clip on the timeline to track motion in it")
        if (!editorState.inside)
            return qsTr("Move the playhead over the selected clip")
        switch (placeMode) {
        case "point":
            return qsTr("Click the feature to track on the preview.")
        case "addPoint":
            return qsTr("Click the feature to add to this tracker.")
        case "follow":
            return qsTr("Click the object the clip should follow.")
        case "planar":
            return planarDraft.count === 0
                   ? qsTr("Click the corners of a flat surface, Enter to finish.")
                   : qsTr("%1 corners · Enter to finish · Backspace to undo · Esc to cancel").arg(planarDraft.count)
        }
        return trackerCount === 0 ? qsTr("Use Point or Planar in the Tracking panel to start a track")
                                  : qsTr("Drag a marker to correct it · ◀ ▶ in the Tracking panel to track")
    }

    onVisibleChanged: if (visible) refresh()
    onPlaceModeChanged: {
        if (placeMode === "planar")
            forceActiveFocus()
        else
            planarDraft.clear()
    }

    Component.onCompleted: refresh()
    Component.onDestruction: ShellBridge.trackerPlaceMode = ""

    Connections {
        target: EditorState
        function onTracksChanged() { root.refresh() }
        function onSelectionChanged() { root.refresh() }
        function onSelectedClipDataChanged() { root.refresh() }
        function onClipPropertiesPreviewed() { root.refresh() }
        // Playback is refreshed by the timer below, not per tick.
        function onPlayheadSecondsChanged() {
            if (EditorState.playing)
                return
            root.refreshFrame()
            if (!EditorState.scrubbing)
                root.refresh()
        }
        function onScrubbingChanged() {
            if (!EditorState.scrubbing)
                root.refresh()
        }
        function onPlayingChanged() {
            if (!EditorState.playing)
                root.refresh()
        }
    }

    // Playback refreshes at 10 Hz rather than per tick: the frame and tracker are heavy to resolve.
    Timer {
        interval: 100
        repeat: true
        running: root.visible && EditorState.playing
        onTriggered: {
            root.editorState = ShellBridge.trackerFrame(root.target)
            root.refreshFrame()
        }
    }

    Connections {
        target: EditorState.tracking
        function onSamplesUpdated(sampleTrackerId) {
            if (sampleTrackerId === ShellBridge.selectedTrackerId)
                root.refreshFrame()
        }
        function onTrackersChanged() { root.refresh() }
    }

    Connections {
        target: ShellBridge
        function onSelectedTrackerIdChanged() { root.refresh() }
        function onTrackerHostIdChanged() { root.refresh() }
    }

    // Keys reach this item only while it has focus, which the planar tool asks for.
    focus: placeMode === "planar"
    Keys.onPressed: (event) => {
        switch (event.key) {
        case Qt.Key_Return:
        case Qt.Key_Enter:
            root.finishPlanar()
            event.accepted = true
            break
        case Qt.Key_Escape:
            ShellBridge.trackerPlaceMode = ""
            event.accepted = true
            break
        case Qt.Key_Backspace:
            planarDraft.undo()
            event.accepted = true
            break
        }
    }

    // The clip's destination rect on the canvas, rotated with it, as MaskDrawOverlay places it.
    Item {
        id: clipFrame
        visible: root.hasFrame
        x: (root.editorState.x || 0) * root.sx
        y: (root.editorState.y || 0) * root.sy
        width: Math.max(1, (root.editorState.width || root.canvasW) * root.sx)
        height: Math.max(1, (root.editorState.height || root.canvasH) * root.sy)
        transformOrigin: Item.Center
        // A tilted clip frame goes through the clip's perspective instead of the flat spin.
        readonly property bool is3d: (root.editorState.rotationX || 0) !== 0
                                     || (root.editorState.rotationY || 0) !== 0
                                     || (root.editorState.z || 0) !== 0
        // Moved by a transform layer: composed through that parent as well, flat or not.
        readonly property bool hasParent: root.editorState.parentActive === true
        rotation: is3d || hasParent ? 0 : (root.editorState.rotation || 0)
        transform: Matrix4x4 {
            matrix: clipFrame.is3d || clipFrame.hasParent
                    ? EditorState.preview.clipPoseMatrix(root.editorState, root.editorState.x || 0,
                                                        root.editorState.y || 0,
                                                        root.editorState.width || root.canvasW,
                                                        root.editorState.height || root.canvasH,
                                                        root.editorState.rotation || 0,
                                                        root.sx, root.sy)
                    : Qt.matrix4x4()
        }

        MaskOverlayClipBounds { }

        MouseArea {
            anchors.fill: parent
            enabled: root.hasClip && (root.placeMode === "point" || root.placeMode === "addPoint" || root.placeMode === "follow")
            cursorShape: Qt.CrossCursor
            onWheel: (wheel) => { wheel.accepted = false }
            onClicked: (mouse) => root.placeClick(mouse)
        }

        MouseArea {
            anchors.fill: parent
            enabled: root.hasClip && root.placeMode === "planar"
            hoverEnabled: true
            cursorShape: Qt.CrossCursor
            onWheel: (wheel) => { wheel.accepted = false }
            onPositionChanged: (mouse) => planarDraft.moveHover(Qt.point(mouse.x, mouse.y), false)
            onPressed: (mouse) => root.planarPress(mouse.x, mouse.y)
            // The second press of a double-click is ignored by planarPress, so this only has to close.
            onDoubleClicked: root.finishPlanar()
        }

        TrackerOverlayItem {
            id: markers
            anchors.fill: parent
            tracking: EditorState.tracking
            clipId: root.clipId
            trackerId: root.hasTracker ? root.trackerId : ""
            minConfidence: root.minConfidence
            showTrails: !EditorState.playing
            lineWidth: Theme.c.maskDrawOverlay.borderWidth
            handleSize: Theme.spacingLg
            handleMargin: Theme.spacingMd
            patternColor: Theme.c.trackerOverlay.pattern
            trailColor: Theme.c.trackerOverlay.trail
            goodColor: Theme.c.trackerOverlay.good
            warnColor: Theme.c.trackerOverlay.warn
            badColor: Theme.c.trackerOverlay.bad
            userKeyColor: Theme.c.trackerOverlay.userKey
            regionColor: Theme.c.trackerOverlay.region
            handleColor: Theme.c.trackerOverlay.handle
        }

        Repeater {
            model: root.placeMode === "planar" ? planarDraft.points : []

            delegate: Rectangle {
                required property point modelData
                required property int index
                readonly property real side: index === 0 ? 11 : 7
                readonly property bool closing: index === 0 && planarDraft.hoverNearFirst

                x: modelData.x - side / 2
                y: modelData.y - side / 2
                width: side
                height: side
                color: closing ? Theme.c.trackerOverlay.good : Theme.c.trackerOverlay.region
            }
        }

        Shape {
            anchors.fill: parent
            visible: root.placeMode === "planar" && planarDraft.count > 0

            ShapePath {
                strokeColor: Theme.c.trackerOverlay.region
                strokeWidth: Theme.c.maskDrawOverlay.borderWidth
                fillColor: "transparent"

                PathPolyline {
                    path: planarDraft.points
                }
            }

            ShapePath {
                strokeColor: Theme.c.trackerOverlay.region
                strokeWidth: Theme.c.maskDrawOverlay.borderWidth
                strokeStyle: ShapePath.DashLine
                dashPattern: [4, 3]
                fillColor: "transparent"

                PathMove {
                    x: planarDraft.last.x
                    y: planarDraft.last.y
                }
                PathLine {
                    x: planarDraft.hover.x
                    y: planarDraft.hover.y
                }
            }
        }

        MouseArea {
            anchors.fill: parent
            enabled: root.canEdit
            hoverEnabled: true
            preventStealing: true
            cursorShape: over ? Qt.SizeAllCursor : Qt.ArrowCursor
            onWheel: (wheel) => { wheel.accepted = false }

            property bool over: false

            onPositionChanged: (mouse) => {
                if (!pressed) {
                    over = markers.hitTest(mouse.x, mouse.y).index >= 0
                    return
                }
                if (root.interacting)
                    markers.dragTo(mouse.x, mouse.y)
            }
            onPressed: (mouse) => {
                if (markers.beginDrag(mouse.x, mouse.y))
                    root.interacting = true
                else
                    mouse.accepted = false
            }
            onReleased: root.endDrag()
            onCanceled: root.endDrag()
        }

        Rectangle {
            visible: root.busy
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: Theme.spacingSm
            width: badgeText.width + Theme.spacingMd * 2
            height: badgeText.height + Theme.spacingXs * 2
            radius: Theme.radiusXs
            color: Theme.c.trackerOverlay.badgeBackground

            Text {
                id: badgeText
                anchors.centerIn: parent
                text: qsTr("Tracking… %1%").arg(Math.round(EditorState.tracking.progress * 100))
                color: Theme.c.trackerOverlay.badgeForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
        }
    }

    Rectangle {
        visible: hintLabel.text !== ""
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spacingLg
        width: hintLabel.width + Theme.spacingLg * 2
        height: hintLabel.height + Theme.spacingMd * 2
        radius: Theme.c.maskDrawOverlay.radius
        color: Theme.c.maskDrawOverlay.hintBackground

        Text {
            id: hintLabel
            anchors.centerIn: parent
            text: root.hintText()
            color: Theme.c.maskDrawOverlay.hintForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }
}
