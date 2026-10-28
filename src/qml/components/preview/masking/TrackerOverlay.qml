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

    // TrackingController::trackerFrame at the playhead, for the selected tracker.
    property var frame: ({})
    readonly property var framePoints: frame.points || []
    readonly property var frameCorners: frame.region || []
    readonly property var frameTrails: frame.trail || []
    property int trackerCount: 0
    property real minConfidence: 0.5

    // While a drag runs, the dragged item's position comes from here. The delegates keep their
    // identity, so the mouse grab survives the controller's refreshes.
    property var pointDrag: null
    property var cornerDrag: null

    // Planar placement, as normalized corners in the order clicked.
    property var planarPoints: []
    property point planarHover: Qt.point(0, 0)

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

    function toPx(p) {
        return { "x": p.x * clipFrame.width, "y": p.y * clipFrame.height }
    }

    function distance(a, b) {
        return Math.hypot(a.x - b.x, a.y - b.y)
    }

    function pointAt(i) {
        const d = pointDrag
        return d && d.index === i ? d : framePoints[i]
    }

    function cornerAt(i) {
        const d = cornerDrag
        return d && d.index === i ? d : frameCorners[i]
    }

    function tintFor(confidence, minC) {
        if (confidence < minC)
            return Theme.c.trackerOverlay.bad
        if (confidence >= 0.8)
            return Theme.c.trackerOverlay.good
        return Theme.c.trackerOverlay.warn
    }

    function trailPixels(i) {
        // A list nested in the C++ map arrives as a sequence wrapper without Array methods.
        return Array.from(frameTrails[i] || [], p => toPx(p))
    }

    function outlinePixels() {
        const pts = frameCorners.map((c, i) => toPx(cornerAt(i)))
        if (pts.length > 0)
            pts.push(pts[0])
        return pts
    }

    function planarPixels() {
        return planarPoints.map(p => toPx(p))
    }

    function lastPlanarPx() {
        return planarPoints.length > 0 ? toPx(planarPoints[planarPoints.length - 1]) : ({ "x": 0, "y": 0 })
    }

    function nearFirst() {
        return planarPoints.length >= 4 && distance(planarHover, toPx(planarPoints[0])) <= 8
    }

    function refresh() {
        if (!visible || interacting)
            return
        target = ShellBridge.trackerTarget()
        if (!EditorState.playing)
            editorState = ShellBridge.trackerFrame(target)
        refreshTracker()
        refreshFrame()
    }

    function refreshTracker() {
        const list = hasClip ? EditorState.tracking.trackers(target.track, target.clip) : []
        trackerCount = list.length
        const tracker = list.find(t => t.id === ShellBridge.selectedTrackerId)
        minConfidence = tracker ? tracker.minConfidence : 0.5
    }

    function refreshFrame() {
        if (!visible || interacting)
            return
        const id = ShellBridge.selectedTrackerId
        frame = hasClip && id !== "" ? EditorState.tracking.trackerFrame(target.track, target.clip, id,
                                                                         EditorState.playheadSeconds)
                                     : ({})
    }

    function placeClick(mouse) {
        const n = normAt(mouse.x, mouse.y)
        const t = ShellBridge.trackerTarget()
        if (t.kind === "clip") {
            if (placeMode === "point") {
                const id = EditorState.tracking.addPointTracker(t.track, t.clip, n.x, n.y)
                if (id !== "")
                    ShellBridge.selectedTrackerId = id
                else
                    Toasts.info(qsTr("Trackers need a video clip"))
            } else if (placeMode === "addPoint") {
                EditorState.tracking.addTrackerPoint(t.track, t.clip, ShellBridge.trackerPlaceId, n.x, n.y)
            }
        }
        ShellBridge.trackerPlaceMode = ""
    }

    function planarPress(mx, my) {
        const press = { "x": mx, "y": my }
        if (planarPoints.length >= 4 && distance(press, toPx(planarPoints[0])) <= 8) {
            finishPlanar()
            return
        }
        // A double-click's second press lands on the vertex just placed; it must not add a duplicate.
        if (planarPoints.length > 0 && distance(press, toPx(planarPoints[planarPoints.length - 1])) < 3)
            return
        planarPoints = planarPoints.concat([normAt(mx, my)])
    }

    function finishPlanar() {
        if (planarPoints.length < 4 || !hasClip)
            return
        const t = ShellBridge.trackerTarget()
        const id = t.kind === "clip" ? EditorState.tracking.addPlanarTracker(t.track, t.clip, planarPoints) : ""
        ShellBridge.trackerPlaceMode = ""
        if (id !== "")
            ShellBridge.selectedTrackerId = id
        else
            Toasts.info(qsTr("Trackers need a video clip"))
    }

    function endPointDrag() {
        if (!interacting)
            return
        const moved = pointDrag !== null
        interacting = false
        if (moved)
            EditorState.commitPreviewDrag()
        refreshFrame()
        pointDrag = null
    }

    function endCornerDrag() {
        if (!interacting)
            return
        if (cornerDrag !== null) {
            const region = frameCorners.map((c, i) => ({ "x": cornerAt(i).x, "y": cornerAt(i).y }))
            EditorState.tracking.correctPlanarCorners(target.track, target.clip, ShellBridge.selectedTrackerId, region)
        }
        interacting = false
        refreshFrame()
        cornerDrag = null
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
        case "planar":
            return planarPoints.length === 0
                   ? qsTr("Click the corners of a flat surface, Enter to finish.")
                   : qsTr("%1 corners · Enter to finish · Backspace to undo · Esc to cancel").arg(planarPoints.length)
        }
        return trackerCount === 0 ? qsTr("Use Point or Planar in the Tracking panel to start a track")
                                  : qsTr("Drag a marker to correct it · ◀ ▶ in the Tracking panel to track")
    }

    onVisibleChanged: if (visible) refresh()
    onPlaceModeChanged: {
        if (placeMode === "planar")
            forceActiveFocus()
        else
            planarPoints = []
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
            root.planarPoints = root.planarPoints.slice(0, -1)
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
            enabled: root.hasClip && (root.placeMode === "point" || root.placeMode === "addPoint")
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
            onPositionChanged: (mouse) => root.planarHover = Qt.point(mouse.x, mouse.y)
            onPressed: (mouse) => root.planarPress(mouse.x, mouse.y)
            // The second press of a double-click is ignored by planarPress, so this only has to close.
            onDoubleClicked: root.finishPlanar()
        }

        Repeater {
            model: root.frameTrails.length

            delegate: Shape {
                required property int index
                anchors.fill: parent
                visible: !EditorState.playing

                ShapePath {
                    strokeColor: Qt.alpha(Theme.c.trackerOverlay.trail, 0.6)
                    strokeWidth: Theme.c.maskDrawOverlay.borderWidth
                    fillColor: "transparent"

                    PathPolyline {
                        path: root.trailPixels(index)
                    }
                }
            }
        }

        Shape {
            anchors.fill: parent
            visible: root.frameCorners.length >= 3

            ShapePath {
                strokeColor: Theme.c.trackerOverlay.region
                strokeWidth: Theme.c.maskDrawOverlay.borderWidth
                fillColor: Qt.alpha(Theme.c.trackerOverlay.region, 0.12)

                PathPolyline {
                    path: root.outlinePixels()
                }
            }
        }

        Repeater {
            model: root.framePoints.length

            delegate: Item {
                id: pointItem
                required property int index
                readonly property var pt: root.framePoints[index] ?? ({})
                readonly property var at: root.pointAt(index) ?? ({})
                readonly property real cx: at.x * clipFrame.width
                readonly property real cy: at.y * clipFrame.height
                readonly property real pw: pt.patternW * clipFrame.width
                readonly property real ph: pt.patternH * clipFrame.height
                visible: pt.has === true || pt.ghost === true
                opacity: pt.has === true ? 1.0 : 0.6
                x: cx - pw / 2
                y: cy - ph / 2
                width: pw
                height: ph

                Rectangle {
                    anchors.fill: parent
                    color: "transparent"
                    border.width: Theme.c.maskDrawOverlay.borderWidth
                    border.color: pointItem.pt.has === true
                                  ? root.tintFor(pointItem.pt.confidence, root.minConfidence)
                                  : Theme.c.trackerOverlay.warn
                }

                Rectangle {
                    x: pointItem.pw / 2 - 6
                    y: pointItem.ph / 2 - 0.5
                    width: 12
                    height: 1
                    color: Theme.c.trackerOverlay.pattern
                }

                Rectangle {
                    x: pointItem.pw / 2 - 0.5
                    y: pointItem.ph / 2 - 6
                    width: 1
                    height: 12
                    color: Theme.c.trackerOverlay.pattern
                }

                Rectangle {
                    visible: pointItem.pt.userKey === true
                    x: pointItem.pw / 2 - 3.5
                    y: pointItem.ph / 2 - 3.5
                    width: 7
                    height: 7
                    rotation: 45
                    color: Theme.c.trackerOverlay.userKey
                }

                MouseArea {
                    anchors.centerIn: parent
                    width: Math.max(24, pointItem.pw)
                    height: Math.max(24, pointItem.ph)
                    enabled: root.canEdit
                    cursorShape: Qt.SizeAllCursor
                    onWheel: (wheel) => { wheel.accepted = false }
                    preventStealing: true

                    property point grab: Qt.point(0, 0)

                    onPressed: (mouse) => {
                        root.interacting = true
                        const p = mapToItem(clipFrame, mouse.x, mouse.y)
                        grab = Qt.point(p.x - pointItem.cx, p.y - pointItem.cy)
                    }
                    onPositionChanged: (mouse) => {
                        if (!root.interacting)
                            return
                        const p = mapToItem(clipFrame, mouse.x, mouse.y)
                        const n = root.normAt(p.x - grab.x, p.y - grab.y)
                        root.pointDrag = { "index": pointItem.index, "x": n.x, "y": n.y }
                        EditorState.tracking.previewSetTrackerPoint(root.target.track, root.target.clip,
                                                                    ShellBridge.selectedTrackerId,
                                                                    pointItem.index, n.x, n.y)
                    }
                    onReleased: root.endPointDrag()
                    onCanceled: root.endPointDrag()
                }
            }
        }

        Repeater {
            model: root.placeMode === "planar" ? root.planarPoints : []

            delegate: Rectangle {
                required property var modelData
                required property int index
                readonly property real side: index === 0 ? 11 : 7
                readonly property bool closing: index === 0 && root.nearFirst()

                x: modelData.x * clipFrame.width - side / 2
                y: modelData.y * clipFrame.height - side / 2
                width: side
                height: side
                color: closing ? Theme.c.trackerOverlay.good : Theme.c.trackerOverlay.region
            }
        }

        Shape {
            anchors.fill: parent
            visible: root.placeMode === "planar" && root.planarPoints.length > 0

            ShapePath {
                strokeColor: Theme.c.trackerOverlay.region
                strokeWidth: Theme.c.maskDrawOverlay.borderWidth
                fillColor: "transparent"

                PathPolyline {
                    path: root.planarPixels()
                }
            }

            ShapePath {
                strokeColor: Theme.c.trackerOverlay.region
                strokeWidth: Theme.c.maskDrawOverlay.borderWidth
                strokeStyle: ShapePath.DashLine
                dashPattern: [4, 3]
                fillColor: "transparent"

                PathMove {
                    x: root.lastPlanarPx().x
                    y: root.lastPlanarPx().y
                }
                PathLine {
                    x: root.planarHover.x
                    y: root.planarHover.y
                }
            }
        }

        Repeater {
            model: root.frameCorners.length

            delegate: Rectangle {
                id: corner
                required property int index
                readonly property var at: root.cornerAt(index) ?? ({})

                width: Theme.spacingLg
                height: Theme.spacingLg
                x: at.x * clipFrame.width - width / 2
                y: at.y * clipFrame.height - height / 2
                color: Theme.c.trackerOverlay.handle

                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -Theme.spacingMd
                    enabled: root.canEdit
                    cursorShape: Qt.SizeAllCursor
                    onWheel: (wheel) => { wheel.accepted = false }
                    preventStealing: true

                    onPressed: root.interacting = true
                    onPositionChanged: (mouse) => {
                        if (!root.interacting)
                            return
                        const p = mapToItem(clipFrame, mouse.x, mouse.y)
                        const n = root.normAt(p.x, p.y)
                        root.cornerDrag = { "index": corner.index, "x": n.x, "y": n.y }
                    }
                    onReleased: root.endCornerDrag()
                    onCanceled: root.endCornerDrag()
                }
            }
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
