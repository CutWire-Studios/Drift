import QtQuick
import Drift
// The interactive chrome of one timeline clip: trim handles, fade dots and the context menu.
// The clip itself — body, filmstrip, waveform, labels — is drawn by TimelineTrackClips, which
// also owns the body gestures (select, move). This overlay only exists for the clips that need
// it right now (see TimelineTrackClips.activeClips), sits exactly over the drawn clip, and leaves
// its middle empty so presses there reach the renderer.
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
    // Wider trim hit areas for phones; desktop leaves this false.
    property bool touchMode: false
    // The renderer, told about the live trim so it draws the body where the edge currently is.
    property var trackClips: null

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

    // The row object from EditorState.clipsModel(trackIndex), whose properties are that model's
    // roles. Required rather than passed in, so this can only ever be a delegate of that model.
    //
    // Roles, not a QVariantMap out of panel.tracks: that rebuilt and re-converted every clip in
    // the project on every edit, and handed each delegate a brand-new JS object, so every
    // binding through it re-ran even for a clip nothing had touched.
    required property var model
    readonly property var clipData: model
    // The revision, not the selection itself: reading EditorState.selection rebuilt a list of
    // maps for the whole selection, and this binding exists once per clip in the project.
    property bool selected: (EditorState.selectionRevision,
                             EditorState.selectionContains(trackIndex, clipIndex))
    // Live trim geometry, held here instead of being written to the project on every pointer
    // sample. Applying the edit per sample meant rebuilding the whole timeline model each time —
    // ~33 ms on a heavy project, several frames — for a change to one clip. The drag now previews
    // against these and commits once on release; EditorState.previewTrim* runs the same
    // computation the commit will, so the clip does not move when it lands.
    property bool trimPreviewActive: false
    property real trimPreviewStart: 0
    property real trimPreviewDuration: 0
    property real trimPreviewIn: 0
    property real trimPreviewOut: 0

    // The A/V companion of the clip being trimmed. The commit hands it identical timing, so it
    // follows the same preview rather than sitting still until release.
    readonly property bool trimFollowFollower: panel.trimFollowActive
                                               && !!clipData.linkId
                                               && clipData.linkId === panel.trimFollowLinkId
                                               && clipData.id !== panel.trimFollowClipId
    // A clip after the one being trimmed, pushed along by ripple.
    readonly property bool rippleShifted: panel.rippleShiftIds.length > 0
                                          && panel.rippleShiftMap[clipData.id] === true

    function syncLivePreview() {
        if (trackClips)
            trackClips.setLivePreview(clipData.id, trimPreviewActive, trimPreviewStart,
                                      trimPreviewDuration, trimPreviewIn, trimPreviewOut)
    }
    onTrimPreviewActiveChanged: syncLivePreview()
    onTrimPreviewStartChanged: syncLivePreview()
    onTrimPreviewDurationChanged: syncLivePreview()
    onTrimPreviewInChanged: syncLivePreview()
    onTrimPreviewOutChanged: syncLivePreview()

    readonly property real effectiveStart: trimPreviewActive ? trimPreviewStart
                                           : trimFollowFollower ? panel.trimFollowStart
                                           : rippleShifted ? (clipData.start || 0) + panel.rippleShiftDelta
                                           : (clipData.start || 0)
    readonly property real effectiveDuration: trimPreviewActive ? trimPreviewDuration
                                              : trimFollowFollower ? panel.trimFollowDuration
                                              : (clipData.duration || 0)

    // The bin asset behind this clip, by path: the clip model carries no asset id.
    readonly property string mediaAssetId: (AssetLibrary.badgeRevision,
                                            clipData.kind === "video"
                                            ? AssetLibrary.assetIdForPath(clipData.path || "") : "")
    readonly property bool mediaEditFriendly: (AssetLibrary.badgeRevision,
                                               mediaAssetId.length > 0
                                               && AssetLibrary.isEditFriendly(mediaAssetId))
    property string trackType: panel.tracks[trackIndex]?.type ?? ""
    property var clipEffects: clipData.effects || []
    property var clipAudioEffects: clipData.audioEffects || []
    readonly property bool hasAnyEffects: clipEffects.length > 0 || clipAudioEffects.length > 0
    readonly property bool isTransformClip: clipData.kind === "adjustment"
                                            && clipData.adjustmentKind === "transform"
    // A transform layer moves this clip's track.
    readonly property bool hasTransformParent:
        (panel.tracks[trackIndex]?.transformCoveredBy || []).length > 0
    // Named so tooling (and a screen reader) can address a clip by what the user sees on it
    // rather than by pixel position. The timeline is the app's main interaction surface and had
    // no accessible identity at all.
    Accessible.role: Accessible.Button
    Accessible.name: qsTr("%1, track %2").arg(clipItem.clipData.name || "").arg(clipItem.trackIndex + 1)
    Accessible.selected: clipItem.selected
    Accessible.onPressAction: EditorState.selectClip(clipItem.trackIndex, clipItem.clipIndex)

    // Subtitles keep cue-owned timing; text clips use the same edge fades as video. On touch the
    // fades live in the toolbar's Fade sheet: a dot a finger can hit sits right next to the trim
    // handle, and one of the two always got grabbed instead of the other.
    readonly property bool timelineFadeHandles: trackType !== "subtitle" && !touchMode

    // Four ToolTips — each a Popup — existed on every clip in the project for the sake of the
    // one clip the pointer is actually over. Gated on the clip being touched at all; each
    // tooltip keeps its own visible binding inside, so the reveal delay and the conditions are
    // exactly what they were.
    readonly property bool tooltipsActive:
        leftTrimHandle.areaContainsMouse || leftTrimHandle.areaPressed || leftTrimHandle.areaHovered
        || rightTrimHandle.areaContainsMouse || rightTrimHandle.areaPressed || rightTrimHandle.areaHovered
        || fadeInHandle.areaContainsMouse || fadeInHandle.areaPressed
        || fadeOutHandle.areaContainsMouse || fadeOutHandle.areaPressed

    // Arms the Loader holding the context menu and pops it. Re-pops an already-loaded menu
    // rather than reloading it, for the second right-click that lands while the first is still
    // closing.
    function openContextMenu() {
        if (clipContextMenuLoader.active && clipContextMenuLoader.item) {
            popupContextMenu(clipContextMenuLoader.item)
            return
        }
        clipContextMenuLoader.active = true
    }

    // A finger leaves no cursor position behind, so the menu opens where the press landed.
    function popupContextMenu(menu) {
        if (trackClips && trackClips.lastPressTouch && !touchMode)
            menu.popup(trackClips, trackClips.dragPointer.x, trackClips.dragPointer.y)
        else
            menu.popup()
    }

    // Premiere-style trim pointer (vertical bar + arrow), sized to this clip.
    //
    // A press owns the cursor for the whole gesture. containsMouse goes false the moment the
    // pointer leaves the strip, and during a trim the handle geometry trails the pointer by a
    // frame — so it flapped, and every flap was a restoreOverrideCursor()/setOverrideCursor()
    // pair with a freshly rasterised pixmap behind it.
    readonly property int trimCursorSide: leftTrimHandle.areaPressed ? -1
                                          : rightTrimHandle.areaPressed ? 1
                                          : leftTrimHandle.areaContainsMouse ? -1
                                          : rightTrimHandle.areaContainsMouse ? 1 : 0
    readonly property int trimCursorHeight: Math.round(height)
    // Identifies this clip to the shared override cursor. Not derived from trackIndex/clipIndex:
    // those shift when a clip is inserted or removed, which would orphan a held cursor.
    property int trimCursorToken: 0
    function applyTrimCursor() {
        if (trimCursorSide !== 0 && trimCursorToken === 0)
            trimCursorToken = EditorState.acquireTrimCursorToken()
        EditorState.setTimelineTrimCursor(trimCursorSide, trimCursorHeight, trimCursorToken)
        if (trimCursorSide === 0)
            trimCursorToken = 0
    }
    onTrimCursorSideChanged: applyTrimCursor()
    onTrimCursorHeightChanged: if (trimCursorSide !== 0) applyTrimCursor()
    Component.onDestruction: {
        if (trimPreviewActive && trackClips)
            trackClips.setLivePreview(clipData.id, false, 0, 0, 0, 0)
        if (trimCursorSide !== 0)
            EditorState.setTimelineTrimCursor(0, 0, trimCursorToken)
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
        1.0 / Math.max(1, EditorState.projectFile.projectFps()),
        Theme.clipMinWidth / panel.pxPerSecond)
    // On touch the bars give way on a narrow clip, so its middle — where a long-press picks it
    // up — never shrinks to a sliver between two trim handles. The grab outside the clip stays.
    readonly property real trimHandleWidth: touchMode
                                            ? Math.min(Theme.androidClipTrimHandleWidth,
                                                       Math.max(6, width * 0.25))
                                            : Theme.clipTrimHandleWidth
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
        // A pointer has a scroll wheel and never needed this; starting it already decided keeps
        // every branch below dead for it. A finger on the desktop timeline arbitrates too.
        edgeAxis = (touchMode || mouse.source !== Qt.MouseEventNotSynthesized) ? 0 : 1
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

    // Fade dots, pointer only (see timelineFadeHandles): at the clip's top corners, where the trim
    // strips leave room for them.
    readonly property real fadeHandleSize: 13
    readonly property real fadeHandleInset: 0
    readonly property real fadeHandleMinWidth: 26

    // Floored so short clips stay visible and
    // trimmable even at low zoom.
    width: Math.max(Theme.clipMinWidth,
                    effectiveDuration * panel.pxPerSecond
                    - 2 * Theme.clipSelectionRingWidth)
    height: Math.max(0, trackRow.height - 2 * Theme.clipSelectionRingWidth)

    // Selected clips slide with a move the renderer is dragging — the dragged one included, since
    // its handles have to ride along with it.
    readonly property bool moveFollowFollower: panel.moveFollowActive && selected
    readonly property real followOffsetX: moveFollowFollower ? panel.moveFollowDeltaX : 0
    readonly property real followOffsetY: (moveFollowFollower && trackIndex === panel.moveLeaderTrack)
                                          ? panel.moveFollowDeltaY : 0

    x: Math.max(Theme.clipSelectionRingWidth,
                effectiveStart * panel.pxPerSecond + Theme.clipSelectionRingWidth + followOffsetX)
    y: Theme.clipSelectionRingWidth + followOffsetY

    // Override point for the clip body. The default body is painted by
    // TimelineTrackClips; this slot draws nothing unless a theme supplies one.
    ThemeSlot {
        slot: "TimelineClipBody"
        control: clipItem
        anchors.fill: parent
        fallback: Component {
            Item { }
        }
    }

    // Stands in for the rect the Menu used to be parented to, so it resolves the same parent item
    // and clamps against the same bounds.
    Loader {
        id: clipContextMenuLoader
        anchors.fill: parent
        active: false
        asynchronous: false
        sourceComponent: Component {
            TimelineClipContextMenu {
                owner: clipItem
                // Deferred: onClosed runs inside the Popup's own close path, and dropping the
                // Loader's item there would destroy an object still unwinding.
                onUnloadRequested: Qt.callLater(function() { clipContextMenuLoader.active = false })
            }
        }
        onLoaded: clipItem.popupContextMenu(item)
    }

    // Fade dots sit above trim handles so they stay
    // grabable at the corners (zero fade). Trim the
    // edge below the dots; grab the dots to fade.
    TimelineFadeHandle {
        id: fadeInHandle
        owner: clipItem
    }
    TimelineFadeHandle {
        id: fadeOutHandle
        owner: clipItem
        outgoing: true
    }

    TimelineTrimHandle {
        id: leftTrimHandle
        owner: clipItem
        edge: -1
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
    }
    TimelineTrimHandle {
        id: rightTrimHandle
        owner: clipItem
        edge: 1
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
    }
}
