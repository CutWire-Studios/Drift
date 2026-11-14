import QtQuick
import Drift

// One track header: reorder drag, chrome, toggles, name, and the context menu.
Item {
    id: trackLabelRow

    property var column
    property var renameDialog
    property var deleteDialog
    required property int index

    Accessible.role: Accessible.Row
    Accessible.name: trackLabelRow.trackDisplayName
    // Read through column.tracks (not the EditorState
    // getters) so the toggles rebind on tracksChanged.
    readonly property bool trackMuted: column.tracks[index].muted === true
    readonly property bool trackHidden: column.tracks[index].hidden === true
    readonly property bool trackLocked: column.tracks[index].locked === true
    readonly property bool trackExcludedFromExport: column.tracks[index].excludeFromExport === true
    readonly property bool trackSolo: column.tracks[index].solo === true
    readonly property int clipDisplay: column.tracks[index].clipDisplay !== undefined
                                       ? column.tracks[index].clipDisplay : 1
    readonly property bool trackWaveform: clipDisplay === 2
    // Falls back to the type+position label until a custom name is set.
    readonly property string trackDisplayName:
        column.tracks[index].name && column.tracks[index].name.length > 0
        ? column.tracks[index].name
        : column.tracks[index].isCameraLayer === true
          ? column.trackTypeLabel(column.tracks[index].type, false, true)
          : column.trackTypeLabel(column.tracks[index].type, column.tracks[index].isTransformLayer)
            + " " + column.trackTypeOrdinal(index)
    // Same, but falling back to the short "V1"/"A2" form the compact header uses when
    // there's no custom name to show instead.
    readonly property string trackCompactLabel:
        column.tracks[index].name && column.tracks[index].name.length > 0
        ? column.tracks[index].name
        : column.tracks[index].isCameraLayer === true
          ? column.trackTypeShortLabel(column.tracks[index].type, false, true)
          : column.trackTypeShortLabel(column.tracks[index].type, column.tracks[index].isTransformLayer)
            + column.trackTypeOrdinal(index)
    readonly property bool isTransformLayer: column.tracks[index].isTransformLayer === true
    readonly property bool isCameraLayer: column.tracks[index].isCameraLayer === true
    readonly property bool transformable: ["video", "text", "subtitle", "shape"]
                                          .indexOf(column.tracks[index].type) !== -1
    readonly property bool insideBracket: (column.tracks[index].transformCoveredBy || []).length > 0
    // Tinted while a bracket foot is dragged over it: this is what the span would cover.
    readonly property bool spanPreviewed: column.spanPreviewLayer >= 0
                                          && index > column.spanPreviewLayer
                                          && index <= column.spanPreviewEnd
    readonly property real rowTop: column.trackRowTop(index)
    readonly property bool selected: {
        if (column.compact)
            return false
        if (EditorState.selectedTrack === index)
            return true
        return EditorState.selectionRevision >= 0 && EditorState.selectionContainsTrack(index)
    }
    // On desktop the lanes sit below the parent's own content, or collapse into ribbons along
    // the top. The compact (phone) header keeps them as full strips at the top.
    readonly property int laneCount: {
        const dep = column.tracks.length
        return EditorState.clipsModel(index).adjustmentLanes.length
    }
    readonly property real rowContentHeight: column.trackHeight(index)
    readonly property bool lanesCollapsed: !column.compact && column.tracks[index].lanesCollapsed === true
    readonly property real lanesPx: laneCount * (lanesCollapsed ? Theme.adjustmentRibbonHeight
                                                                : Theme.adjustmentLaneHeight)
    readonly property real contentTop: column.compact ? lanesPx : 0
    readonly property real contentHeight: rowContentHeight - lanesPx
    readonly property bool twoLine: !column.compact && contentHeight >= 44 && (wantMic || wantLanes)
    readonly property real line1Height: twoLine ? 24 : contentHeight
    readonly property real line2Height: twoLine ? 22 : 0
    readonly property real blockTop: contentTop + Math.min(3, Math.max(0, contentHeight - line1Height - line2Height))
    readonly property real laneLineX: 30
    readonly property real centerOffset: contentTop + contentHeight / 2 - height / 2
    readonly property string hideLabel: isTransformLayer
        ? (trackHidden ? qsTr("Turn transform on") : qsTr("Turn transform off"))
        : column.tracks[index].type === "adjustment"
          ? (trackHidden ? qsTr("Turn adjustment on") : qsTr("Turn adjustment off"))
          : (trackHidden ? qsTr("Show track") : qsTr("Hide track"))

    x: column.bracketGutter
    width: column.labelsWidth - column.bracketGutter
    // A lane draws inside its parent's row, so it gets no header of its own.
    visible: column.trackOccupiesARow(index)
    height: column.trackHeight(index)
            + (index < column.tracks.length - 1 ? Theme.trackGap : 0)
    // Split deliberately: trackRowTop() walks every track ahead of this one, so binding
    // it straight into `y` re-ran that walk for every header on every scroll frame. The
    // walk now depends only on the track model; the per-frame part is one subtraction.
    // Follows the timeline's vertical scroll so labels stay
    // aligned with their rows.
    y: rowTop - column.contentY
    opacity: column.draggingTrackFrom === index ? 0.45 : 1.0
    clip: true

    readonly property string trackType: column.tracks[index].type
    readonly property bool wantSolo: trackType === "audio"
                                     || (trackType === "video" && column.tracks[index].hasAudio === true)
    readonly property bool wantMute: trackType === "video" || trackType === "audio"
    readonly property bool wantHide: trackType === "video" || trackType === "adjustment"
                                     || trackType === "text" || trackType === "subtitle"
                                     || trackType === "shape" || isTransformLayer
    readonly property bool wantMic: trackType === "audio"
                                    && column.tracks[index].isAdjustmentLane !== true
    readonly property int laneChannels: {
        const dep = column.tracks.length
        return EditorState.trackMaxChannelCount(index)
    }
    readonly property bool wantLanes: (trackType === "audio" || (trackType === "video" && trackWaveform))
                                      && laneChannels > 1
    readonly property bool lanesOn: column.tracks[index].showChannelWaveforms === true
    readonly property bool isRecordingHere:
        EditorState.isRecordingAudio && EditorState.recordingTrackIndex === index
    readonly property bool wantClip: !column.compact && trackType === "video"
    // Toggles drop out lowest-priority first as the header narrows; whatever does not fit is
    // reachable from the overflow button, which opens the context menu.
    readonly property var iconFit: {
        if (!column.compact)
            return { overflow: false, mute: false, hide: false, mic: false, lanes: false, clip: false }
        const wants = [wantMute, wantHide, wantMic, wantLanes, wantClip]
        const spacing = trackIconRow.spacing
        const avail = width - 16 - 4 - 8
        const cap = Math.max(0, Math.floor((avail + spacing) / (16 + spacing)))
        let total = 0
        for (const w of wants)
            total += w ? 1 : 0
        const overflow = total > cap
        let room = overflow ? Math.max(0, cap - 1) : cap
        const fit = { overflow: overflow }
        const keys = ["mute", "hide", "mic", "lanes", "clip"]
        for (let i = 0; i < wants.length; ++i) {
            fit[keys[i]] = wants[i] && room > 0
            if (fit[keys[i]])
                --room
        }
        return fit
    }

    ThemeSlot {
        slot: "TrackHeaderRowBackground"
        control: trackLabelRow
        anchors.fill: parent
        z: -1
        fallback: Component {
            Rectangle {
                anchors.fill: parent
                color: Theme.c.trackHeaderRow.background
            }
        }
    }

    // Reorder drag. Covers the whole header rather than just the grip:
    // reaching for the header body is the instinctive gesture, and the
    // grip alone was a ~22px target that was easy to miss entirely.
    // Declared first so it sits below the toggle buttons and the
    // right-click menu, which keep their own presses.
    MouseArea {
        id: trackHeaderDrag
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        TimelineCursorRequest {
            kind: "trackReorder"
            active: column.draggingTrackFrom === index
        }

        // A press is not yet a reorder: without a threshold, clicking a
        // header committed a move, because a row's grip already sits
        // past its own midpoint.
        property real pressY: 0
        property bool moved: false
        // Touch only: the hold was recognised, so this row is now draggable.
        // Letting go without moving asks for the context menu instead.
        property bool armed: false
        // Latched per press: the device that pressed decides, not Theme.touchInput.
        property bool pressedByTouch: false
        readonly property bool touchHold: column.touchMode || pressedByTouch
        readonly property real threshold: 4

        // On touch the reorder waits for a hold. Arming on press meant any 4px
        // of finger travel reordered the tracks, and the header column could
        // never be used to scroll the timeline vertically.
        pressAndHoldInterval: 400
        preventStealing: touchHold ? armed : true

        onPressed: (mouse) => {
            pressedByTouch = mouse.source !== Qt.MouseEventNotSynthesized
            pressY = mouse.y
            moved = false
            armed = false
            if (touchHold)
                return
            column.draggingTrackFrom = index
            column.draggingTrackTo = index
            column.draggingTrackSlot = -1
        }
        onPressAndHold: (mouse) => {
            if (!touchHold || moved)
                return
            pressY = mouse.y
            armed = true
            column.draggingTrackFrom = index
            column.draggingTrackTo = index
            column.draggingTrackSlot = -1
            Haptics.pickUp()
        }
        onPositionChanged: (mouse) => {
            if (column.draggingTrackFrom < 0)
                return
            if (!moved && Math.abs(mouse.y - pressY) < threshold)
                return
            moved = true
            const local = mapToItem(column, mouse.x, mouse.y)
            // Rows are drawn at trackRowTop(i) - contentY, so `local` is view
            // space while trackInsertSlotAtY walks content offsets. Scrolled, the
            // drop landed on whichever row happened to sit at that screen y.
            column.draggingTrackSlot = column.trackInsertSlotAtY(local.y + column.contentY)
            column.draggingTrackTo = column.trackMoveTargetForSlot(
                                         column.draggingTrackFrom,
                                         column.draggingTrackSlot)
        }
        onReleased: {
            const wantsMenu = touchHold && armed && !moved
            armed = false
            if (wantsMenu) {
                column.clearTrackDrag()
                trackContextMenu.popup(trackHeaderDrag, trackHeaderDrag.mouseX, trackHeaderDrag.mouseY)
                return
            }
            if (moved && column.draggingTrackFrom >= 0
                    && column.draggingTrackTo >= 0
                    && column.draggingTrackFrom !== column.draggingTrackTo) {
                Haptics.confirm()
                EditorState.moveTrack(column.draggingTrackFrom, column.draggingTrackTo)
            } else {
                Haptics.drop()
            }
            column.clearTrackDrag()
        }
        onCanceled: {
            armed = false
            column.clearTrackDrag()
        }
    }

    TrackHeaderBottomResize {
        visible: !column.compact
        column: trackLabelRow.column
        trackIndex: index
        lanesPx: trackLabelRow.lanesPx
    }

    TrackHeaderEdge {
        column: trackLabelRow.column
        trackIndex: index
    }

    TrackHeaderWash {
        spanPreviewed: trackLabelRow.spanPreviewed
        transformLayer: trackLabelRow.isTransformLayer
        rowHeight: trackLabelRow.column.trackHeight(index)
    }

    TrackHeaderStateTint {
        visible: !column.compact
        hovered: trackHeaderDrag.containsMouse
        selected: trackLabelRow.selected
        locked: trackLabelRow.trackLocked
        rowHeight: trackLabelRow.column.trackHeight(index)
    }

    Loader {
        anchors.fill: parent
        active: trackLabelRow.trackHidden
        sourceComponent: TimelineDisabledStripes { opacity: 0.6 }
    }

    TrackHeaderTypeStrip {
        trackType: trackLabelRow.trackType
        transformLayer: trackLabelRow.isTransformLayer
        rowHeight: trackLabelRow.column.trackHeight(index)
        opacity: trackLabelRow.trackHidden ? 0.4 : 1
    }

    TrackHeaderSeparator {
        visible: !column.compact && index < column.tracks.length - 1
        rowHeight: trackLabelRow.column.trackHeight(index)
    }

    TrackHeaderGrip {
        id: grip
        shown: !(column.compact && trackLabelRow.insideBracket)
        compact: column.compact
        hot: grip.hovered
        dragging: column.draggingTrackFrom === index
        headerHovered: trackHeaderDrag.containsMouse
        centerOffset: column.compact ? trackLabelRow.centerOffset
                                     : trackLabelRow.blockTop + trackLabelRow.line1Height / 2 - trackLabelRow.height / 2
    }

    // Compact: one eye per lane, level with its strip at the top of the row.
    Column {
        visible: column.compact
        anchors.right: parent.right
        anchors.rightMargin: 4
        y: 0
        Repeater {
            model: column.compact ? EditorState.clipsModel(index).adjustmentLanes : 0
            delegate: TrackHeaderLaneEye {
                column: trackLabelRow.column
            }
        }
    }

    Rectangle {
        visible: !column.compact && !lanesCollapsed && laneCount > 0
        x: trackLabelRow.laneLineX
        y: Math.ceil(trackLabelRow.blockTop + trackLabelRow.line1Height / 2 + 7)
        width: 1
        height: Math.max(0, trackLabelRow.contentHeight - y)
        color: Theme.c.trackHeaderLaneRow.connector
    }

    Repeater {
        model: column.compact || lanesCollapsed ? 0 : EditorState.clipsModel(index).adjustmentLanes
        delegate: TrackHeaderLaneRow {
            row: trackLabelRow
            y: trackLabelRow.contentHeight + index * Theme.adjustmentLaneHeight
            count: trackLabelRow.laneCount
        }
    }

    Row {
        id: trackIconRow
        visible: column.compact
        anchors.right: parent.right
        anchors.rightMargin: column.compact ? 4 : 12
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: trackLabelRow.centerOffset
        // Each toggle's hit area overhangs its glyph by 4 either side, so a 4px
        // gap left the mute and hide targets touching: half the visible gap fired
        // the wrong one, and both silently drop the track from render and export.
        // 12 leaves a 4px dead band between them.
        spacing: column.touchMode ? 12 : (column.compact ? 4 : 8)

        TrackHeaderMicButton {
            trackIndex: index
            shown: trackLabelRow.iconFit.mic
        }
        TrackHeaderMuteButton {
            trackIndex: index
            shown: trackLabelRow.iconFit.mute
            trackMuted: trackLabelRow.trackMuted
        }
        TrackHeaderChannelLanesButton {
            column: trackLabelRow.column
            trackIndex: index
            shown: trackLabelRow.iconFit.lanes
        }
        TrackHeaderHideButton {
            trackIndex: index
            shown: trackLabelRow.iconFit.hide
            trackHidden: trackLabelRow.trackHidden
            hideLabel: trackLabelRow.hideLabel
        }
        TrackHeaderClipDisplayButton {
            column: trackLabelRow.column
            trackIndex: index
            clipDisplay: trackLabelRow.clipDisplay
            shown: trackLabelRow.iconFit.clip
        }
        TrackHeaderOverflowButton {
            id: overflowButton
            shown: trackLabelRow.iconFit.overflow
            onActivated: trackContextMenu.popup(overflowButton, 0, overflowButton.height)
        }
    }

    Loader {
        active: !column.compact
        anchors.fill: parent
        sourceComponent: Component {
            TrackHeaderControls {
                row: trackLabelRow
                onOverflowRequested: (anchor) => trackContextMenu.popup(anchor, 0, anchor.height)
            }
        }
    }

    // Compact stand-in for the glyph and the name band: sits in the gap
    // between the grip and the mute/hide pair, which is the only free
    // horizontal space the compact header has. Shows the custom name (elided) once
    // one is set — the context menu's Rename is reachable here too (long-press),
    // and a rename that changes nothing visible would look like it silently failed.
    TrackHeaderCompactName {
        visible: column.compact && width >= 16
        label: trackLabelRow.trackCompactLabel
        centerOffset: trackLabelRow.centerOffset
        iconRow: trackIconRow
    }

    // Clip display toggle for compact headers. Parked in the bottom-left
    // corner rather than in the icon row, which a video track has no room left
    // in — and only video rows (65px) are tall enough for a second corner.
    TrackHeaderClipDisplayButton {
        column: trackLabelRow.column
        trackIndex: index
        clipDisplay: trackLabelRow.clipDisplay
        corner: true
        shown: column.compact && column.tracks[index].type === "video"
    }

    // Track context menu. Right-click on desktop; on touch it is opened from the
    // reorder area below, which sits under the mute/hide toggles so those keep
    // their own presses. Accepting LeftButton here would put this on top of
    // everything and swallow the lot.
    MouseArea {
        id: trackMenuArea
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        onClicked: trackContextMenu.popup()

        TrackHeaderContextMenu {
            id: trackContextMenu
            header: trackLabelRow
            column: trackLabelRow.column
            renameDialog: trackLabelRow.renameDialog
            deleteDialog: trackLabelRow.deleteDialog
            onCoversRequested: coversMenu.popup()
        }

        TrackHeaderCoversMenu {
            id: coversMenu
            column: trackLabelRow.column
            trackIndex: index
            transformLayer: trackLabelRow.isTransformLayer
        }
    }

    // Wheel over track headers should scroll the same track list as the
    // timeline. Alt+wheel retains DAW-style per-lane height adjustment.
    // NoButton keeps clicks, reorder drag and the context menu available.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        z: 40
        onWheel: (wheel) => {
            // Ctrl/Shift wheel belongs to the timeline's zoom/pan gestures.
            if (wheel.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) {
                wheel.accepted = false
                return
            }
            const touchpad = wheel.pixelDelta.x !== 0 || wheel.pixelDelta.y !== 0
            if (touchpad && Math.abs(wheel.pixelDelta.x) > Math.abs(wheel.pixelDelta.y)) {
                wheel.accepted = false
                return
            }
            const dy = wheel.pixelDelta.y !== 0 ? wheel.pixelDelta.y : wheel.angleDelta.y
            if (dy === 0)
                return
            if (wheel.modifiers & Qt.AltModifier)
                EditorState.nudgeTrackHeightScale(index, dy > 0 ? 1 : -1)
            else
                column.scrollRequested(-dy)
        }
    }
}
