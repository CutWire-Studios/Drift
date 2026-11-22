pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes
import Drift

// Draws the motion trackers over the target clip's frame: each point's pattern and search boxes and the
// onion-skin path of its track, and a planar tracker's outline. Also takes the clicks that place new
// trackers.
//
// Grips are HandleKnobs: the pattern box and the search box resize about the marker (one size for the
// tracker), a planar tracker's corners move. The marker itself is dragged from anywhere in its box. A
// drag on a frame that is already tracked writes a user key there, drawn in the user-key colour, and
// later tracking continues from it. A drag is one undo step and Esc puts it back through
// EditorState.drag. Shift or Ctrl extends the selection, a drag on empty space marquees the markers,
// arrows nudge the selected markers (Shift = 10) and Delete removes the tracker. A right-click or a
// long press opens Re-track from here. On a finger the context bar carries the placement chips and the
// track transport, and a badge on the box shows the confidence.
ManipOverlay {
    id: root

    property bool touch: Theme.touchInput
    // The preview canvas the pattern loupe magnifies.
    property Item canvas

    // A pointer drag is running; ManipOverlay.interacting is the drag session's, which a nudge also opens.
    property bool dragging: false
    property bool bodyMoved: false

    refreshPolicy: "none"
    focus: true
    nudgeEnabled: canEdit && markers.selectionCount > 0
    nudgeText: qsTr("Nudge tracker")
    hint: hintText()
    touchBar: touch ? touchBarComponent : null

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
    readonly property real hitSide: touch ? Theme.androidMinTouchTarget : 24

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
        if (!visible || dragging)
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
        let tracker = list.find(t => t.id === ShellBridge.selectedTrackerId)
        // The phone has no Tracking panel to choose one, so the overlay keeps one selected.
        if (touch && hasClip && !tracker) {
            tracker = list.length > 0 ? list[list.length - 1] : undefined
            ShellBridge.selectedTrackerId = tracker ? tracker.id : ""
        }
        minConfidence = tracker ? tracker.minConfidence : 0.5
    }

    function refreshFrame() {
        if (!visible || dragging)
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
        if (planarDraft.nearFirst(press, touch ? Theme.androidMinTouchTarget / 2 : 8)) {
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

    function deviceOf(mouse) {
        return mouse.source !== Qt.MouseEventNotSynthesized ? DragSession.Touch : DragSession.Mouse
    }

    function undoTextFor(kind) {
        switch (kind) {
        case "corner": return qsTr("Move tracking outline corner")
        case "pattern": return qsTr("Resize pattern box")
        case "search": return qsTr("Resize search box")
        }
        return qsTr("Move tracking point")
    }

    // p is in the clip frame's own pixels.
    function beginDrag(kind, index, p, extend, device) {
        if (!markers.beginDrag(kind, index, p.x, p.y, extend))
            return false
        dragging = true
        bodyMoved = false
        root.pointer = clipFrame.mapToItem(root, p.x, p.y)
        EditorState.drag.begin("tracker", device, undoTextFor(kind))
        root.forceActiveFocus()
        return true
    }

    function hudLabel() {
        const kind = markers.dragKind
        if (kind === "corner") {
            const c = markers.cornerAt(markers.dragIndex)
            return qsTr("Corner %1   X %2 %   Y %3 %").arg(markers.dragIndex + 1)
                .arg((c.x / clipFrame.width * 100).toFixed(1)).arg((c.y / clipFrame.height * 100).toFixed(1))
        }
        const info = markers.pointInfo(markers.dragIndex)
        if (!info.shown)
            return ""
        if (kind === "pattern")
            return qsTr("Pattern %1 %").arg((info.pw / clipFrame.width * 100).toFixed(1))
        if (kind === "search")
            return qsTr("Search %1 %").arg((info.sw / clipFrame.width * 100).toFixed(1))
        return pointLabel(info)
    }

    function pointLabel(info) {
        const text = qsTr("X %1 %   Y %2 %   Box %3 %").arg((info.nx * 100).toFixed(1)).arg((info.ny * 100).toFixed(1))
                         .arg((info.boxW * 100).toFixed(1))
        return info.has ? text + "   " + qsTr("Confidence %1 %").arg(Math.round(info.confidence * 100)) : text
    }

    function moveDrag(p, modifiers) {
        if (!dragging)
            return
        bodyMoved = true
        root.pointer = clipFrame.mapToItem(root, p.x, p.y)
        markers.dragTo(p.x, p.y, modifiersFor(modifiers).constrain)
        EditorState.drag.update(hudLabel())
    }

    // The session commits before the write so a drag that writes on release (corner, box size) is one
    // undo step rather than two; a marker move is a preview edit the commit turns into its step.
    function endDrag() {
        if (!dragging)
            return
        EditorState.drag.commit()
        markers.endDrag()
        dragging = false
        refreshFrame()
    }

    function revertDrag() {
        if (!dragging)
            return
        markers.cancelDrag()
        dragging = false
        marquee.active = false
        refreshFrame()
    }

    function deleteTracker() {
        if (!canEdit)
            return
        const tracker = EditorState.tracking.trackers(clipId).find(t => t.id === trackerId)
        if (tracker && tracker.targets.length > 0)
            Toasts.info(qsTr("This tracker drives other clips. Remove it from the Tracking panel to choose what happens to their keys."))
        else
            EditorState.tracking.removeTracker(clipId, trackerId, false)
    }

    // Tracking runs from the playhead, so this is the frame the drag corrected.
    function retrack(forward) {
        if (hasTracker && !busy)
            EditorState.tracking.track(clipId, trackerId, forward, false)
    }

    function stepTrack(forward) {
        if (hasTracker && !busy)
            EditorState.tracking.track(clipId, trackerId, forward, true)
    }

    function shownMarker() {
        for (let i = 0; i < markers.pointCount; ++i) {
            const info = markers.pointInfo(i)
            if (info.shown && info.selected)
                return i
        }
        return -1
    }

    onNudged: (dx, dy) => markers.nudge(dx, dy)
    onConfirmed: {
        if (placeMode === "planar")
            finishPlanar()
    }
    onEscaped: {
        if (placeMode !== "")
            ShellBridge.trackerPlaceMode = ""
        else
            markers.clearSelection()
    }
    onUnhandledKey: (event) => {
        if (event.key === Qt.Key_Backspace && placeMode === "planar") {
            planarDraft.undo()
            event.accepted = true
        } else if ((event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) && canEdit) {
            deleteTracker()
            event.accepted = true
        }
    }

    Connections {
        target: EditorState.drag
        function onCancelled() { root.revertDrag() }
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
        if (touch)
            return trackerCount === 0 ? qsTr("Pick Point or Planar, then tap the feature to track")
                                      : qsTr("Drag a marker to correct it, then track from this frame")
        return trackerCount === 0 ? qsTr("Use Point or Planar in the Tracking panel to start a track")
                                  : qsTr("Drag a marker to correct it · Right-click to re-track from here · ◀ ▶ in the Tracking panel to track")
    }

    onVisibleChanged: if (visible) refresh()
    onPlaceModeChanged: {
        if (placeMode === "planar")
            forceActiveFocus()
        else
            planarDraft.clear()
    }

    Component.onCompleted: {
        refresh()
        if (touch && hasClip && trackerCount === 0 && placeMode === "")
            ShellBridge.trackerPlaceMode = "point"
    }
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
                                     || root.editorState.anchorX !== undefined || root.editorState.anchorY !== undefined
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
            id: placeArea
            anchors.fill: parent
            enabled: root.hasClip && (root.placeMode === "point" || root.placeMode === "addPoint" || root.placeMode === "follow")
            onWheel: (wheel) => { wheel.accepted = false }
            onClicked: (mouse) => root.placeClick(mouse)
        }

        ToolCursor {
            target: placeArea
            shape: "trackPoint"
        }

        ToolCursor {
            target: planarArea
            shape: planarDraft.hoverNearFirst ? "polygonClose" : "trackPlanar"
        }

        MouseArea {
            id: planarArea
            anchors.fill: parent
            enabled: root.hasClip && root.placeMode === "planar"
            hoverEnabled: true
            onWheel: (wheel) => { wheel.accepted = false }
            onPositionChanged: (mouse) => planarDraft.moveHover(Qt.point(mouse.x, mouse.y), false)
            onPressed: (mouse) => {
                root.forceActiveFocus()
                root.planarPress(mouse.x, mouse.y)
            }
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
            patternColor: Theme.c.trackerOverlay.pattern
            trailColor: Theme.c.trackerOverlay.trail
            goodColor: Theme.c.trackerOverlay.good
            warnColor: Theme.c.trackerOverlay.warn
            badColor: Theme.c.trackerOverlay.bad
            userKeyColor: Theme.c.trackerOverlay.userKey
            regionColor: Theme.c.trackerOverlay.region
            searchColor: Theme.c.trackerOverlay.search
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
                startX: planarDraft.first.x
                startY: planarDraft.first.y
                strokeColor: Theme.c.trackerOverlay.region
                strokeWidth: Theme.c.maskDrawOverlay.borderWidth
                fillColor: "transparent"

                PathPolyline {
                    path: planarDraft.points
                }
            }

            ShapePath {
                startX: planarDraft.last.x
                startY: planarDraft.last.y
                strokeColor: Theme.c.trackerOverlay.region
                strokeWidth: Theme.c.maskDrawOverlay.borderWidth
                strokeStyle: ShapePath.DashLine
                dashPattern: [4, 3]
                fillColor: "transparent"

                PathLine {
                    x: planarDraft.hover.x
                    y: planarDraft.hover.y
                }
            }
        }

        MouseArea {
            id: body
            anchors.fill: parent
            enabled: root.canEdit
            hoverEnabled: true
            preventStealing: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            cursorShape: over ? Qt.SizeAllCursor : Qt.ArrowCursor
            onWheel: (wheel) => { wheel.accepted = false }

            property bool over: false
            property bool menuPress: false

            onPositionChanged: (mouse) => {
                if (!pressed) {
                    over = markers.hitTest(mouse.x, mouse.y, root.hitSide).index >= 0
                    return
                }
                if (root.dragging)
                    root.moveDrag(Qt.point(mouse.x, mouse.y), mouse.modifiers)
                else if (marquee.active)
                    marquee.to(mouse.x, mouse.y)
            }
            onPressed: (mouse) => {
                const hit = markers.hitTest(mouse.x, mouse.y, root.hitSide)
                menuPress = false
                if (mouse.button === Qt.RightButton) {
                    if (hit.index < 0) {
                        mouse.accepted = false
                        return
                    }
                    menuPress = true
                    root.forceActiveFocus()
                    markers.beginDrag("point", hit.index, mouse.x, mouse.y, false)
                    markers.cancelDrag()
                    contextMenu.openAt(mouse.x, mouse.y)
                    return
                }
                const extend = (mouse.modifiers & (Qt.ShiftModifier | Qt.ControlModifier)) !== 0
                if (hit.index >= 0) {
                    root.forceActiveFocus()
                    root.beginDrag("point", hit.index, Qt.point(mouse.x, mouse.y), extend, root.deviceOf(mouse))
                } else if (markers.pointCount > 1) {
                    root.forceActiveFocus()
                    if (!extend)
                        markers.clearSelection()
                    marquee.begin(mouse.x, mouse.y, extend)
                } else {
                    markers.clearSelection()
                    mouse.accepted = false
                }
            }
            onReleased: (mouse) => {
                if (marquee.active)
                    marquee.active = false
                if (menuPress)
                    return
                root.endDrag()
            }
            onCanceled: {
                marquee.active = false
                root.endDrag()
            }
            onPressAndHold: (mouse) => {
                if (!root.dragging || root.bodyMoved || root.deviceOf(mouse) === DragSession.Mouse)
                    return
                EditorState.drag.cancel()
                contextMenu.openAt(mouse.x, mouse.y)
            }
        }

        Rectangle {
            id: marquee
            property bool active: false
            property bool extend: false
            property point origin
            visible: active
            border.width: Theme.c.timelineMarqueeRect.borderWidth
            border.color: Theme.c.timelineMarqueeRect.border
            color: Qt.alpha(Theme.c.timelineMarqueeRect.background, Theme.c.timelineMarqueeRect.backgroundOpacity)

            function begin(x, y, add) {
                origin = Qt.point(x, y)
                extend = add
                this.x = x
                this.y = y
                width = 0
                height = 0
                active = true
            }

            function to(x, y) {
                this.x = Math.min(origin.x, x)
                this.y = Math.min(origin.y, y)
                width = Math.abs(x - origin.x)
                height = Math.abs(y - origin.y)
                markers.selectInRect(this.x, this.y, this.x + width, this.y + height, extend)
            }
        }

        Repeater {
            model: root.canEdit && !EditorState.playing ? markers.pointCount : 0

            delegate: Item {
                id: pointGrips
                anchors.fill: parent
                required property int index
                readonly property var info: { markers.revision; return markers.pointInfo(index) }
                readonly property bool live: !!info.shown && !!info.selected
                readonly property real clipRotation: root.editorState.rotation || 0

                HandleKnob {
                    visible: pointGrips.live && pointGrips.info.sw > pointGrips.info.pw + 1
                    shape: "diamond"
                    cursor: "resize:" + (45 + pointGrips.clipRotation)
                    x: (pointGrips.info.x || 0) + (pointGrips.info.sw || 0) / 2 - width / 2
                    y: (pointGrips.info.y || 0) + (pointGrips.info.sh || 0) / 2 - height / 2
                    onPressed: (point, modifiers, device) => root.beginDrag("search", pointGrips.index, point, false, device)
                    onMoved: (point, modifiers) => root.moveDrag(point, modifiers)
                    onReleased: root.endDrag()
                }

                HandleKnob {
                    visible: pointGrips.live
                    shape: "square"
                    cursor: "resize:" + (45 + pointGrips.clipRotation)
                    x: (pointGrips.info.x || 0) + (pointGrips.info.pw || 0) / 2 - width / 2
                    y: (pointGrips.info.y || 0) + (pointGrips.info.ph || 0) / 2 - height / 2
                    onPressed: (point, modifiers, device) => root.beginDrag("pattern", pointGrips.index, point, false, device)
                    onMoved: (point, modifiers) => root.moveDrag(point, modifiers)
                    onReleased: root.endDrag()
                }
            }
        }

        Repeater {
            model: root.canEdit && !EditorState.playing ? markers.cornerCount : 0

            HandleKnob {
                required property int index
                readonly property point corner: { markers.revision; return markers.cornerAt(index) }
                shape: "square"
                cursor: Qt.SizeAllCursor
                x: corner.x - width / 2
                y: corner.y - height / 2
                onPressed: (point, modifiers, device) => root.beginDrag("corner", index, point, false, device)
                onMoved: (point, modifiers) => root.moveDrag(point, modifiers)
                onReleased: root.endDrag()
            }
        }

        Repeater {
            model: root.canEdit && !EditorState.playing ? markers.pointCount : 0

            delegate: Rectangle {
                id: badge
                required property int index
                readonly property var info: { markers.revision; return markers.pointInfo(index) }
                readonly property color tint: info.tint === "good" ? Theme.c.trackerOverlay.good
                                              : info.tint === "bad" ? Theme.c.trackerOverlay.bad
                                              : Theme.c.trackerOverlay.warn
                visible: root.touch && !!info.shown && !!info.has
                x: (info.x || 0) + (info.pw || 0) / 2 - width
                y: (info.y || 0) - (info.ph || 0) / 2 - height - Theme.spacingXs
                width: badgeLabel.implicitWidth + Theme.spacingMd * 2
                height: badgeLabel.implicitHeight + Theme.spacingXs * 2
                radius: Theme.radiusPill
                color: tint
                border.width: Theme.c.maskDrawOverlay.borderWidth
                border.color: Theme.c.trackerOverlay.badgeBackground

                Text {
                    id: badgeLabel
                    anchors.centerIn: parent
                    text: (badge.info.tint === "bad" ? "! " : "") + Math.round((badge.info.confidence || 0) * 100) + "%"
                    color: Theme.c.trackerOverlay.badgeBackground
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
            }
        }

        Rectangle {
            id: status
            readonly property int marker: { markers.revision; return root.shownMarker() }
            readonly property var info: marker >= 0 ? markers.pointInfo(marker) : ({})
            visible: root.canEdit && !EditorState.playing && !root.dragging && marker >= 0 && !!info.shown
            x: Math.max(0, Math.min(clipFrame.width - width, (info.x || 0) - (info.pw || 0) / 2))
            y: Math.min(clipFrame.height - height, (info.y || 0) + Math.max(info.ph || 0, info.sh || 0) / 2 + Theme.spacingXs)
            width: statusLabel.implicitWidth + Theme.spacingMd * 2
            height: statusLabel.implicitHeight + Theme.spacingXs * 2
            radius: Theme.radiusXs
            color: Theme.c.trackerOverlay.badgeBackground

            Text {
                id: statusLabel
                anchors.centerIn: parent
                text: status.visible ? root.pointLabel(status.info) : ""
                color: Theme.c.trackerOverlay.badgeForeground
                font.family: Theme.monoFontFamily
                font.pixelSize: Theme.fontSizeXs
            }
        }

        Rectangle {
            visible: root.busy
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: Theme.spacingSm
            z: 10
            width: badgeRow.width + Theme.spacingMd * 2
            height: badgeRow.height + Theme.spacingXs * 2
            radius: Theme.radiusXs
            color: Theme.c.trackerOverlay.badgeBackground

            Row {
                id: badgeRow
                anchors.centerIn: parent
                spacing: Theme.spacingSm

                Text {
                    id: badgeText
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Tracking… %1%").arg(Math.round(EditorState.tracking.progress * 100))
                    color: Theme.c.trackerOverlay.badgeForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: Theme.icons.x
                    tooltip: qsTr("Stop tracking (Esc)")
                    buttonSize: Theme.iconButtonSize * 0.75
                    iconSize: Theme.iconSizeSm
                    onClicked: ShellBridge.stopTracking()
                }
            }
        }
    }

    ThemedContextMenu {
        id: contextMenu

        function openAt(x, y) {
            const p = clipFrame.mapToItem(root, x, y)
            popup(root, p.x, p.y)
        }

        ThemedMenuItem {
            text: qsTr("Re-track forward from here")
            icon.name: Theme.icons.skipForward
            enabled: root.hasTracker && !root.busy
            onTriggered: root.retrack(true)
        }

        ThemedMenuItem {
            text: qsTr("Re-track backward from here")
            icon.name: Theme.icons.skipBack
            enabled: root.hasTracker && !root.busy
            onTriggered: root.retrack(false)
        }

        ThemedMenuItem {
            text: qsTr("Delete tracker")
            icon.name: Theme.icons.trash
            enabled: root.canEdit
            onTriggered: root.deleteTracker()
        }
    }

    // The pattern region magnified: above the finger while one drags, pinned in the corner on a pointer.
    readonly property var loupeFocus: {
        markers.revision
        if (markers.dragKind === "corner")
            return markers.cornerAt(markers.dragIndex)
        const i = markers.dragIndex >= 0 ? markers.dragIndex : shownMarker()
        const info = i >= 0 ? markers.pointInfo(i) : null
        return info && info.shown ? Qt.point(info.x, info.y) : null
    }
    readonly property real loupeBox: {
        markers.revision
        const i = markers.dragIndex >= 0 ? markers.dragIndex : shownMarker()
        const info = i >= 0 ? markers.pointInfo(i) : null
        return info ? Math.max(info.pw, 12) : 24
    }

    Loupe {
        source: root.canvas
        at: root.pointer
        sourcePoint: root.loupeFocus && root.canvas
                     ? root.mapToItem(root.canvas, clipFrame.mapToItem(root, root.loupeFocus.x, root.loupeFocus.y).x,
                                      clipFrame.mapToItem(root, root.loupeFocus.x, root.loupeFocus.y).y)
                     : Qt.point(0, 0)
        zoom: Math.max(1.5, Math.min(8, diameter * 0.7 / root.loupeBox))
        active: root.touch && root.touchDrag && root.dragging && root.loupeFocus !== null
    }

    Loupe {
        source: root.canvas
        diameter: 120
        x: root.width - width - Theme.spacingLg
        y: root.height - height - Theme.spacingLg
        sourcePoint: root.loupeFocus && root.canvas
                     ? root.mapToItem(root.canvas, clipFrame.mapToItem(root, root.loupeFocus.x, root.loupeFocus.y).x,
                                      clipFrame.mapToItem(root, root.loupeFocus.x, root.loupeFocus.y).y)
                     : Qt.point(0, 0)
        zoom: Math.max(1.5, Math.min(8, diameter * 0.7 / root.loupeBox))
        active: !root.touch && root.canEdit && !EditorState.playing && root.loupeFocus !== null && root.canvas !== null
    }

    Component {
        id: touchBarComponent

        Column {
            id: bars
            visible: root.hasClip && root.hasFrame
            width: root.width
            spacing: Theme.spacingSm

            TouchContextBar {
                x: (bars.width - width) / 2
                chips: []
                visible: !EditorState.tracking.busy

                ThemedChip {
                    anchors.verticalCenter: parent.verticalCenter
                    chipHeight: Theme.androidMinTouchTarget
                    text: qsTr("Point")
                    selected: root.placeMode === "point"
                    tooltip: qsTr("Tap the feature to follow")
                    onClicked: ShellBridge.trackerPlaceMode = root.placeMode === "point" ? "" : "point"
                }

                ThemedChip {
                    anchors.verticalCenter: parent.verticalCenter
                    chipHeight: Theme.androidMinTouchTarget
                    text: qsTr("Planar")
                    selected: root.placeMode === "planar"
                    tooltip: qsTr("Tap the corners of a flat surface")
                    onClicked: ShellBridge.trackerPlaceMode = root.placeMode === "planar" ? "" : "planar"
                }

                ThemedChip {
                    visible: root.placeMode === "planar" && planarDraft.count >= 4
                    anchors.verticalCenter: parent.verticalCenter
                    chipHeight: Theme.androidMinTouchTarget
                    text: qsTr("Done")
                    onClicked: root.finishPlanar()
                }

                IconButton {
                    visible: root.canEdit
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: Theme.icons.trash
                    buttonSize: Theme.androidMinTouchTarget
                    tooltip: qsTr("Delete tracker")
                    onClicked: root.deleteTracker()
                }
            }

            TouchContextBar {
                x: (bars.width - width) / 2
                chips: []
                visible: root.hasTracker

                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: Theme.icons.skipBack
                    buttonSize: Theme.androidMinTouchTarget
                    tooltip: qsTr("Track back to the start")
                    tooltipActionId: "tracking.trackBackward"
                    enabled: !EditorState.tracking.busy
                    onClicked: root.retrack(false)
                }

                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: Theme.icons.stepBack
                    buttonSize: Theme.androidMinTouchTarget
                    tooltip: qsTr("Track one frame back")
                    tooltipActionId: "tracking.trackFrameBackward"
                    enabled: !EditorState.tracking.busy
                    onClicked: root.stepTrack(false)
                }

                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: Theme.icons.x
                    buttonSize: Theme.androidMinTouchTarget
                    tooltip: qsTr("Stop tracking (Esc)")
                    enabled: EditorState.tracking.busy
                    onClicked: ShellBridge.stopTracking()
                }

                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: Theme.icons.stepForward
                    buttonSize: Theme.androidMinTouchTarget
                    tooltip: qsTr("Track one frame forward")
                    tooltipActionId: "tracking.trackFrameForward"
                    enabled: !EditorState.tracking.busy
                    onClicked: root.stepTrack(true)
                }

                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: Theme.icons.skipForward
                    buttonSize: Theme.androidMinTouchTarget
                    tooltip: qsTr("Track to the end")
                    tooltipActionId: "tracking.trackForward"
                    enabled: !EditorState.tracking.busy
                    onClicked: root.retrack(true)
                }
            }
        }
    }
}
