import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

// A single clip on a timeline track: background/fade canvas, filmstrip or
// waveform, name/effects band, drag-to-move, trim handles and fade dots, plus
// the clip context menu. Positioning and edits are delegated back to the panel
// (pxPerSecond, snap/landing helpers, trackIndexAtY) and to EditorState.
Item {
    id: clipItem

    // Owning TimelinePanel (pxPerSecond, clipColor, trackIndexAtY, landing
    // preview + effect-drop state, tracks) and the enclosing column.
    //
    // trackRow is whatever Item this delegate is parented to — the track row itself, or the
    // clip-area / adjustment-lane wrapper inside it. Sizing off `parent` is what lets a clip in
    // a nested lane fit its strip without knowing lanes exist.
    //
    // Because this property exists, `trackRow` inside a binding AT THE CALL SITE resolves to it
    // and NOT to an enclosing `id: trackRow` — so a call site must never write
    // `trackIndex: trackRow.trackIndex`. It reads as the row's index and silently yields 0 when
    // the immediate parent is a wrapper, which renders every track's clips as track 0's. Bind
    // through the wrapper's own id instead (see trackClipArea / laneStrip in TimelinePanel).
    property var panel
    readonly property var trackRow: parent
    property var timelineColumn
    // Filled by Repeater when used as a delegate (AOT-safe).
    required property int index
    property int trackIndex
    property int clipIndex: index
    // Wider trim/move hit areas for phones; desktop leaves this false.
    property bool touchMode: false

    // Desktop timeline deletion is intentionally scoped to the clip that owns
    // keyboard focus. Clicking a clip already calls forceActiveFocus(), so this
    // does not steal Delete from the Media Bin, dialogs or other editor surfaces.
    //
    // macOS sends the key labelled Delete on MacBook keyboards as Backspace;
    // extended keyboards can also send the forward-delete Key_Delete.
    //
    // deleteSelectedClip() owns the A/V semantics:
    //   linked pair   -> delete the whole linked set
    //   unlinked pair -> delete only the current selection
    Keys.onPressed: function(event) {
        if ((event.key === Qt.Key_Delete
                || event.key === Qt.Key_Backspace)
                && clipItem.selected) {

            EditorState.deleteSelectedClip()
            event.accepted = true
        }
    }

    property var clipData: panel.tracks[trackIndex].clips[clipIndex]
    property bool selected: (EditorState.selection,
                             EditorState.selectionContains(trackIndex, clipIndex))
    property string trackType: panel.tracks[trackIndex].type
    property bool showWaveform: panel.tracks[trackIndex].showWaveform === true
    property bool showChannelWaveforms: panel.tracks[trackIndex].showChannelWaveforms === true
    property var clipEffects: clipData.effects || []
    property var clipAudioEffects: clipData.audioEffects || []
    readonly property bool hasAnyEffects: clipEffects.length > 0 || clipAudioEffects.length > 0
    readonly property string effectsLabelText: {
        const names = []
        for (var i = 0; i < clipEffects.length; i++) {
            const fx = clipEffects[i]
            const label = fx.label || qsTr("Effect")
            names.push(fx.enabled === false ? qsTr("%1 (off)").arg(label) : label)
        }
        for (var j = 0; j < clipAudioEffects.length; j++) {
            const afx = clipAudioEffects[j]
            const alabel = afx.label || qsTr("Effect")
            names.push(afx.enabled === false ? qsTr("%1 (off)").arg(alabel) : alabel)
        }
        return names.join(" · ")
    }
    // An adjustment is its stack — the name ("Adjustment Layer") says nothing the colour and
    // lane do not already. So it shows the effects it carries, and falls back to naming its kind
    // only while it is still empty.
    readonly property string adjustmentLabelText: {
        if (clipItem.effectsLabelText.length > 0)
            return clipItem.effectsLabelText
        // Nothing in it yet: a name the user gave it, else what kind of layer it is.
        if (clipItem.clipData.name && clipItem.clipData.name.length > 0)
            return clipItem.clipData.name
        const kind = clipItem.clipData.adjustmentKind
        if (kind === "audioEffects") return qsTr("Audio adjustment")
        if (kind === "mask") return qsTr("Mask")
        return qsTr("Adjustment")
    }

    // Named so tooling (and a screen reader) can address a clip by what the user sees on it
    // rather than by pixel position. The timeline is the app's main interaction surface and had
    // no accessible identity at all.
    Accessible.role: Accessible.Button
    Accessible.name: {
        const label = clipItem.clipData.name && clipItem.clipData.name.length > 0
                    ? clipItem.clipData.name
                    : clipItem.adjustmentLabelText
        return qsTr("%1, track %2").arg(label).arg(clipItem.trackIndex + 1)
    }
    Accessible.selected: clipItem.selected
    Accessible.onPressAction: EditorState.selectClip(clipItem.trackIndex, clipItem.clipIndex)

    property bool effectDropTarget: panel.effectDropTrackIndex === trackIndex
                                    && panel.effectDropClipIndex === clipIndex
    // Subtitles keep cue-owned timing; text clips use the same edge fades as video.
    readonly property bool timelineFadeHandles: trackType !== "subtitle"

    // Gain along a fade ramp (progress 0..1). Mirrors Clip::shapeFade / FadeShape.
    function fadeGainAt(progress) {
        const t = Math.max(0, Math.min(1, progress))
        const curve = clipItem.clipData.fadeCurve || "smooth"
        if (curve === "linear")
            return t
        if (curve === "equalPower")
            return Math.sin(t * Math.PI * 0.5)
        if (curve === "bezier") {
            // Mirrors FadeShape::bezierAt — anchors pinned at (0,0)/(1,1), solve x for t.
            const h = clipItem.clipData.fadeHandles || [0.42, 0.0, 0.58, 1.0]
            let lo = 0, hi = 1
            for (let i = 0; i < 24; ++i) {
                const mid = (lo + hi) / 2
                const mt = 1 - mid
                const x = 3 * mt * mt * mid * h[0] + 3 * mt * mid * mid * h[2] + mid * mid * mid
                if (x < t) lo = mid; else hi = mid
            }
            const u = (lo + hi) / 2
            const mu = 1 - u
            return 3 * mu * mu * u * h[1] + 3 * mu * u * u * h[3] + u * u * u
        }
        if (curve === "custom") {
            const pts = clipItem.clipData.fadeShape || []
            if (pts.length < 2)
                return t
            if (t <= pts[0].t)
                return pts[0].g
            if (t >= pts[pts.length - 1].t)
                return pts[pts.length - 1].g
            for (let i = 0; i + 1 < pts.length; ++i) {
                const a = pts[i]
                const b = pts[i + 1]
                if (t < a.t || t > b.t)
                    continue
                const span = b.t - a.t
                if (Math.abs(span) < 1e-9)
                    return b.g
                const u = (t - a.t) / span
                return a.g + (b.g - a.g) * u
            }
            return pts[pts.length - 1].g
        }
        // smooth (smoothstep)
        return t * t * (3.0 - 2.0 * t)
    }

    // Premiere-style trim pointer (vertical bar + arrow), sized to this clip.
    readonly property int trimCursorSide: leftTrimMouse.containsMouse ? -1
                                          : rightTrimMouse.containsMouse ? 1 : 0
    readonly property int trimCursorHeight: Math.round(height)
    function applyTrimCursor() {
        EditorState.setTimelineTrimCursor(trimCursorSide, trimCursorHeight)
    }
    onTrimCursorSideChanged: applyTrimCursor()
    onTrimCursorHeightChanged: if (trimCursorSide !== 0) applyTrimCursor()
    Component.onDestruction: {
        if (trimCursorSide !== 0)
            EditorState.setTimelineTrimCursor(0, 0)
        if (lifted && typeof panel.setScrollLocked === "function")
            panel.setScrollLocked(false)
    }

    // An adjustment pinned to a clip takes its extent from that clip, so its edges are not the
    // user's to drag — unlink it first and they become live.
    readonly property bool pinnedToClip: clipData.kind === "adjustment"
                                         && !!clipData.linkedClipId

    // Trim handles stay on whenever selected.
    // Width is floored so the clip never becomes
    // an unusable sliver; at that floor both
    // edges stay trimmable and the middle moves.
    readonly property bool showTrimHandles: selected && !pinnedToClip
    readonly property real minDurationSeconds: Math.max(
        Theme.clipMinDurationSeconds,
        Theme.clipMinWidth / panel.pxPerSecond)
    readonly property real trimHandleWidth: touchMode
                                            ? Theme.androidClipTrimHandleWidth
                                            : Theme.clipTrimHandleWidth
    // Clamped against the clip's own width: a minimum-width (56dp) clip inset by a
    // fixed 22dp each side left a 12dp movable core, and the edge bands are dead
    // until the clip is selected — so short clips could not be tapped at all.
    readonly property real edgeMargin: touchMode
                                       ? Math.min(Theme.androidClipEdgeMargin, width / 4)
                                       : 14
    readonly property real trimHotspotExtra: touchMode
                                             ? Theme.androidTrimHotspotExtra
                                             : 10

    // --- Trim strips vs. scrolling the layers ---------------------------------------------
    // On touch each trim strip is ~38dp wide and runs the full height of the clip, at both
    // edges, and it holds the grab: MouseArea copies preventStealing into stealMouse once, in
    // mousePressEvent, so nothing that starts on a strip can ever be handed back to the
    // Flickable. It has to hold it, or the Flickable would steal a horizontal trim mid-drag.
    //
    // The cost is that those two bands cover the clip you are working on, which is exactly
    // where a finger lands when it goes to scroll to another layer — and with the timeline
    // pane only a couple of rows tall, scrolling is not a rare thing to want. So the strips
    // arbitrate instead of refusing: the first movement past the drag threshold decides
    // whether the gesture is a trim or a scroll, and a scroll drives the timeline for the rest
    // of the gesture. Whichever it picks, it keeps for the whole press, so a trim cannot turn
    // into a scroll halfway through a frame-accurate drag.
    //
    // Scene coordinates throughout. Scrolling moves this item under a finger that has not
    // moved, so in local coordinates the pointer would appear to travel and feed the scroll
    // back into itself.
    property int edgeAxis: 0 // 0 undecided, 1 trimming, 2 scrolling
    property real edgePressSceneX: 0
    property real edgePressSceneY: 0
    property real edgeLastSceneY: 0

    function beginEdgeGesture(area, mouse) {
        const p = area.mapToItem(null, mouse.x, mouse.y)
        // Desktop has a pointer and a scroll wheel and never needed this; starting it already
        // decided keeps every branch below dead there.
        edgeAxis = touchMode ? 0 : 1
        edgePressSceneX = p.x
        edgePressSceneY = p.y
        edgeLastSceneY = p.y
    }

    // True when this move belongs to the timeline rather than to the strip, in which case it
    // has already been applied and the caller must not also trim.
    function edgeGestureScrolled(area, mouse) {
        if (edgeAxis === 1)
            return false
        const p = area.mapToItem(null, mouse.x, mouse.y)
        if (edgeAxis === 0) {
            const dx = Math.abs(p.x - edgePressSceneX)
            const dy = Math.abs(p.y - edgePressSceneY)
            const slop = Qt.styleHints.startDragDistance
            // Vertical has to both clear the threshold and beat horizontal: a trim is the
            // reason these strips exist, so anything ambiguous stays a trim.
            if (dy > slop && dy > dx)
                edgeAxis = 2
            else if (dx > slop)
                edgeAxis = 1
            if (edgeAxis !== 2)
                return false
            // Re-anchored at the moment it arms, so the view does not jump by the threshold
            // that was spent deciding.
            edgeLastSceneY = p.y
        }
        // Shares the drag autoscroll's mover, which already clamps to the content bounds --
        // writing contentY directly goes around boundsBehavior. Android-only, like it.
        if (typeof panel.dragEdgeScroll === "function")
            panel.dragEdgeScroll(0, edgeLastSceneY - p.y)
        edgeLastSceneY = p.y
        return true
    }

    // Fade dots. On touch they used to be switched off outright: at the clip corners
    // they landed inside the trim strips, which own the full height of both edges.
    // Instead they are sized to a fingertip and inset past those strips, and — like the
    // trim handles — only appear on the selected clip. Desktop keeps the 13px corner
    // dots and a zero inset, so nothing there moves.
    readonly property real fadeHandleSize: touchMode ? 22 : 13
    // Clears the trim strip's own hotspot (trimHandleWidth + 4) plus the dot's -6
    // hit margin, so the two never contend for the same press.
    readonly property real fadeHandleInset: touchMode ? trimHandleWidth + 12 : 0
    readonly property real fadeHandleMinWidth: touchMode
                                               ? 2 * fadeHandleInset + fadeHandleSize + 12
                                               : 26

    // Name band height, derived once instead of
    // being hardcoded at three separate sites,
    // and clamped so it can never swallow a
    // short (25px) text or subtitle row.
    // One line now: the effect list that used to need a taller band moved to the lane.
    readonly property real headerBandHeight:
        Math.min(Theme.clipHeaderBandHeight, Math.max(0, height * 0.5))

    y: Theme.clipSelectionRingWidth
    // Floored so short clips stay visible and
    // trimmable even at low zoom.
    width: Math.max(Theme.clipMinWidth,
                    clipData.duration * panel.pxPerSecond
                    - 2 * Theme.clipSelectionRingWidth)
    height: Math.max(0, trackRow.height - 2 * Theme.clipSelectionRingWidth)

    // While dragging, show the same snapped landing outline the
    // library drop uses, on whichever track the clip is over.
    //
    // Gated on `moving` rather than drag.active: Qt only clears drag.active
    // after onReleased returns, so resetting y on drop re-armed the preview and
    // left an orphaned outline on the old track.
    function updateMovePreview() {
        if (!clipMouse.moving)
            return
        // Keep linked/selected partners locked to this clip's live X and Y.
        panel.updateMoveFollow(x - clipMouse.moveOriginX,
                               y - Theme.clipSelectionRingWidth)
        const desired = Math.max(0, (x - Theme.clipSelectionRingWidth) / panel.pxPerSecond)
        const pos = mapToItem(timelineColumn, width / 2, height / 2)
        const targetTrack = panel.trackIndexAtY(pos.y)
        panel.showLandingPreview(targetTrack >= 0 ? targetTrack : trackIndex,
                                 desired, clipData.duration)
    }
    onXChanged: updateMovePreview()
    onYChanged: updateMovePreview()

    // CapCut-style: selected/linked partners slide with the dragged clip.
    readonly property bool moveFollowFollower: panel.moveFollowActive
                                              && selected
                                              && !(panel.moveLeaderTrack === trackIndex
                                                   && panel.moveLeaderClip === clipIndex)
    readonly property real followOffsetX: moveFollowFollower ? panel.moveFollowDeltaX : 0
    readonly property real followOffsetY: (moveFollowFollower && trackIndex === panel.moveLeaderTrack)
                                          ? panel.moveFollowDeltaY : 0

    Binding {
        target: clipItem
        property: "x"
        when: !clipMouse.drag.active
        value: Math.max(Theme.clipSelectionRingWidth,
                        clipItem.clipData.start * panel.pxPerSecond
                        + Theme.clipSelectionRingWidth
                        + clipItem.followOffsetX)
    }

    Binding {
        target: clipItem
        property: "y"
        when: !clipMouse.drag.active
        value: Theme.clipSelectionRingWidth + followOffsetY
    }

    // No `Behavior on y` here. A drop settle looks appealing but cannot work at this
    // seam: onReleased reads clipItem.y through mapToItem to decide the target track,
    // and MouseArea.drag writes y directly during the drag. Animating y makes that
    // read lag the pointer, so trackIndexAtY resolves back to the origin track and a
    // cross-track drag silently becomes a same-track move. Gating the Behavior on
    // drag.active does not save it — Qt clears drag.active only after onReleased
    // returns, so y is already reset by then and the settle never plays anyway.
    // Touch: the hold has to be visible or there is no way to know the clip is now
    // liftable rather than still waiting. Lifts the clip out of the row and above its
    // neighbours for as long as the finger owns it.
    readonly property bool lifted: clipItem.touchMode
                                  && (clipMouse.moveArmed || clipMouse.drag.active)
    z: (lifted || clipMouse.drag.active || (moveFollowFollower && Math.abs(followOffsetY) > 1)) ? 10 : 0
    scale: lifted ? 1.03 : 1.0
    Behavior on scale {
        NumberAnimation { duration: Theme.durationPress; easing.type: Theme.easing }
    }

    Rectangle {
        id: clipBackground
        anchors.fill: parent
        radius: Theme.radiusSm
        // Lightens on hover — previously nothing
        // in the clip reacted to the pointer.
        color: {
            if (clipItem.clipData.kind === "adjustment") {
                const base = panel.adjustmentColor(clipItem.clipData.adjustmentKind)
                const lit = clipMouse.containsMouse || clipItem.lifted
                return lit ? Qt.lighter(base, 1.15) : base
            }
            const base = panel.clipColor(
                clipItem.trackType === "shape" ? "graphic" : clipItem.trackType)
            const lit = clipMouse.containsMouse || clipItem.lifted
            return lit ? Qt.lighter(base, 1.15) : base
        }
        border.width: clipItem.effectDropTarget
                      ? Theme.borderWidthFocus
                      : (clipItem.selected ? Theme.clipSelectionRingWidth : 0)
        border.color: clipItem.effectDropTarget ? Theme.clipEffect : Theme.primary
        clip: true

        Behavior on color {
            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }
        Behavior on border.width {
            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }

        Rectangle {
            anchors.fill: parent
            visible: clipItem.effectDropTarget
            color: Qt.rgba(Theme.clipEffect.r, Theme.clipEffect.g, Theme.clipEffect.b, 0.28)
            z: 4
        }

        // Fade ramp overlays — sized only to the fade spans so a multi-hour clip
        // does not allocate a hundreds-of-thousands-px Canvas framebuffer.
        Canvas {
            id: fadeInCanvas
            x: 0
            y: 0
            z: 2
            width: Math.max(0, Math.min(parent.width,
                                        (clipItem.clipData.fadeIn || 0) * panel.pxPerSecond))
            height: parent.height
            visible: width > 0.5
            // Curve / custom shape changes must redraw even when width is unchanged.
            property string curveKey: (clipItem.clipData.fadeCurve || "smooth")
                                      + "|" + JSON.stringify(clipItem.clipData.fadeShape || [])
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onCurveKeyChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                if (width < 0.5 || height < 0.5)
                    return
                ctx.fillStyle = "rgba(0,0,0,0.38)"
                ctx.strokeStyle = "rgba(255,255,255,0.9)"
                ctx.lineWidth = 1.5
                const steps = Math.max(8, Math.min(64, Math.ceil(width / 2)))
                ctx.beginPath()
                ctx.moveTo(0, 0)
                ctx.lineTo(width, 0)
                for (let i = steps; i >= 0; --i) {
                    const t = i / steps
                    const g = clipItem.fadeGainAt(t)
                    ctx.lineTo(t * width, height * (1.0 - g))
                }
                ctx.closePath()
                ctx.fill()
                ctx.beginPath()
                for (let i = 0; i <= steps; ++i) {
                    const t = i / steps
                    const g = clipItem.fadeGainAt(t)
                    const x = t * width
                    const y = height * (1.0 - g)
                    if (i === 0)
                        ctx.moveTo(x, y)
                    else
                        ctx.lineTo(x, y)
                }
                ctx.stroke()
            }
        }

        Canvas {
            id: fadeOutCanvas
            y: 0
            z: 2
            width: Math.max(0, Math.min(parent.width,
                                        (clipItem.clipData.fadeOut || 0) * panel.pxPerSecond))
            height: parent.height
            x: parent.width - width
            visible: width > 0.5
            property string curveKey: (clipItem.clipData.fadeCurve || "smooth")
                                      + "|" + JSON.stringify(clipItem.clipData.fadeShape || [])
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onXChanged: requestPaint()
            onCurveKeyChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                if (width < 0.5 || height < 0.5)
                    return
                ctx.fillStyle = "rgba(0,0,0,0.38)"
                ctx.strokeStyle = "rgba(255,255,255,0.9)"
                ctx.lineWidth = 1.5
                const steps = Math.max(8, Math.min(64, Math.ceil(width / 2)))
                // Fade-out: progress from full (left of wedge) to silent (right edge).
                ctx.beginPath()
                ctx.moveTo(0, 0)
                ctx.lineTo(width, 0)
                for (let i = steps; i >= 0; --i) {
                    const t = i / steps
                    const g = clipItem.fadeGainAt(1.0 - t)
                    ctx.lineTo(t * width, height * (1.0 - g))
                }
                ctx.closePath()
                ctx.fill()
                ctx.beginPath()
                for (let i = 0; i <= steps; ++i) {
                    const t = i / steps
                    const g = clipItem.fadeGainAt(1.0 - t)
                    const x = t * width
                    const y = height * (1.0 - g)
                    if (i === 0)
                        ctx.moveTo(x, y)
                    else
                        ctx.lineTo(x, y)
                }
                ctx.stroke()
            }
        }

        ClipFilmstrip {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.topMargin: (clipItem.trackType === "video"
                                || clipItem.trackType === "audio"
                                || clipItem.trackType === "shape")
                               ? clipItem.headerBandHeight
                               : 0
            visible: clipItem.clipData.filmstripPath
                     && clipItem.clipData.filmstripPath.length > 0
                     && !clipItem.showWaveform
                     && (clipItem.trackType === "video"
                         || clipItem.trackType === "shape"
                         || clipItem.clipData.kind === "image")
            filmstripPath: clipItem.clipData.filmstripPath
            // Video only: on-demand tiles need a decodable video stream, and images/shapes
            // have a single poster frame that the strip already covers exactly.
            sourcePath: clipItem.clipData.kind === "video" ? (clipItem.clipData.path || "") : ""
            inPoint: clipItem.clipData.inPoint
            outPoint: clipItem.clipData.outPoint
            sourceDuration: clipItem.clipData.sourceDuration
            // Image and vector "strips" are a single poster frame.
            frameCount: (clipItem.clipData.kind === "image" || clipItem.clipData.kind === "vector") ? 1 : 8
            // Viewport-cull tiles so multi-hour clips don't spawn thousands of Images.
            worldX: clipItem.x
            viewX: panel.timelineViewX
            viewW: panel.timelineViewW
            refreshEpoch: panel.filmstripRefreshEpoch !== undefined
                          ? panel.filmstripRefreshEpoch : 0
            z: 0
        }

        Rectangle {
            visible: clipItem.trackType === "video"
                     || clipItem.trackType === "audio"
                     || clipItem.trackType === "shape"
            width: parent.width
            height: clipItem.headerBandHeight
            color: Theme.scrimColor
            z: 1

            // Just the name. The effect stack used to be listed on a second line here, but it
            // now lives on the clip's adjustment lane, which shows it in the row above — two
            // copies of the same list, one of them detached from the thing you edit.
            Text {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                text: clipItem.clipData.name
                color: Theme.onMedia
                font.pixelSize: Theme.fontSizeTiny
                font.family: Theme.fontFamily
                elide: Text.ElideRight
            }
        }

        // Adjustments get a single centred line rather than the scrim band above: a nested lane
        // is a ~20px strip, and a band plus two text rows has nowhere to be legible.
        Item {
            visible: clipItem.clipData.kind === "adjustment"
            anchors.fill: parent
            z: 1

            Text {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                text: clipItem.adjustmentLabelText
                color: Theme.onMedia
                font.pixelSize: Theme.fontSizeTiny
                font.family: Theme.fontFamily
                elide: Text.ElideRight
            }
        }

        Column {
            visible: clipItem.trackType === "text"
                     || clipItem.trackType === "subtitle"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            // Clamped so the label width cannot
            // go negative on very narrow clips.
            anchors.leftMargin: Math.min(Theme.spacingLg, parent.width / 4)
            anchors.rightMargin: Math.min(Theme.spacingLg, parent.width / 4)
            spacing: 1

            Text {
                width: parent.width
                text: clipItem.trackType === "subtitle"
                      ? (clipItem.clipData.name
                         || qsTr("Subtitles"))
                      : (clipItem.clipData.textContent
                         || clipItem.clipData.name)
                color: Theme.onMedia
                font.pixelSize: Theme.fontSizeXs
                font.family: Theme.fontFamily
                elide: Text.ElideRight
            }

        }

        // Waveform: only the slice of the clip that is on screen gets a Canvas, at 1:1 px,
        // so a multi-hour clip neither allocates a hundreds-of-thousands-px buffer nor
        // stretches a fixed peak list across it.
        Item {
            id: waveformHost
            visible: clipItem.trackType === "audio"
                     || (clipItem.trackType === "video"
                         && clipItem.showWaveform)
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.topMargin: clipItem.headerBandHeight
            anchors.bottom: parent.bottom
            clip: true

            // Bumped when an off-thread decode lands, to re-run the peaks and lane-count
            // bindings. Lives on the host because the labels below need it too.
            property int decodeRevision: 0

            // Channels the decoder found, 0 until the first block lands — so a multi-channel
            // clip draws the merged lane for one frame and then splits.
            readonly property int sourceChannels: {
                void decodeRevision
                if (!clipItem.showChannelWaveforms || !clipItem.clipData.path)
                    return 0
                return EditorState.waveformChannelCount(
                    clipItem.clipData.path, clipItem.clipData.audioStreamIndex || 0)
            }

            // Below this a lane is a grey smear rather than a waveform, so fall back to the
            // single merged envelope instead of drawing something unreadable.
            readonly property real minLaneHeight: 8

            readonly property int laneCount: {
                if (sourceChannels < 2)
                    return 1
                return (height / sourceChannels) >= minLaneHeight ? sourceChannels : 1
            }
            readonly property real laneHeight: height / Math.max(1, laneCount)

            Connections {
                target: EditorState
                function onWaveformRangeReady(path) {
                    if (path === clipItem.clipData.path)
                        waveformHost.decodeRevision++
                }
            }

            Canvas {
                id: waveformCanvas

                // Visible slice of the clip, snapped to a 256px grid so scrolling only
                // re-queries peaks every step instead of every frame.
                readonly property real windowStep: 256
                readonly property real visibleLeft: {
                    const left = panel.timelineViewX - clipItem.x - windowStep
                    return Math.max(0, Math.floor(left / windowStep) * windowStep)
                }
                readonly property real visibleRight: {
                    if (panel.timelineViewW <= 0)
                        return waveformHost.width
                    const right = panel.timelineViewX - clipItem.x + panel.timelineViewW + windowStep
                    return Math.min(waveformHost.width,
                                    Math.ceil(right / windowStep) * windowStep)
                }
                // Source seconds per px of clip body, so the strip maps to the trimmed
                // window rather than the whole file.
                readonly property real srcPerPx: {
                    const span = (clipItem.clipData.outPoint || 0) - (clipItem.clipData.inPoint || 0)
                    return waveformHost.width > 0 && span > 0 ? span / waveformHost.width : 0
                }

                x: visibleLeft
                width: Math.max(1, Math.min(4096, Math.floor(visibleRight - visibleLeft)))
                height: waveformHost.height

                property var peaks: {
                    void waveformHost.decodeRevision
                    if (!clipItem.clipData.path || srcPerPx <= 0)
                        return []
                    // The per-channel query covers this window already; asking for the merged
                    // envelope as well would double the work for something nothing draws.
                    if (waveformHost.laneCount > 1)
                        return []
                    return EditorState.waveformPeaksRange(
                        clipItem.clipData.path,
                        (clipItem.clipData.inPoint || 0) + x * srcPerPx,
                        width * srcPerPx,
                        Math.ceil(width),
                        clipItem.clipData.audioStreamIndex || 0)
                }

                // { channels, buckets, names, peaks } with peaks channel-major and flat.
                property var channelData: {
                    void waveformHost.decodeRevision
                    if (!clipItem.clipData.path || srcPerPx <= 0
                            || waveformHost.laneCount <= 1)
                        return null
                    return EditorState.waveformChannelPeaksRange(
                        clipItem.clipData.path,
                        (clipItem.clipData.inPoint || 0) + x * srcPerPx,
                        width * srcPerPx,
                        Math.ceil(width),
                        clipItem.clipData.audioStreamIndex || 0)
                }

                onPeaksChanged: requestPaint()
                onChannelDataChanged: requestPaint()
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()

                // One lane per source channel. Drawn as a single filled path per lane rather
                // than a rect per column: 8 lanes over a viewport-wide clip is tens of
                // thousands of fillRect calls, which visibly hitches on repaint.
                function paintLanes(ctx, data) {
                    const channels = data.channels
                    const buckets = data.buckets
                    const values = data.peaks
                    const w = Math.max(1, Math.floor(width))
                    const laneH = height / channels

                    ctx.fillStyle = Theme.waveformColor
                    for (var c = 0; c < channels; c++) {
                        const base = c * buckets
                        const mid = c * laneH + laneH / 2
                        const half = (laneH / 2) * 0.85
                        ctx.beginPath()
                        // Top edge left to right, then the mirrored bottom edge back, so the
                        // lane closes into one shape.
                        for (var x = 0; x < w; x++) {
                            var i0 = base + Math.floor(x * buckets / w)
                            var i1 = base + Math.floor((x + 1) * buckets / w)
                            if (i1 <= i0)
                                i1 = Math.min(base + buckets, i0 + 1)
                            var peak = 0
                            for (var i = i0; i < i1; i++) {
                                if (values[i] > peak)
                                    peak = values[i]
                            }
                            const amp = Math.max(0.5, peak * half)
                            if (x === 0)
                                ctx.moveTo(x, mid - amp)
                            else
                                ctx.lineTo(x, mid - amp)
                            ctx.lineTo(x + 1, mid - amp)
                        }
                        for (var xb = w - 1; xb >= 0; xb--) {
                            var j0 = base + Math.floor(xb * buckets / w)
                            var j1 = base + Math.floor((xb + 1) * buckets / w)
                            if (j1 <= j0)
                                j1 = Math.min(base + buckets, j0 + 1)
                            var peakB = 0
                            for (var j = j0; j < j1; j++) {
                                if (values[j] > peakB)
                                    peakB = values[j]
                            }
                            const ampB = Math.max(0.5, peakB * half)
                            ctx.lineTo(xb + 1, mid + ampB)
                            ctx.lineTo(xb, mid + ampB)
                        }
                        ctx.closePath()
                        ctx.fill()
                    }

                    // Without a divider eight lanes read as one grey block. Inner edges only.
                    ctx.fillStyle = Theme.panelBorder
                    ctx.globalAlpha = 0.5
                    for (var d = 1; d < channels; d++)
                        ctx.fillRect(0, Math.round(d * laneH), w, 1)
                    ctx.globalAlpha = 1.0
                }

                onPaint: {
                    var ctx = getContext("2d");
                    ctx.clearRect(0, 0, width, height);

                    if (waveformHost.laneCount > 1 && channelData
                            && channelData.channels > 1 && channelData.buckets > 0) {
                        paintLanes(ctx, channelData);
                        return;
                    }

                    if (!peaks || peaks.length === 0)
                        return;
                    ctx.fillStyle = Theme.waveformColor;
                    var mid = height / 2;
                    var w = Math.max(1, Math.floor(width));
                    var n = peaks.length;
                    for (var x = 0; x < w; x++) {
                        var i0 = Math.floor(x * n / w);
                        var i1 = Math.floor((x + 1) * n / w);
                        if (i1 <= i0)
                            i1 = Math.min(n, i0 + 1);
                        var peak = 0;
                        for (var i = i0; i < i1; i++) {
                            if (peaks[i] > peak)
                                peak = peaks[i];
                        }
                        var amp = peak * mid * 0.9;
                        if (amp > 0.5)
                            ctx.fillRect(x, mid - amp, 1, amp * 2);
                    }
                }
            }

            // Channel labels. Pinned to the host, not drawn into the canvas: the canvas x
            // tracks the viewport, so anything painted in its coordinates slides while you
            // scroll. Hidden when a lane is too short for the glyph to fit.
            Repeater {
                model: waveformHost.laneCount > 1 ? waveformHost.laneCount : 0

                Text {
                    readonly property var names: waveformCanvas.channelData
                                                 ? waveformCanvas.channelData.names : null
                    x: 3
                    y: index * waveformHost.laneHeight
                       + (waveformHost.laneHeight - height) / 2
                    visible: waveformHost.laneHeight >= 14
                    text: (names && names[index]) ? names[index] : (index + 1)
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeTiny
                }
            }
        }
    }

    MouseArea {
        id: clipMouse
        z: 2
        anchors.fill: parent
        hoverEnabled: true
        // Always leave edge strips for CapCut-style trim cursor on approach.
        anchors.leftMargin: clipItem.edgeMargin
        anchors.rightMargin: clipItem.edgeMargin
        enabled: !leftTrimMouse.pressed && !rightTrimMouse.pressed
                     && !fadeInMouse.pressed && !fadeOutMouse.pressed
        // CapCut: open hand to move. Trim edges set SizeHorCursor via their own
        // HoverHandlers (subtitle-grip pattern); do not claim a cursor here while
        // those zones are hovered or Qt keeps the open-hand shape.
        cursorShape: {
            if (drag.active)
                return Qt.ClosedHandCursor
            if (leftTrimMouse.containsMouse || rightTrimMouse.containsMouse
                    || fadeInMouse.containsMouse || fadeOutMouse.containsMouse)
                return Qt.BlankCursor
            return Qt.OpenHandCursor
        }
        // Right-click opens the clip menu; the app
        // previously had no context menus at all.
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        // On touch a clip only becomes draggable after a deliberate press-and-hold.
        // Holding the grab from the first touch (preventStealing over a surface that is
        // mostly clips) meant the Flickable could never pan, so a timeline with any
        // content in it could not be scrolled at all.
        //
        // Arming goes through drag.target, not preventStealing: MouseArea copies
        // preventStealing into its private stealMouse flag once, in mousePressEvent, and
        // drag activation needs keepMouseGrab && stealMouse. Raising preventStealing
        // after the press sets only keepMouseGrab, which then blocks the branch that
        // would have set stealMouse — leaving the clip grabbed and permanently
        // undraggable. A null drag.target skips the drag block entirely instead, so the
        // Flickable steals at its own threshold and panning works.
        drag.target: clipItem.touchMode ? (moveArmed ? clipItem : null) : clipItem
        drag.axis: Drag.XAndYAxis
        // Once armed the finger is already down and still, so any motion is the move.
        drag.threshold: clipItem.touchMode ? 0 : 8
        drag.minimumX: {
            if (clipItem.selected && EditorState.selection.length > 1) {
                const earliest = EditorState.selectionEarliestStartSeconds()
                const minStart = Math.max(0, (clipItem.clipData.start || 0) - earliest)
                return minStart * panel.pxPerSecond + Theme.clipSelectionRingWidth
            }
            return Theme.clipSelectionRingWidth
        }
        // Allow dropping onto any track, not just the immediate neighbours.
        drag.minimumY: -Math.max(clipItem.trackRow.height, panel.totalTracksHeight())
        drag.maximumY: Math.max(clipItem.trackRow.height * 2, panel.totalTracksHeight())
        preventStealing: !clipItem.touchMode
        pressAndHoldInterval: clipItem.touchMode ? 450 : 800
        property int originTrack: clipItem.trackIndex
        property int originClip: clipItem.clipIndex
        // Model-space X at press — follow delta is measured from this so partners
        // stay locked to the leader even after the drag threshold has been crossed.
        property real moveOriginX: 0
        // drag.active is cleared after onReleased in some paths; remember we dragged.
        property bool didDrag: false
        // True only for the span we treat as a move, so the landing preview stops
        // updating the moment the clip is dropped.
        property bool moving: false
        // Touch only: the hold has been recognised and the clip is now draggable.
        // Releasing without moving opens the context menu instead.
        property bool moveArmed: false

        // Where inside the clip the finger landed, so the autoscroll driver below can
        // recompute the pointer's viewport position each tick instead of guessing at
        // the clip's centre (which for a clip wider than the viewport is never in it).
        property real grabX: 0
        property real grabY: 0

        onPressed: (mouse) => {
            Qt.callLater(function() { clipItem.forceActiveFocus() })
            // Read before anything selects: `selected` comes back through the model, so testing it
            // after the call would report the state this press is about to create.
            const wasSelected = clipItem.selected
            // A previous gesture that ended while snapped would otherwise leave the latch engaged
            // and swallow this one's first tick.
            Haptics.reset()
            originTrack = clipItem.trackIndex
            originClip = clipItem.clipIndex
            moveOriginX = clipItem.clipData.start * panel.pxPerSecond
                          + Theme.clipSelectionRingWidth
            grabX = mouse.x + clipItem.edgeMargin
            grabY = mouse.y
            didDrag = false
            moving = false
            moveArmed = false
            if (mouse.button === Qt.RightButton) {
                // Right-click selects, then opens the menu.
                if (!clipItem.selected)
                    EditorState.selectClip(clipItem.trackIndex, clipItem.clipIndex)
                clipContextMenu.popup()
                return
            }
            // Touch multi-select mode (see the panel's multiSelectActive): the press
            // does nothing so a pan still reaches the Flickable, and the tap toggles
            // on release. Undefined on desktop, which has shift-click and the marquee.
            if (panel.multiSelectActive === true)
                return
            if ((mouse.modifiers & (Qt.ShiftModifier | Qt.ControlModifier)) !== 0)
                EditorState.addToSelection(clipItem.trackIndex, clipItem.clipIndex)
            else if (!wasSelected)
                EditorState.selectClip(clipItem.trackIndex, clipItem.clipIndex)
            // Only when the press changed the selection. Tapping a clip that is already selected
            // does nothing, and onClicked is about to run this same branch again — ticking on both
            // would double up on any tap slow enough to outlast the rate limiter.
            if (!wasSelected)
                Haptics.select()
        }
        // Hold, then move to drag; hold and let go to get the menu. One gesture serves
        // both, which is what leaves the plain drag free for the Flickable to pan with.
        onPressAndHold: (mouse) => {
            if (!clipItem.touchMode || mouse.button === Qt.RightButton)
                return
            if (didDrag || drag.active || panel.multiSelectActive === true)
                return
            moveArmed = true
            // The moment the clip becomes draggable, and the only one in the gesture that nothing
            // on screen can announce ahead of the user's own movement: the lift animation plays
            // because of this, not before it.
            Haptics.pickUp()
            if (!clipItem.selected)
                EditorState.selectClip(clipItem.trackIndex, clipItem.clipIndex)
        }
        onClicked: (mouse) => {
            if (mouse.button === Qt.RightButton || didDrag)
                return
            if (panel.multiSelectActive === true) {
                // The press deliberately does nothing in this mode so a pan can still reach the
                // Flickable, which leaves the tick to the tap that lands here.
                panel.toggleInSelection(clipItem.trackIndex, clipItem.clipIndex)
                Haptics.select()
                return
            }
            if ((mouse.modifiers & (Qt.ShiftModifier | Qt.ControlModifier)) !== 0)
                EditorState.addToSelection(clipItem.trackIndex, clipItem.clipIndex)
            else
                EditorState.selectClip(clipItem.trackIndex, clipItem.clipIndex)
        }

        // Surfaces actions that were previously
        // reachable only by unlabelled shortcut,
        // plus cutSelection which had no UI at all.
        ThemedContextMenu {
            id: clipContextMenu

            property bool canPasteEffects: false
            property bool canPasteAttributes: false
            onAboutToShow: {
                canPasteEffects = EditorState.clipboardHasEffects()
                canPasteAttributes = EditorState.canPasteAttributes()
            }

            ThemedMenuItem {
                text: qsTr("Properties")
                icon.name: Theme.icons.sliders
                onTriggered: {
                    if (!clipItem.selected)
                        EditorState.selectClip(clipItem.trackIndex, clipItem.clipIndex)
                    if (typeof panel.openClipProperties === "function")
                        panel.openClipProperties()
                }
            }
            ThemedMenuItem {
                // Touch route into multi-clip selection; the panel owns the mode and
                // the desktop panel does not declare the property at all.
                text: qsTr("Select multiple")
                icon.name: Theme.icons.check
                visible: panel.multiSelectActive === false
                onTriggered: panel.multiSelectActive = true
            }
            ThemedMenuSeparator { }
            ThemedMenuItem {
                text: qsTr("Split at current time")
                icon.name: Theme.icons.scissors
                onTriggered: EditorState.splitAtPlayhead()
            }
            ThemedMenuItem {
                text: qsTr("Split item at current time")
                icon.name: Theme.icons.scissors
                // Scoped to just this clip (and its linked partner, e.g. companion
                // audio) — splitAtPlayhead() above cuts every clip under the playhead
                // across every track, which is surprising when picked from one clip's menu.
                visible: EditorState.playheadSeconds > clipItem.clipData.start
                        && EditorState.playheadSeconds < clipItem.clipData.start + clipItem.clipData.duration
                onTriggered: EditorState.splitClipAt(clipItem.trackIndex, clipItem.clipIndex,
                                                     EditorState.playheadSeconds)
            }
            ThemedMenuItem {
                text: qsTr("Separate audio")
                icon.name: Theme.icons.audioLines
                // CapCut: only offer extract when the clip still has embedded audio.
                visible: clipItem.trackType === "video" && EditorState.separateAudioAvailable
                onTriggered: EditorState.separateAudioFromSelection()
            }
            ThemedMenuItem {
                text: qsTr("Separate all audio tracks")
                icon.name: Theme.icons.audioLines
                visible: clipItem.trackType === "video" && EditorState.separateAudioAvailable
                         && EditorState.clipAudioStreamCount(clipItem.trackIndex, clipItem.clipIndex) > 1
                onTriggered: EditorState.separateAllAudioTracks(clipItem.trackIndex, clipItem.clipIndex)
            }
            ThemedMenuItem {
                text: qsTr("Unlink")
                icon.name: Theme.icons.unlink
                visible: !!clipItem.clipData.linked && EditorState.unlinkAvailable
                onTriggered: EditorState.unlinkSelectedClips()
            }
            ThemedMenuSeparator { }
            ThemedMenuItem {
                text: qsTr("Cut")
                icon.name: Theme.icons.scissors
                onTriggered: EditorState.cutSelection()
            }
            ThemedMenuItem {
                text: qsTr("Copy")
                icon.name: Theme.icons.copy
                onTriggered: EditorState.copySelection()
            }
            ThemedMenuItem {
                text: qsTr("Paste attributes…")
                icon.name: Theme.icons.clipboardPaste
                enabled: clipContextMenu.canPasteAttributes
                onTriggered: EditorState.requestPasteAttributes()
            }
            ThemedMenuItem {
                text: qsTr("Duplicate")
                icon.name: Theme.icons.copyPlus
                onTriggered: EditorState.duplicateSelectedClip()
            }
            ThemedMenuItem {
                text: qsTr("Rename…")
                icon.name: Theme.icons.pencil
                onTriggered: clipItem.panel.requestRenameClip(clipItem.trackIndex, clipItem.clipIndex)
            }
            ThemedMenuSeparator { }
            ThemedMenuItem {
                text: qsTr("Copy effects")
                icon.name: Theme.icons.wand
                visible: clipItem.hasAnyEffects
                onTriggered: EditorState.copyClipEffectsToClipboard(clipItem.trackIndex,
                                                                    clipItem.clipIndex)
            }
            ThemedMenuItem {
                text: qsTr("Paste effects")
                icon.name: Theme.icons.clipboardPaste
                enabled: clipContextMenu.canPasteEffects
                onTriggered: EditorState.pasteEffectsFromClipboard(clipItem.trackIndex,
                                                                   clipItem.clipIndex)
            }
            ThemedMenuItem {
                text: qsTr("Save effects as preset…")
                icon.name: Theme.icons.save
                visible: clipItem.hasAnyEffects
                onTriggered: clipItem.panel.requestSaveEffectPreset(clipItem.trackIndex,
                                                                     clipItem.clipIndex)
            }
            ThemedMenuSeparator { visible: clipItem.clipData.kind === "adjustment" }
            ThemedMenuItem {
                text: qsTr("Unlink from clip")
                icon.name: Theme.icons.unlink
                visible: clipItem.pinnedToClip
                onTriggered: EditorState.unlinkAdjustment(clipItem.trackIndex, clipItem.clipIndex)
            }
            ThemedMenuItem {
                text: qsTr("Move to its own track")
                icon.name: Theme.icons.layers
                // Only meaningful for a nested one: a standalone adjustment already has one.
                visible: clipItem.clipData.kind === "adjustment"
                         && clipItem.panel.tracks[clipItem.trackIndex].isAdjustmentLane === true
                onTriggered: EditorState.moveAdjustmentToOwnTrack(clipItem.trackIndex,
                                                                  clipItem.clipIndex)
            }
            ThemedMenuSeparator { }
            ThemedMenuItem {
                text: qsTr("Delete")
                icon.name: Theme.icons.trash
                onTriggered: EditorState.deleteSelectedClip()
            }
        }
        onReleased: {
            const moved = drag.active || didDrag
            // Armed but never moved: the hold was a request for the menu.
            const wantsMenu = clipItem.touchMode && moveArmed && !moved
            // NOTE: Keep didDrag true if moved so onClicked (which runs immediately after
            // onReleased in Qt Quick) knows a drag happened and does not collapse the selection.
            // didDrag is reset on the next onPressed or onCanceled.
            if (!moved)
                didDrag = false
            moving = false
            moveArmed = false
            // Clear follow before committing so partners don't keep the drag
            // offset on top of the new model start for a frame.
            panel.clearMoveFollow()
            panel.clearLandingPreview()
            if (!moved) {
                if (wantsMenu) {
                    clipItem.y = Theme.clipSelectionRingWidth
                    clipContextMenu.popup()
                }
                return
            }
            const newStart = (clipItem.x - Theme.clipSelectionRingWidth) / panel.pxPerSecond
            const pos = clipItem.mapToItem(timelineColumn, clipItem.width / 2, clipItem.height / 2)
            const target = typeof panel.dropTargetAtY === "function"
                         ? panel.dropTargetAtY(pos.y)
                         : { "track": panel.trackIndexAtY(pos.y), "lane": -1 }
            clipItem.y = Theme.clipSelectionRingWidth
            const isAdjustment = clipItem.clipData.kind === "adjustment"
            const wasInLane = panel.tracks[originTrack].isAdjustmentLane === true
            // Use indices captured on press — after the model updates, clipIndex
            // on this delegate can already refer to a different clip.
            if (isAdjustment && target.lane >= 0) {
                // Released on a lane strip: an ordinary cross-track move onto that lane. Only an
                // adjustment can land there — a media clip aimed at the strip falls through to
                // the row's own clip area rather than snapping back from a rejected move.
                if (target.lane !== originTrack)
                    EditorState.moveClipToTrack(originTrack, originClip, target.lane, newStart)
                else
                    EditorState.moveClip(originTrack, originClip, newStart)
            } else if (isAdjustment && target.track >= 0
                       && panel.tracks[target.track].type !== "adjustment") {
                // An adjustment released on a media track's body nests inside it: it stops
                // applying to everything composited below and applies only to that track.
                EditorState.moveAdjustmentToLane(originTrack, originClip, target.track, newStart)
            } else if (isAdjustment && wasInLane && target.track < 0) {
                // Dragged clear of every row: the inverse gesture, back to a track of its own
                // affecting everything below it.
                EditorState.moveAdjustmentToOwnTrack(originTrack, originClip, newStart)
            } else if (target.track >= 0 && target.track !== originTrack) {
                EditorState.moveClipToTrack(originTrack, originClip, target.track, newStart)
            } else {
                EditorState.moveClip(originTrack, originClip, newStart)
            }
            // Closes the gesture pickUp opened, and clears the snap and lane latches the drag left
            // engaged.
            Haptics.drop()
        }
        onPositionChanged: {
            if (pressed && drag.active) {
                didDrag = true
                if (!moving) {
                    moving = true
                    panel.beginMoveFollow(originTrack, originClip)
                }
                panel.updateMoveFollow(clipItem.x - moveOriginX,
                                       clipItem.y - Theme.clipSelectionRingWidth)
            }
        }
        onCanceled: {
            didDrag = false
            moving = false
            moveArmed = false
            panel.clearMoveFollow()
            panel.clearLandingPreview()
            // No drop tick: the clip went back where it came from. The latches still have to go.
            Haptics.reset()
        }
    }

    // Edge autoscroll while a clip is being dragged on touch. Guarded on the panel
    // actually offering it, because this file is shared with the desktop TimelinePanel
    // (same pattern as panel.openClipProperties).
    // Armed means this clip owns the gesture. The Flickable is told to stop competing for
    // it rather than the MouseArea trying to hold the grab: raising preventStealing after
    // the press sets keepMouseGrab without stealMouse, and drag activation needs both.
    onLiftedChanged: {
        if (clipItem.touchMode && typeof panel.setScrollLocked === "function")
            panel.setScrollLocked(clipItem.lifted)
    }
    Timer {
        id: dragAutoScroll
        interval: 16
        repeat: true
        running: clipItem.touchMode && clipMouse.moving && clipMouse.drag.active
                 && typeof panel.dragEdgeScroll === "function"

        // Ramps with how far past the edge the finger is, so a nudge creeps and a
        // finger pinned to the edge travels.
        function step(depth) { return Math.min(24, 6 + depth * 0.4) }

        onTriggered: {
            const px = clipItem.x + clipMouse.grabX - panel.timelineViewX
            const py = clipItem.mapToItem(timelineColumn, 0, clipMouse.grabY).y
                       + panel.seekHeaderHeight - panel.timelineViewY
            // Bands are a fraction of the axis, capped: the timeline pane is only ~120px
            // tall at its minimum, and fixed 48px bands there would meet in the middle and
            // leave every position inside one — scrolling on any drag, in one direction,
            // forever. The seek strip is pinned to the top of the viewport, so the vertical
            // band starts below it.
            const edgeX = Math.min(48, panel.timelineViewW * 0.25)
            const usableTop = panel.seekHeaderHeight
            const usableH = Math.max(0, panel.timelineViewH - usableTop)
            const edgeY = Math.min(48, usableH * 0.25)
            var dx = 0
            var dy = 0
            if (px < edgeX)
                dx = -step(edgeX - px)
            else if (px > panel.timelineViewW - edgeX)
                dx = step(px - (panel.timelineViewW - edgeX))
            if (edgeY > 0) {
                if (py < usableTop + edgeY)
                    dy = -step(usableTop + edgeY - py)
                else if (py > panel.timelineViewH - edgeY)
                    dy = step(py - (panel.timelineViewH - edgeY))
            }
            if (dx === 0 && dy === 0)
                return

            const applied = panel.dragEdgeScroll(dx, dy)
            // Qt recomputes the drag target's position from a *scene* anchor mapped
            // through the parent's current transform, so the scroll delta is already
            // folded into the next move event — adding it here does not double-count,
            // it only keeps the clip under the finger while no move events arrive.
            clipItem.x += applied.x
            clipItem.y += applied.y
        }
    }

    // Fade dots sit above trim handles so they stay
    // grabable at the corners (zero fade). Trim the
    // edge below the dots; grab the dots to fade.
    Rectangle {
        id: fadeInHandle
        width: clipItem.fadeHandleSize
        height: clipItem.fadeHandleSize
        radius: clipItem.fadeHandleSize / 2
        y: clipItem.touchMode
           ? Math.max(0, (clipItem.height - clipItem.fadeHandleSize) / 2)
           : 2
        z: 40
        visible: clipItem.timelineFadeHandles && clipItem.selected
                 && clipItem.width > clipItem.fadeHandleMinWidth
        color: Theme.primary
        border.color: Theme.onMedia
        border.width: 2

        Binding {
            target: fadeInHandle
            property: "x"
            when: !fadeInMouse.pressed
            value: Math.max(clipItem.fadeHandleInset,
                            Math.min(clipItem.width - clipItem.fadeHandleInset - fadeInHandle.width,
                                     (clipItem.clipData.fadeIn || 0) * panel.pxPerSecond - fadeInHandle.width / 2))
        }

        MouseArea {
            id: fadeInMouse
            anchors.fill: parent
            anchors.leftMargin: -6
            anchors.rightMargin: -6
            anchors.topMargin: -6
            anchors.bottomMargin: clipItem.height < 35 ? 4 : -6
            z: 1
            preventStealing: true
            hoverEnabled: true
            cursorShape: Qt.SizeHorCursor
            onPressed: (mouse) => {
                Qt.callLater(function() { clipItem.forceActiveFocus() })
                mouse.accepted = true
                EditorState.beginPreviewDrag(qsTr("Adjust fade"))
            }
            onPositionChanged: (mouse) => {
                if (!pressed)
                    return
                const px = Math.max(0, Math.min(clipItem.width,
                                                mapToItem(clipItem, mouse.x, mouse.y).x))
                fadeInHandle.x = Math.max(clipItem.fadeHandleInset,
                                          Math.min(clipItem.width - clipItem.fadeHandleInset
                                                   - fadeInHandle.width,
                                                   px - fadeInHandle.width / 2))
                EditorState.previewSetClipFade(clipItem.trackIndex, clipItem.clipIndex,
                                               px / panel.pxPerSecond,
                                               clipItem.clipData.fadeOut || 0)
            }
            onReleased: EditorState.commitPreviewDrag()
            onCanceled: EditorState.cancelPreviewDrag()

            HoverHandler { cursorShape: Qt.SizeHorCursor }

            ThemedToolTip {
                visible: fadeInMouse.pressed || fadeInMouse.containsMouse
                text: qsTr("Fade in %1s").arg((clipItem.clipData.fadeIn || 0).toFixed(2))
            }
        }
    }

    Rectangle {
        id: fadeOutHandle
        width: clipItem.fadeHandleSize
        height: clipItem.fadeHandleSize
        radius: clipItem.fadeHandleSize / 2
        y: clipItem.touchMode
           ? Math.max(0, (clipItem.height - clipItem.fadeHandleSize) / 2)
           : 2
        z: 40
        visible: clipItem.timelineFadeHandles && clipItem.selected
                 && clipItem.width > clipItem.fadeHandleMinWidth
        color: Theme.primary
        border.color: Theme.onMedia
        border.width: 2

        Binding {
            target: fadeOutHandle
            property: "x"
            when: !fadeOutMouse.pressed
            value: Math.max(clipItem.fadeHandleInset,
                            Math.min(clipItem.width - clipItem.fadeHandleInset - fadeOutHandle.width,
                                     clipItem.width - (clipItem.clipData.fadeOut || 0) * panel.pxPerSecond - fadeOutHandle.width / 2))
        }

        MouseArea {
            id: fadeOutMouse
            anchors.fill: parent
            anchors.leftMargin: -6
            anchors.rightMargin: -6
            anchors.topMargin: -6
            anchors.bottomMargin: clipItem.height < 35 ? 4 : -6
            z: 1
            preventStealing: true
            hoverEnabled: true
            cursorShape: Qt.SizeHorCursor
            onPressed: (mouse) => {
                Qt.callLater(function() { clipItem.forceActiveFocus() })
                mouse.accepted = true
                EditorState.beginPreviewDrag(qsTr("Adjust fade"))
            }
            onPositionChanged: (mouse) => {
                if (!pressed)
                    return
                const px = Math.max(0, Math.min(clipItem.width,
                                                mapToItem(clipItem, mouse.x, mouse.y).x))
                fadeOutHandle.x = Math.max(clipItem.fadeHandleInset,
                                           Math.min(clipItem.width - clipItem.fadeHandleInset
                                                    - fadeOutHandle.width,
                                                    px - fadeOutHandle.width / 2))
                EditorState.previewSetClipFade(clipItem.trackIndex, clipItem.clipIndex,
                                               clipItem.clipData.fadeIn || 0,
                                               Math.max(0, (clipItem.width - px) / panel.pxPerSecond))
            }
            onReleased: EditorState.commitPreviewDrag()
            onCanceled: EditorState.cancelPreviewDrag()

            HoverHandler { cursorShape: Qt.SizeHorCursor }

            ThemedToolTip {
                visible: fadeOutMouse.pressed || fadeOutMouse.containsMouse
                text: qsTr("Fade out %1s").arg((clipItem.clipData.fadeOut || 0).toFixed(2))
            }
        }
    }

    Rectangle {
        id: leftTrimHandle
        // Thin edge bar; hotspots still use the wide Theme width when idle.
        width: (leftTrimMouse.containsMouse || leftTrimHover.hovered || leftTrimMouse.pressed)
               ? Math.max(2, clipItem.trimHandleWidth * 0.35)
               : clipItem.trimHandleWidth
        anchors.left: clipBackground.left
        anchors.top: clipBackground.top
        anchors.bottom: clipBackground.bottom
        color: clipItem.showTrimHandles ? Theme.primary : "transparent"
        opacity: !clipItem.showTrimHandles ? 0
                 : (leftTrimMouse.containsMouse || leftTrimHover.hovered || leftTrimMouse.pressed)
                   ? 1.0 : 0.85

        Behavior on opacity {
            enabled: clipItem.showTrimHandles
            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }
        Behavior on width {
            enabled: clipItem.showTrimHandles
            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }

        ThemedToolTip {
            text: qsTr("Drag to trim the start")
            visible: clipItem.showTrimHandles
                     && (leftTrimMouse.containsMouse || leftTrimHover.hovered)
                     && !leftTrimMouse.pressed
        }
        z: 30

        MouseArea {
            id: leftTrimMouse
            anchors.fill: parent
            anchors.leftMargin: -clipItem.trimHotspotExtra
            anchors.rightMargin: -4
            // Leave the top corner for the fade-in dot.
            anchors.topMargin: clipItem.timelineFadeHandles && clipItem.showTrimHandles
                               && !clipItem.touchMode ? (clipItem.height < 35 ? 10 : 16) : -6
            anchors.bottomMargin: -6
            // Same reason as the move drag: these are ~38px strips at both edges of
            // every clip and they hold the grab, so on touch they turned each clip
            // boundary into another place the timeline could not be panned. Only the
            // selected clip — the one actually showing trim handles — arms them.
            enabled: !clipItem.touchMode || clipItem.showTrimHandles
            preventStealing: true
            hoverEnabled: true
            cursorShape: Qt.BlankCursor

            HoverHandler {
                id: leftTrimHover
                cursorShape: Qt.BlankCursor
            }

            onPressed: (mouse) => {
                Qt.callLater(function() { clipItem.forceActiveFocus() })
                const wasSelected = clipItem.selected
                Haptics.reset()
                clipItem.beginEdgeGesture(leftTrimMouse, mouse)
                if (!clipItem.selected)
                    EditorState.selectClip(clipItem.trackIndex, clipItem.clipIndex)
                if (!wasSelected)
                    Haptics.select()
            }
            onPositionChanged: (mouse) => {
                if (!pressed)
                    return
                // A vertical drag on this strip is someone reaching for another layer, not for
                // this clip's in point.
                if (clipItem.edgeGestureScrolled(leftTrimMouse, mouse))
                    return
                const end = (clipItem.clipData.start || 0)
                            + (clipItem.clipData.duration || 0)
                const raw = mapToItem(trackRow, mouse.x, mouse.y).x / panel.pxPerSecond
                // Floor duration so the clip stays at least
                // clipMinWidth; handles remain draggable to extend.
                const newStart = Math.min(raw, end - clipItem.minDurationSeconds)
                // The trim reports what it did rather than the caller guessing from the geometry:
                // snapping and every limit that can stop this edge live inside it, and the one the
                // user needs told — the source running out — has no cue on screen at all.
                Haptics.trimStep(
                    EditorState.trimClipLeft(clipItem.trackIndex, clipItem.clipIndex,
                                             Math.max(0, newStart)))
            }
            onReleased: Haptics.reset()
            onCanceled: Haptics.reset()
        }
    }

    Rectangle {
        id: rightTrimHandle
        width: (rightTrimMouse.containsMouse || rightTrimHover.hovered || rightTrimMouse.pressed)
               ? Math.max(2, clipItem.trimHandleWidth * 0.35)
               : clipItem.trimHandleWidth
        anchors.right: clipBackground.right
        anchors.top: clipBackground.top
        anchors.bottom: clipBackground.bottom
        color: clipItem.showTrimHandles ? Theme.primary : "transparent"
        opacity: !clipItem.showTrimHandles ? 0
                 : (rightTrimMouse.containsMouse || rightTrimHover.hovered || rightTrimMouse.pressed)
                   ? 1.0 : 0.85

        Behavior on opacity {
            enabled: clipItem.showTrimHandles
            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }
        Behavior on width {
            enabled: clipItem.showTrimHandles
            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }

        ThemedToolTip {
            text: qsTr("Drag to trim the end")
            visible: clipItem.showTrimHandles
                     && (rightTrimMouse.containsMouse || rightTrimHover.hovered)
                     && !rightTrimMouse.pressed
        }
        z: 30

        MouseArea {
            id: rightTrimMouse
            anchors.fill: parent
            anchors.leftMargin: -4
            anchors.rightMargin: -clipItem.trimHotspotExtra
            // Leave the top corner for the fade-out dot.
            anchors.topMargin: clipItem.timelineFadeHandles && clipItem.showTrimHandles
                               && !clipItem.touchMode ? (clipItem.height < 35 ? 10 : 16) : -6
            anchors.bottomMargin: -6
            // Same reason as the move drag: these are ~38px strips at both edges of
            // every clip and they hold the grab, so on touch they turned each clip
            // boundary into another place the timeline could not be panned. Only the
            // selected clip — the one actually showing trim handles — arms them.
            enabled: !clipItem.touchMode || clipItem.showTrimHandles
            preventStealing: true
            hoverEnabled: true
            cursorShape: Qt.BlankCursor

            HoverHandler {
                id: rightTrimHover
                cursorShape: Qt.BlankCursor
            }

            onPressed: (mouse) => {
                Qt.callLater(function() { clipItem.forceActiveFocus() })
                const wasSelected = clipItem.selected
                Haptics.reset()
                clipItem.beginEdgeGesture(rightTrimMouse, mouse)
                if (!clipItem.selected)
                    EditorState.selectClip(clipItem.trackIndex, clipItem.clipIndex)
                if (!wasSelected)
                    Haptics.select()
            }
            onPositionChanged: (mouse) => {
                if (!pressed)
                    return
                if (clipItem.edgeGestureScrolled(rightTrimMouse, mouse))
                    return
                const start = clipItem.clipData.start || 0
                const raw = mapToItem(trackRow, mouse.x, mouse.y).x / panel.pxPerSecond
                const newEnd = Math.max(raw, start + clipItem.minDurationSeconds)
                Haptics.trimStep(
                    EditorState.trimClipRight(clipItem.trackIndex, clipItem.clipIndex, newEnd))
            }
            onReleased: Haptics.reset()
            onCanceled: Haptics.reset()
        }
    }
}
