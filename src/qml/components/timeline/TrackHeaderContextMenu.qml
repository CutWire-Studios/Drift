import QtQuick
import Drift

// Right-click menu for one track header.
ThemedContextMenu {
    id: trackContextMenu

    property var header
    property var column
    property var renameDialog
    property var deleteDialog
    signal coversRequested()

    property var showMenu: null

    function isTopLevel(i) {
        return column.tracks[i].isAdjustmentLane !== true
    }
    // Project index just past this track and the lanes nested under it.
    function belowIndex() {
        return header.index + 1 + header.laneCount
    }
    function previousTopLevel() {
        for (let i = header.index - 1; i >= 0; i--) {
            if (isTopLevel(i))
                return i
        }
        return -1
    }
    // Where this track must land to pass the next one and its lanes; -1 when it is last.
    function nextTopLevelEnd() {
        let next = -1
        for (let i = belowIndex(); i < column.tracks.length; i++) {
            if (isTopLevel(i)) {
                next = i
                break
            }
        }
        if (next < 0)
            return -1
        let end = next + 1
        while (end < column.tracks.length && !isTopLevel(end))
            end++
        return end - 1
    }
    function newTrackType() {
        const type = column.tracks[header.index].type
        return ["video", "audio", "text", "subtitle", "shape"].indexOf(type) >= 0 ? type : "video"
    }
    function selectTrackClips() {
        const clips = column.tracks[header.index].clips || []
        const pairs = []
        for (let c = 0; c < clips.length; c++)
            pairs.push({ "track": header.index, "clip": c })
        EditorState.setSelection(pairs)
    }

    onAboutToShow: rebuildShowMenu()

    function rebuildShowMenu() {
        if (showMenu) {
            removeMenu(showMenu)
            showMenu = null
        }
        if (column.tracks[header.index].type !== "video")
            return
        const sub = showInClipsComponent.createObject(null)
        for (let i = 0; i < count; i++) {
            if (itemAt(i) === showInClipsAnchor) {
                insertMenu(i + 1, sub)
                break
            }
        }
        showMenu = sub
    }

    Component {
        id: showInClipsComponent
        ThemedContextMenu {
            title: qsTr("Show in clips")

            ThemedMenuItem {
                text: trackContextMenu.column.clipDisplayLabel(0)
                icon.name: trackContextMenu.header.clipDisplay === 0
                           ? Theme.icons.check : trackContextMenu.column.clipDisplayGlyph(0)
                onTriggered: EditorState.setTrackClipDisplay(trackContextMenu.header.index, 0)
            }
            ThemedMenuItem {
                text: trackContextMenu.column.clipDisplayLabel(1)
                icon.name: trackContextMenu.header.clipDisplay === 1
                           ? Theme.icons.check : trackContextMenu.column.clipDisplayGlyph(1)
                onTriggered: EditorState.setTrackClipDisplay(trackContextMenu.header.index, 1)
            }
            ThemedMenuItem {
                text: trackContextMenu.column.clipDisplayLabel(2)
                icon.name: trackContextMenu.header.clipDisplay === 2
                           ? Theme.icons.check : trackContextMenu.column.clipDisplayGlyph(2)
                onTriggered: EditorState.setTrackClipDisplay(trackContextMenu.header.index, 2)
            }
        }
    }

    ThemedMenuItem {
        text: qsTr("Rename…")
        icon.name: Theme.icons.pencil
        onTriggered: {
            column.pendingRenameTrack = header.index
            // Custom name only — pre-filling the type+position fallback
            // ("Video 1") would persist it as a real name on an unchanged
            // confirm, then stick after the track is reordered.
            renameDialog.nameField.text = column.tracks[header.index].name || ""
            renameDialog.open()
        }
    }
    ThemedMenuItem {
        visible: header.laneCount > 0
        height: visible ? implicitHeight : 0
        text: header.lanesCollapsed ? qsTr("Expand child lanes") : qsTr("Collapse child lanes")
        icon.name: header.lanesCollapsed ? Theme.icons.chevronDown : Theme.icons.chevronUp
        onTriggered: EditorState.setTrackLanesCollapsed(header.index, !header.lanesCollapsed)
    }
    ThemedMenuSeparator {}
    ThemedMenuItem {
        text: header.trackMuted ? qsTr("Unmute track")
                                       : qsTr("Mute track")
        icon.name: header.trackMuted ? Theme.icons.volumeHigh
                                            : Theme.icons.volumeOff
        onTriggered: EditorState.setTrackMuted(header.index, !header.trackMuted)
    }
    ThemedMenuItem {
        visible: header.wantSolo
        text: header.trackSolo ? qsTr("Unsolo track") : qsTr("Solo track")
        icon.name: Theme.icons.headphones
        onTriggered: EditorState.setTrackSolo(header.index, !header.trackSolo)
    }
    ThemedMenuItem {
        text: header.trackHidden ? qsTr("Show track")
                                        : qsTr("Hide track")
        icon.name: header.trackHidden ? Theme.icons.eye
                                             : Theme.icons.eyeOff
        onTriggered: EditorState.setTrackHidden(header.index, !header.trackHidden)
    }
    ThemedMenuItem {
        text: header.trackLocked ? qsTr("Unlock track") : qsTr("Lock track")
        icon.name: header.trackLocked ? Theme.icons.lockOpen : Theme.icons.lock
        onTriggered: EditorState.setTrackLocked(header.index, !header.trackLocked)
    }
    // A review track: still shown and heard while editing, never exported. Not offered for a
    // transform or camera layer, whose absence would silently reframe everything under it.
    ThemedMenuItem {
        visible: (!header.isTransformLayer && !header.isCameraLayer) || header.trackExcludedFromExport
        height: visible ? implicitHeight : 0
        text: header.trackExcludedFromExport ? qsTr("Include in export") : qsTr("Exclude from export")
        icon.name: Theme.icons.film
        onTriggered: EditorState.setTrackExcludedFromExport(header.index, !header.trackExcludedFromExport)
    }
    ThemedMenuSeparator {
        visible: header.wantMic || header.wantLanes
    }
    ThemedMenuItem {
        visible: header.wantMic
        text: header.isRecordingHere ? qsTr("Stop recording") : qsTr("Record voiceover")
        icon.name: Theme.icons.mic
        onTriggered: {
            if (EditorState.isRecordingAudio)
                EditorState.stopAudioRecording()
            else
                EditorState.startAudioRecording(header.index)
        }
    }
    WorkspaceMenuItem {
        visible: header.wantMic && !ShellBridge.phoneShell
        text: qsTr("Recording setup (teleprompter, noise)…")
        icon.name: Theme.icons.mic
        target: ({ workspace: "record", panel: "recorder" })
    }
    ThemedMenuItem {
        visible: header.wantLanes
        text: header.lanesOn ? qsTr("Show one combined waveform") : qsTr("Show each channel separately")
        icon.name: Theme.icons.split
        onTriggered: {
            const on = !header.lanesOn
            EditorState.setTrackShowChannelWaveforms(header.index, on)
            if (on)
                column.growRowForChannelLanes(header.index, header.laneChannels)
        }
    }
    ThemedMenuSeparator {
        visible: header.isTransformLayer || header.transformable
                 || header.isCameraLayer
    }
    // Back-to-back camera clips are cuts between framings; each new one starts at rest.
    ThemedMenuItem {
        visible: header.isCameraLayer
        text: qsTr("Add camera clip at playhead")
        icon.name: Theme.icons.video
        onTriggered: EditorState.addCameraClip()
    }
    // A submenu cannot be hidden per row, so the span choices open as their
    // own menu from here.
    ThemedMenuItem {
        visible: header.isTransformLayer
        text: qsTr("Covers…")
        icon.name: Theme.icons.layers
        onTriggered: trackContextMenu.coversRequested()
    }
    ThemedMenuItem {
        visible: header.isTransformLayer
        text: qsTr("Select covered clips")
        icon.name: Theme.icons.maximize
        onTriggered: column.selectCoveredClips(header.index)
    }
    ThemedMenuItem {
        visible: header.transformable
        text: qsTr("Add transform layer above")
        icon.name: Theme.icons.maximize
        onTriggered: EditorState.addTransformLayerAbove(header.index)
    }
    ThemedMenuSeparator {}
    ThemedMenuItem { id: showInClipsAnchor; visible: false }
    ThemedContextMenu {
        title: qsTr("Track height")

        ThemedMenuItem {
            text: qsTr("Short")
            icon.name: column.isHeightPreset(header.index, 0.6)
                       ? Theme.icons.check : Theme.icons.listChevronsDownUp
            onTriggered: EditorState.setTrackHeightScale(header.index, 0.6)
        }
        ThemedMenuItem {
            text: qsTr("Tall")
            icon.name: column.isHeightPreset(header.index, 1.0)
                       ? Theme.icons.check : Theme.icons.list
            onTriggered: EditorState.setTrackHeightScale(header.index, 1.0)
        }
        ThemedMenuItem {
            text: qsTr("Taller")
            icon.name: column.isHeightPreset(header.index, 1.6)
                       ? Theme.icons.check : Theme.icons.listChevronsUpDown
            onTriggered: EditorState.setTrackHeightScale(header.index, 1.6)
        }
        ThemedMenuSeparator {
            visible: !column.touchHold
        }
        ThemedMenuItem {
            visible: !column.touchHold
            enabled: false
            text: qsTr("Drag the bottom edge or scroll over the header to fine-tune")
            icon.name: Theme.icons.info
        }
    }
    // Desktop fine-tunes a lane by wheeling over its header. Touch has no
    // wheel, so the same nudge is offered explicitly.
    ThemedMenuItem {
        visible: column.touchHold
        text: qsTr("Taller row")
        icon.name: Theme.icons.chevronUp
        enabled: column.tracks[header.index].heightScale < EditorState.trackHeightScaleMax()
        onTriggered: EditorState.nudgeTrackHeightScale(header.index, 1)
    }
    ThemedMenuItem {
        visible: column.touchHold
        text: qsTr("Shorter row")
        icon.name: Theme.icons.chevronDown
        enabled: column.tracks[header.index].heightScale > EditorState.trackHeightScaleMin()
        onTriggered: EditorState.nudgeTrackHeightScale(header.index, -1)
    }
    ThemedMenuSeparator {}
    ThemedMenuItem {
        text: qsTr("Add track above")
        icon.name: Theme.icons.plus
        onTriggered: EditorState.insertTrack(trackContextMenu.newTrackType(), header.index)
    }
    ThemedMenuItem {
        text: qsTr("Add track below")
        icon.name: Theme.icons.plus
        onTriggered: EditorState.insertTrack(trackContextMenu.newTrackType(), trackContextMenu.belowIndex())
    }
    ThemedMenuItem {
        text: qsTr("Move up")
        icon.name: Theme.icons.chevronUp
        enabled: trackContextMenu.previousTopLevel() >= 0
        onTriggered: EditorState.moveTrack(header.index, trackContextMenu.previousTopLevel())
    }
    ThemedMenuItem {
        text: qsTr("Move down")
        icon.name: Theme.icons.chevronDown
        enabled: trackContextMenu.nextTopLevelEnd() >= 0
        onTriggered: EditorState.moveTrack(header.index, trackContextMenu.nextTopLevelEnd())
    }
    ThemedMenuItem {
        text: qsTr("Select clips on track")
        icon.name: Theme.icons.maximize
        enabled: (column.tracks[header.index].clips || []).length > 0
        onTriggered: trackContextMenu.selectTrackClips()
    }
    ThemedMenuSeparator {}
    ThemedMenuItem {
        text: qsTr("Delete track")
        icon.name: Theme.icons.trash
        onTriggered: {
            column.pendingDeleteTrack = header.index
            deleteDialog.open()
        }
    }
}
