import QtQuick
import QtQuick.Shapes
import Drift

// Trim strip on one clip edge. `edge` is -1 for the start and 1 for the end.
// Position (left or right anchor) is set by the clip.
Rectangle {
    id: handle

    required property var owner
    property int edge: -1

    readonly property bool areaContainsMouse: trimMouse.containsMouse
    readonly property bool areaPressed: trimMouse.pressed
    readonly property bool areaHovered: trimHover.hovered
    readonly property bool hot: areaContainsMouse || areaHovered || areaPressed
    // A looping clip's edges add or remove repeats instead of trimming, so they look different.
    readonly property bool looping: !!owner.clipData.loop

    // The hit strip keeps its width; only the drawn bracket reacts to hover. On touch the strip
    // is the finger-sized body, with the same bracket drawn on its outer edge.
    width: owner.trimHandleWidth
    radius: looping ? width / 2 : owner.touchMode ? Theme.c.timelineTrimHandle.radius : 0
    color: !owner.showTrimHandles ? "transparent"
           : looping ? Theme.c.timelineTrimHandle.loopBackground
           : owner.touchMode ? Qt.alpha(Theme.c.timelineTrimHandle.background, 0.55)
                             : "transparent"

    readonly property color accent: Theme.c.timelineTrimHandle.background
    property real barW: owner.touchMode ? 4 : (hot ? 4 : 3)
    readonly property real serifLen: owner.touchMode ? Math.max(barW + 2, width - 1) : 5
    readonly property bool drawBracket: owner.showTrimHandles && !looping

    Behavior on barW {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }

    Item {
        id: bracket
        anchors.fill: parent
        visible: handle.drawBracket
        opacity: handle.hot ? 1.0 : 0.9

        Behavior on opacity {
            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }

        Rectangle {
            x: handle.edge < 0 ? 0 : parent.width - width
            width: handle.barW
            height: parent.height
            color: handle.accent
        }
        Repeater {
            model: 2
            delegate: Rectangle {
                required property int index
                x: handle.edge < 0 ? 0 : parent.width - width
                y: index === 0 ? 0 : parent.height - height
                width: handle.serifLen
                height: handle.barW
                color: handle.accent
            }
        }

        Shape {
            id: chevron
            readonly property real cw: 4
            readonly property real ch: 9
            width: cw
            height: ch
            x: handle.edge < 0 ? handle.barW + 2 : parent.width - handle.barW - 2 - width
            anchors.verticalCenter: parent.verticalCenter
            visible: !owner.touchMode && parent.height >= 30
            opacity: handle.hot ? 1 : 0
            preferredRendererType: Shape.CurveRenderer

            Behavior on opacity {
                NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
            }

            ShapePath {
                strokeColor: handle.accent
                strokeWidth: 1.5
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                startX: handle.edge < 0 ? chevron.cw : 0
                startY: 0
                PathLine { x: handle.edge < 0 ? 0 : chevron.cw; y: chevron.ch / 2 }
                PathLine { x: handle.edge < 0 ? chevron.cw : 0; y: chevron.ch }
            }
        }
    }

    TimelineTrimGrip {
        visible: owner.touchMode && owner.showTrimHandles && !handle.looping
    }

    IconGlyph {
        anchors.centerIn: parent
        visible: handle.looping && owner.showTrimHandles && handle.width >= 8 && parent.height >= 14
        glyph: Theme.icons.repeat
        iconSize: Math.min(handle.width - 2, 12)
        iconColor: Theme.c.timelineTrimHandle.loopForeground
    }

    Loader {
        anchors.fill: parent
        active: owner.tooltipsActive
        sourceComponent: Component {
            ThemedToolTip {
                text: handle.looping ? qsTr("Drag to repeat the clip")
                      : handle.edge < 0 ? qsTr("Drag to trim the start")
                                        : qsTr("Drag to trim the end")
                visible: owner.showTrimHandles
                         && (handle.areaContainsMouse || handle.areaHovered)
                         && !handle.areaPressed
            }
        }
    }

    MouseArea {
        id: trimMouse
        anchors.fill: parent
        anchors.leftMargin: handle.edge < 0 ? -owner.trimHotspotExtra : -4
        anchors.rightMargin: handle.edge > 0 ? -owner.trimHotspotExtra : -4
        anchors.bottomMargin: -6
        // Same reason as the move drag: these are ~38px strips at both edges of
        // every clip and they hold the grab, so on touch they turned each clip
        // boundary into another place the timeline could not be panned. Only the
        // selected clip — the one actually showing trim handles — arms them.
        // A clip too narrow to show handles has no trim; mid-gesture the shrinking clip must not
        // pull the area out from under the drag.
        enabled: (owner.width >= Theme.clipControlsMinWidth || pressed)
                 && (!owner.touchMode || owner.showTrimHandles)
        preventStealing: true
        hoverEnabled: true
        cursorShape: Qt.BlankCursor

        HoverHandler {
            id: trimHover
            enabled: trimMouse.enabled
            cursorShape: Qt.BlankCursor
        }

        // Last position this edge was actually committed at, in whole pixels. A high-polling
        // mouse delivers several positionChanged per frame, and each one used to mutate the
        // project and rebuild the whole timeline model. Same idiom the move drag uses above.
        property int lastTrimPx: -2147483647
        // Where the edge was last asked to go. Negative means the gesture never moved it,
        // so there is nothing to commit.
        property real lastTrimSeconds: -1
        // Set while this handle owns an open preview drag, so the drag is closed exactly
        // once however the gesture ends.
        property bool trimming: false
        // Edge minus finger, in trackRow pixels, taken at press: the touch band reaches well
        // outside the edge, so without it the edge would snap under the finger on the first move.
        property real grabOffset: 0
        property real fingerSceneX: 0
        property real fingerSceneY: 0

        function endTrimLock() {
            if (typeof owner.panel.setScrollLocked === "function")
                owner.panel.setScrollLocked(false)
        }

        function applyTrim() {
            const start = owner.clipData.start || 0
            const end = start + (owner.clipData.duration || 0)
            const local = trimMouse.mapFromItem(null, fingerSceneX, fingerSceneY)
            const fingerX = trimMouse.mapToItem(owner.trackRow, local.x, local.y).x
            const raw = (fingerX + grabOffset) / owner.panel.pxPerSecond
            const target = handle.edge < 0
                    ? Math.max(0, Math.min(raw, end - owner.minDurationSeconds))
                    : Math.max(raw, start + owner.minDurationSeconds)
            const px = Math.round(target * owner.panel.pxPerSecond)
            if (px === lastTrimPx)
                return
            lastTrimPx = px
            lastTrimSeconds = target
            // Previewed, not applied. The reply reports what the trim would do -- snapping
            // and every limit that can stop this edge live inside it, and the one the user
            // needs told, the source running out, has no cue on screen at all.
            const p = handle.edge < 0
                    ? EditorState.previewTrimLeft(owner.trackIndex, owner.clipIndex, target)
                    : EditorState.previewTrimRight(owner.trackIndex, owner.clipIndex, target)
            // The engine applies the same snap target to audio and video edges. Show the
            // vertical ruler across every track while this edge is magnetically aligned.
            const edgeSeconds = handle.edge < 0 ? p.start : p.start + p.duration
            owner.panel.snapGuideSeconds = p.ok && EditorState.snapEnabled
                    && p.outcome === 2 && Math.abs(edgeSeconds - target) > 0.0005
                    ? edgeSeconds : -1
            if (p.ok && p.changed) {
                owner.trimPreviewStart = p.start
                owner.trimPreviewDuration = p.duration
                owner.trimPreviewIn = p.inPoint
                owner.trimPreviewOut = p.outPoint
                owner.trimPreviewActive = true
                owner.panel.setTrimFollow(owner.clipData.linkId, owner.clipData.id,
                                         p.start, p.duration, p.inPoint, p.outPoint,
                                         p.rippleIds, p.rippleDelta)
            }
            Haptics.trimStep(p.ok ? p.outcome : 0)
        }

        // Same edge autoscroll as a lifted clip: the pane is a few seconds wide, so an edge could
        // otherwise never be pulled past what is on screen.
        Timer {
            interval: 16
            repeat: true
            running: trimMouse.trimming && owner.edgeAxis !== 2
                     && typeof owner.panel.dragEdgeScroll === "function"

            onTriggered: {
                const panel = owner.panel
                const local = trimMouse.mapFromItem(null, trimMouse.fingerSceneX,
                                                    trimMouse.fingerSceneY)
                const px = trimMouse.mapToItem(owner.trackRow, local.x, local.y).x
                         - panel.timelineViewX
                const edgeX = Math.min(48, panel.timelineViewW * 0.25)
                let dx = 0
                if (px < edgeX)
                    dx = -Math.min(24, 6 + (edgeX - px) * 0.4)
                else if (px > panel.timelineViewW - edgeX)
                    dx = Math.min(24, 6 + (px - (panel.timelineViewW - edgeX)) * 0.4)
                if (dx === 0)
                    return
                const applied = panel.dragEdgeScroll(dx, 0)
                if (applied.x !== 0)
                    trimMouse.applyTrim()
            }
        }

        onPressed: (mouse) => {
            Qt.callLater(function() { owner.forceActiveFocus() })
            const wasSelected = owner.selected
            Haptics.reset()
            owner.beginEdgeGesture(trimMouse, mouse)
            if (!owner.selected)
                EditorState.selectClip(owner.trackIndex, owner.clipIndex)
            if (!wasSelected)
                Haptics.select()
            // One undo entry and one dirty mark for the whole drag. Without this a trim was
            // invisible to both: it never moved the undo stack index, and the dirty flag is
            // only ever set from that index changing — so trimming, closing, and being asked
            // nothing about saving lost the work outright.
            EditorState.beginPreviewDrag(qsTr("Trim clip"))
            EditorState.beginTrimGesture(owner.trackIndex, owner.clipIndex, handle.edge)
            lastTrimPx = -2147483647
            lastTrimSeconds = -1
            owner.panel.snapGuideSeconds = -1
            trimming = true
            const cs = owner.clipData.start || 0
            const edgeX = (handle.edge < 0 ? cs : cs + (owner.clipData.duration || 0))
                          * owner.panel.pxPerSecond
            grabOffset = edgeX - mapToItem(owner.trackRow, mouse.x, mouse.y).x
            const scene = mapToItem(null, mouse.x, mouse.y)
            fingerSceneX = scene.x
            fingerSceneY = scene.y
            if (EditorState.playing)
                EditorState.playing = false
            if (typeof owner.panel.setScrollLocked === "function")
                owner.panel.setScrollLocked(true)
        }
        onPositionChanged: (mouse) => {
            if (!pressed)
                return
            // A vertical drag on this strip is someone reaching for another layer, not for
            // this clip's edge.
            if (owner.edgeGestureScrolled(trimMouse, mouse))
                return
            const scene = mapToItem(null, mouse.x, mouse.y)
            fingerSceneX = scene.x
            fingerSceneY = scene.y
            applyTrim()
        }
        onReleased: {
            endTrimLock()
            Haptics.reset()
            owner.panel.snapGuideSeconds = -1
            if (trimming) {
                trimming = false
                // The one and only edit of the gesture. Dropping the preview first so the
                // geometry bindings are reading the project again by the time it lands.
                owner.trimPreviewActive = false
                owner.panel.clearTrimFollow()
                // One call, so the edit, the undo push and finishEdit announce the timeline
                // once between them rather than three times.
                EditorState.commitTrim(owner.trackIndex, owner.clipIndex,
                                       handle.edge, lastTrimSeconds)
            }
        }
        onCanceled: {
            endTrimLock()
            Haptics.reset()
            owner.panel.snapGuideSeconds = -1
            if (trimming) {
                trimming = false
                owner.trimPreviewActive = false
                owner.panel.clearTrimFollow()
                EditorState.endTrimGesture()
                EditorState.cancelPreviewDrag()
            }
        }
    }
}
