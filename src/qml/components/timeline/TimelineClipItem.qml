import QtQuick
import Drift
import ".."

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
    property string trackType: panel.tracks[trackIndex].type
    property var clipEffects: clipData.effects || []
    property var clipAudioEffects: clipData.audioEffects || []
    readonly property bool hasAnyEffects: clipEffects.length > 0 || clipAudioEffects.length > 0
    // Named so tooling (and a screen reader) can address a clip by what the user sees on it
    // rather than by pixel position. The timeline is the app's main interaction surface and had
    // no accessible identity at all.
    Accessible.role: Accessible.Button
    Accessible.name: qsTr("%1, track %2").arg(clipItem.clipData.name || "").arg(clipItem.trackIndex + 1)
    Accessible.selected: clipItem.selected
    Accessible.onPressAction: EditorState.selectClip(clipItem.trackIndex, clipItem.clipIndex)

    // Subtitles keep cue-owned timing; text clips use the same edge fades as video.
    readonly property bool timelineFadeHandles: trackType !== "subtitle"

    // Four ToolTips — each a Popup — existed on every clip in the project for the sake of the
    // one clip the pointer is actually over. Gated on the clip being touched at all; each
    // tooltip keeps its own visible binding inside, so the reveal delay and the conditions are
    // exactly what they were.
    readonly property bool tooltipsActive:
        leftTrimMouse.containsMouse || leftTrimMouse.pressed || leftTrimHover.hovered
        || rightTrimMouse.containsMouse || rightTrimMouse.pressed || rightTrimHover.hovered
        || fadeInMouse.containsMouse || fadeInMouse.pressed
        || fadeOutMouse.containsMouse || fadeOutMouse.pressed

    // Arms the Loader holding the context menu and pops it. Re-pops an already-loaded menu
    // rather than reloading it, for the second right-click that lands while the first is still
    // closing.
    function openContextMenu() {
        if (clipContextMenuLoader.active && clipContextMenuLoader.item) {
            clipContextMenuLoader.item.popup()
            return
        }
        clipContextMenuLoader.active = true
    }

    // Premiere-style trim pointer (vertical bar + arrow), sized to this clip.
    //
    // A press owns the cursor for the whole gesture. containsMouse goes false the moment the
    // pointer leaves the strip, and during a trim the handle geometry trails the pointer by a
    // frame — so it flapped, and every flap was a restoreOverrideCursor()/setOverrideCursor()
    // pair with a freshly rasterised pixmap behind it.
    readonly property int trimCursorSide: leftTrimMouse.pressed ? -1
                                          : rightTrimMouse.pressed ? 1
                                          : leftTrimMouse.containsMouse ? -1
                                          : rightTrimMouse.containsMouse ? 1 : 0
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
        Theme.clipMinDurationSeconds,
        Theme.clipMinWidth / panel.pxPerSecond)
    readonly property real trimHandleWidth: touchMode
                                            ? Theme.androidClipTrimHandleWidth
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

    // Stands in for the rect the Menu used to be parented to, so it resolves the same parent item
    // and clamps against the same bounds.
    Loader {
        id: clipContextMenuLoader
        anchors.fill: parent
        active: false
        asynchronous: false
        sourceComponent: clipContextMenuComponent
        onLoaded: item.popup()
    }

    Component {
        id: clipContextMenuComponent

        ThemedContextMenu {
            id: clipContextMenu

            property bool canPasteEffects: false
            property bool canPasteAttributes: false
            property bool canMergeTrackSubtitles: false
            onAboutToShow: {
                canPasteEffects = EditorState.clipboardHasEffects()
                canPasteAttributes = EditorState.canPasteAttributes()
                canMergeTrackSubtitles = clipItem.trackType === "subtitle"
                        && EditorState.canMergeAllSubtitlesOnTrack(clipItem.trackIndex)
            }
            // Deferred: onClosed runs inside the Popup's own close path, and dropping the
            // Loader's item there would destroy an object still unwinding.
            onClosed: Qt.callLater(function() { clipContextMenuLoader.active = false })

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
            ThemedMenuItem {
                text: qsTr("Open composite")
                icon.name: Theme.icons.layers
                visible: clipItem.clipData.kind === "composite"
                onTriggered: EditorState.openCompositeClip(clipItem.trackIndex, clipItem.clipIndex)
            }
            ThemedMenuItem {
                text: qsTr("Flatten composite")
                icon.name: Theme.icons.film
                visible: clipItem.clipData.kind === "composite"
                enabled: !EditorState.exportInProgress
                onTriggered: EditorState.flattenComposite(clipItem.trackIndex, clipItem.clipIndex)
            }
            ThemedMenuItem {
                text: qsTr("Make composite")
                icon.name: Theme.icons.layers
                visible: EditorState.makeCompositeAvailable
                onTriggered: EditorState.makeCompositeFromSelection()
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
                text: qsTr("Convert to edit-friendly format")
                icon.name: Theme.icons.rabbit
                // Converts the bin media, so every clip using it follows along.
                visible: clipItem.mediaAssetId.length > 0 && !clipItem.mediaEditFriendly
                onTriggered: EditorState.convertAssetsToConstantFrameRate([clipItem.mediaAssetId])
            }
            ThemedMenuItem {
                text: qsTr("Unlink")
                icon.name: Theme.icons.unlink
                visible: !!clipItem.clipData.linked && EditorState.unlinkAvailable
                onTriggered: EditorState.unlinkSelectedClips()
            }
            ThemedMenuItem {
                text: qsTr("Merge subtitle clips")
                icon.name: Theme.icons.linkTwo
                visible: clipItem.trackType === "subtitle"
                enabled: EditorState.mergeAvailable
                onTriggered: EditorState.mergeSelectedClips()
            }
            ThemedMenuItem {
                text: qsTr("Merge all subtitles on this track")
                icon.name: Theme.icons.linkTwo
                visible: clipItem.trackType === "subtitle"
                enabled: clipContextMenu.canMergeTrackSubtitles
                onTriggered: EditorState.mergeAllSubtitlesOnTrack(clipItem.trackIndex)
            }
            ThemedMenuItem {
                text: qsTr("Convert to text clips")
                icon.name: Theme.icons.type
                visible: clipItem.trackType === "subtitle"
                onTriggered: EditorState.convertSubtitleToTextClips(clipItem.trackIndex, clipItem.clipIndex)
            }
            ThemedMenuItem {
                text: qsTr("Convert to subtitle")
                icon.name: Theme.icons.captions
                visible: clipItem.trackType === "text" && EditorState.textToSubtitleAvailable
                onTriggered: clipItem.panel.requestConvertTextToSubtitle()
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

            Loader {
                anchors.fill: parent
                active: clipItem.tooltipsActive
                sourceComponent: Component {
                    ThemedToolTip {
                        visible: fadeInMouse.pressed || fadeInMouse.containsMouse
                        text: qsTr("Fade in %1s").arg((clipItem.clipData.fadeIn || 0).toFixed(2))
                    }
                }
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

            Loader {
                anchors.fill: parent
                active: clipItem.tooltipsActive
                sourceComponent: Component {
                    ThemedToolTip {
                        visible: fadeOutMouse.pressed || fadeOutMouse.containsMouse
                        text: qsTr("Fade out %1s").arg((clipItem.clipData.fadeOut || 0).toFixed(2))
                    }
                }
            }
        }
    }

    Rectangle {
        id: leftTrimHandle
        // Thin edge bar; hotspots still use the wide Theme width when idle.
        width: (leftTrimMouse.containsMouse || leftTrimHover.hovered || leftTrimMouse.pressed)
               ? Math.max(2, clipItem.trimHandleWidth * 0.35)
               : clipItem.trimHandleWidth
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
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

        Loader {
            anchors.fill: parent
            active: clipItem.tooltipsActive
            sourceComponent: Component {
                ThemedToolTip {
                    text: qsTr("Drag to trim the start")
                    visible: clipItem.showTrimHandles
                             && (leftTrimMouse.containsMouse || leftTrimHover.hovered)
                             && !leftTrimMouse.pressed
                }
            }
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

            onPressed: (mouse) => {
                Qt.callLater(function() { clipItem.forceActiveFocus() })
                const wasSelected = clipItem.selected
                Haptics.reset()
                clipItem.beginEdgeGesture(leftTrimMouse, mouse)
                if (!clipItem.selected)
                    EditorState.selectClip(clipItem.trackIndex, clipItem.clipIndex)
                if (!wasSelected)
                    Haptics.select()
                // One undo entry and one dirty mark for the whole drag. Without this a trim was
                // invisible to both: it never moved the undo stack index, and the dirty flag is
                // only ever set from that index changing — so trimming, closing, and being asked
                // nothing about saving lost the work outright.
                EditorState.beginPreviewDrag(qsTr("Trim clip"))
                EditorState.beginTrimGesture(clipItem.trackIndex, clipItem.clipIndex, -1)
                lastTrimPx = -2147483647
                lastTrimSeconds = -1
                trimming = true
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
                const newStart = Math.max(0, Math.min(raw, end - clipItem.minDurationSeconds))
                const px = Math.round(newStart * panel.pxPerSecond)
                if (px === lastTrimPx)
                    return
                lastTrimPx = px
                lastTrimSeconds = newStart
                // Previewed, not applied. The reply reports what the trim would do -- snapping
                // and every limit that can stop this edge live inside it, and the one the user
                // needs told, the source running out, has no cue on screen at all.
                const p = EditorState.previewTrimLeft(clipItem.trackIndex, clipItem.clipIndex,
                                                      newStart)
                if (p.ok && p.changed) {
                    clipItem.trimPreviewStart = p.start
                    clipItem.trimPreviewDuration = p.duration
                    clipItem.trimPreviewIn = p.inPoint
                    clipItem.trimPreviewOut = p.outPoint
                    clipItem.trimPreviewActive = true
                    panel.setTrimFollow(clipItem.clipData.linkId, clipItem.clipData.id,
                                        p.start, p.duration, p.inPoint, p.outPoint)
                }
                Haptics.trimStep(p.ok ? p.outcome : 0)
            }
            onReleased: {
                Haptics.reset()
                if (trimming) {
                    trimming = false
                    // The one and only edit of the gesture. Dropping the preview first so the
                    // geometry bindings are reading the project again by the time it lands.
                    clipItem.trimPreviewActive = false
                    panel.clearTrimFollow()
                    // One call, so the edit, the undo push and finishEdit announce the timeline
                    // once between them rather than three times.
                    EditorState.commitTrim(clipItem.trackIndex, clipItem.clipIndex,
                                           -1, lastTrimSeconds)
                }
            }
            onCanceled: {
                Haptics.reset()
                if (trimming) {
                    trimming = false
                    clipItem.trimPreviewActive = false
                    panel.clearTrimFollow()
                    EditorState.endTrimGesture()
                    EditorState.cancelPreviewDrag()
                }
            }
        }
    }

    Rectangle {
        id: rightTrimHandle
        width: (rightTrimMouse.containsMouse || rightTrimHover.hovered || rightTrimMouse.pressed)
               ? Math.max(2, clipItem.trimHandleWidth * 0.35)
               : clipItem.trimHandleWidth
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
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

        Loader {
            anchors.fill: parent
            active: clipItem.tooltipsActive
            sourceComponent: Component {
                ThemedToolTip {
                    text: qsTr("Drag to trim the end")
                    visible: clipItem.showTrimHandles
                             && (rightTrimMouse.containsMouse || rightTrimHover.hovered)
                             && !rightTrimMouse.pressed
                }
            }
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

            // See the matching properties on the left handle.
            property int lastTrimPx: -2147483647
            // Where the edge was last asked to go. Negative means the gesture never moved it,
            // so there is nothing to commit.
            property real lastTrimSeconds: -1
            property bool trimming: false

            onPressed: (mouse) => {
                Qt.callLater(function() { clipItem.forceActiveFocus() })
                const wasSelected = clipItem.selected
                Haptics.reset()
                clipItem.beginEdgeGesture(rightTrimMouse, mouse)
                if (!clipItem.selected)
                    EditorState.selectClip(clipItem.trackIndex, clipItem.clipIndex)
                if (!wasSelected)
                    Haptics.select()
                EditorState.beginPreviewDrag(qsTr("Trim clip"))
                EditorState.beginTrimGesture(clipItem.trackIndex, clipItem.clipIndex, 1)
                lastTrimPx = -2147483647
                lastTrimSeconds = -1
                trimming = true
            }
            onPositionChanged: (mouse) => {
                if (!pressed)
                    return
                if (clipItem.edgeGestureScrolled(rightTrimMouse, mouse))
                    return
                const start = clipItem.clipData.start || 0
                const raw = mapToItem(trackRow, mouse.x, mouse.y).x / panel.pxPerSecond
                const newEnd = Math.max(raw, start + clipItem.minDurationSeconds)
                const px = Math.round(newEnd * panel.pxPerSecond)
                if (px === lastTrimPx)
                    return
                lastTrimPx = px
                lastTrimSeconds = newEnd
                // See the left handle: previewed here, committed once on release.
                const p = EditorState.previewTrimRight(clipItem.trackIndex, clipItem.clipIndex,
                                                       newEnd)
                if (p.ok && p.changed) {
                    clipItem.trimPreviewStart = p.start
                    clipItem.trimPreviewDuration = p.duration
                    clipItem.trimPreviewIn = p.inPoint
                    clipItem.trimPreviewOut = p.outPoint
                    clipItem.trimPreviewActive = true
                    panel.setTrimFollow(clipItem.clipData.linkId, clipItem.clipData.id,
                                        p.start, p.duration, p.inPoint, p.outPoint)
                }
                Haptics.trimStep(p.ok ? p.outcome : 0)
            }
            onReleased: {
                Haptics.reset()
                if (trimming) {
                    trimming = false
                    // The one and only edit of the gesture. Dropping the preview first so the
                    // geometry bindings are reading the project again by the time it lands.
                    clipItem.trimPreviewActive = false
                    panel.clearTrimFollow()
                    // One call, so the edit, the undo push and finishEdit announce the timeline
                    // once between them rather than three times.
                    EditorState.commitTrim(clipItem.trackIndex, clipItem.clipIndex,
                                           1, lastTrimSeconds)
                }
            }
            onCanceled: {
                Haptics.reset()
                if (trimming) {
                    trimming = false
                    clipItem.trimPreviewActive = false
                    panel.clearTrimFollow()
                    EditorState.endTrimGesture()
                    EditorState.cancelPreviewDrag()
                }
            }
        }
    }
}
