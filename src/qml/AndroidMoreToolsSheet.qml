import QtQuick
import Drift
import "components"

// Everything the four-slot clip toolbar does not carry.
//
// It is not the old 26-icon strip in a drawer: rows are labelled, grouped and conditional.
// The strip's tooltip strings — unreachable text on a touch screen, since there is no hover
// — become the visible detail line under each label. That is the whole point of the sheet.
//
// Rows appear and disappear rather than greying out, so with nothing selected this is the
// timeline group alone, one screen.
AndroidBottomSheet {
    id: root

    property var panel: null

    title: qsTr("More tools")

    readonly property bool hasSelection: {
        void EditorState.selectionRevision
        return EditorState.selectedTrack >= 0 && EditorState.selectedClip >= 0
    }

    // The clip rows below are what the long-press menu used to hold; long-press now only lifts
    // a clip, so this is where they live. Their conditions are the menu's, read from the
    // selected clip instead of from the delegate that was pressed.
    readonly property var clipInfo: hasSelection ? EditorState.selectedClipData : ({})
    readonly property string trackType: {
        if (!hasSelection || !panel || !panel.tracks)
            return ""
        const track = panel.tracks[EditorState.selectedTrack]
        return track ? track.type : ""
    }
    readonly property bool clipHasEffects: (clipInfo.effects || []).length > 0
                                           || (clipInfo.audioEffects || []).length > 0
    readonly property string mediaAssetId: (AssetLibrary.badgeRevision,
                                            clipInfo.kind === "video"
                                            ? AssetLibrary.assetIdForPath(clipInfo.path || "") : "")
    // Clipboard state has no change signal; read it whenever the sheet opens.
    property bool canPasteEffects: false
    property bool canPasteAttributes: false
    property bool canMergeTrackSubtitles: false
    Connections {
        target: root
        function onAboutToShow() {
            root.canPasteEffects = EditorState.clipboardHasEffects()
            root.canPasteAttributes = EditorState.canPasteAttributes()
            root.canMergeTrackSubtitles = root.trackType === "subtitle"
                    && EditorState.canMergeAllSubtitlesOnTrack(EditorState.selectedTrack)
        }
    }

    // Trim, timing and the rest are ordered by likely reach, not by source-file order:
    // trim start, trim end and speed are the first three rows so they sit above the fold
    // at the resting detent.
    readonly property var groups: [
        {
            title: qsTr("Clip"),
            rows: [
                { id: "selectMultiple", label: qsTr("Select multiple"),
                  detail: qsTr("Tap clips to add them to the selection"),
                  icon: Theme.icons.check },
                { id: "cut", label: qsTr("Cut"),
                  detail: qsTr("Remove the clip and keep it to paste"),
                  icon: Theme.icons.scissors },
                { id: "copy", label: qsTr("Copy"),
                  detail: qsTr("Keep a copy to paste"),
                  icon: Theme.icons.copy },
                { id: "pasteAttributes", label: qsTr("Paste attributes…"),
                  detail: qsTr("Apply what you copied from another clip"),
                  icon: Theme.icons.clipboardPaste },
                { id: "rename", label: qsTr("Rename…"),
                  detail: qsTr("Change the clip's name"),
                  icon: Theme.icons.pencil },
                { id: "openComposite", label: qsTr("Open composite"),
                  detail: qsTr("Edit the clips inside"),
                  icon: Theme.icons.layers },
                { id: "flattenComposite", label: qsTr("Flatten composite"),
                  detail: qsTr("Render it into a single video clip"),
                  icon: Theme.icons.film },
                { id: "makeComposite", label: qsTr("Make composite"),
                  detail: qsTr("Group the selected clips into one"),
                  icon: Theme.icons.layers },
                { id: "unlink", label: qsTr("Unlink"),
                  detail: qsTr("Edit video and its audio separately"),
                  icon: Theme.icons.unlink },
                { id: "separateAllAudio", label: qsTr("Separate all audio tracks"),
                  detail: qsTr("One audio clip per audio track in the file"),
                  icon: Theme.icons.audioLines },
                { id: "editFriendly", label: qsTr("Convert to edit-friendly format"),
                  detail: qsTr("Smoother editing for phone and screen recordings"),
                  icon: Theme.icons.rabbit },
                { id: "mergeSubtitles", label: qsTr("Merge subtitle clips"),
                  detail: qsTr("Join the selected subtitle clips"),
                  icon: Theme.icons.linkTwo },
                { id: "mergeAllSubtitles", label: qsTr("Merge all subtitles on this track"),
                  detail: qsTr("Join every subtitle clip on the track"),
                  icon: Theme.icons.linkTwo },
                { id: "subtitleToText", label: qsTr("Convert to text clips"),
                  detail: qsTr("One text clip per cue"),
                  icon: Theme.icons.type },
                { id: "textToSubtitle", label: qsTr("Convert to subtitle"),
                  detail: qsTr("Turn text clips into subtitle cues"),
                  icon: Theme.icons.captions },
                { id: "unlinkAdjustment", label: qsTr("Unlink from clip"),
                  detail: qsTr("Stop following the clip it is attached to"),
                  icon: Theme.icons.unlink },
                { id: "adjustmentOwnTrack", label: qsTr("Move to its own track"),
                  detail: qsTr("Take the adjustment out of this lane"),
                  icon: Theme.icons.layers }
            ]
        },
        {
            title: qsTr("Effects"),
            rows: [
                { id: "copyEffects", label: qsTr("Copy effects"),
                  detail: qsTr("Keep this clip's effects to paste"),
                  icon: Theme.icons.wand },
                { id: "pasteEffects", label: qsTr("Paste effects"),
                  detail: qsTr("Add the copied effects to this clip"),
                  icon: Theme.icons.clipboardPaste },
                { id: "saveEffectsPreset", label: qsTr("Save effects as preset…"),
                  detail: qsTr("Reuse this look on other clips"),
                  icon: Theme.icons.save }
            ]
        },
        {
            title: qsTr("Trim & timing"),
            rows: [
                { id: "splitAll", label: qsTr("Split all tracks"),
                  detail: qsTr("Cut every clip under the playhead"),
                  icon: Theme.icons.scissors },
                { id: "trimStart", label: qsTr("Trim start"),
                  detail: qsTr("Drop everything before the playhead"),
                  icon: Theme.icons.trimStart },
                { id: "trimEnd", label: qsTr("Trim end"),
                  detail: qsTr("Drop everything after the playhead"),
                  icon: Theme.icons.trimEnd },
                { id: "speed", label: qsTr("Speed"),
                  detail: qsTr("Change how fast this clip plays"),
                  icon: Theme.icons.gauge },
                { id: "freeze", label: qsTr("Freeze frame"),
                  detail: qsTr("Freeze frame at current time"),
                  icon: Theme.icons.snowflake },
                { id: "merge", label: qsTr("Merge"),
                  detail: qsTr("Merge adjacent clips"),
                  icon: Theme.icons.linkTwo },
                { id: "closeGap", label: qsTr("Close gap"),
                  detail: qsTr("Close gap after clip"),
                  icon: Theme.icons.chevronsRightLeft }
            ]
        },
        {
            title: qsTr("Audio"),
            rows: [
                { id: "separateAudio", label: qsTr("Separate audio"),
                  detail: qsTr("Separate audio from video"),
                  icon: Theme.icons.audioLines }
            ]
        },
        {
            title: qsTr("Timeline"),
            rows: [
                { id: "snap", label: qsTr("Snapping"), toggle: true,
                  detail: qsTr("Line clip edges up with cuts and markers"),
                  icon: Theme.icons.magnet },
                { id: "ripple", label: qsTr("Ripple"), toggle: true,
                  detail: qsTr("Close gaps when trimming"),
                  icon: Theme.icons.foldHorizontal },
                { id: "overlap", label: qsTr("Overlap"), toggle: true,
                  detail: qsTr("Allow clip overlap"),
                  icon: Theme.icons.option },
                { id: "beat", label: qsTr("Beat markers"), toggle: true,
                  detail: qsTr("Find the beat and show markers"),
                  icon: Theme.icons.music }
            ]
        },
        {
            title: qsTr("Markers & view"),
            rows: [
                // Kept, against the plan's "paste moves to the long-press menu": the
                // timeline context menu has paste-attributes and paste-effects but no
                // paste-clip, so dropping this row would make the phone's clipboard
                // write-only again — a cut clip could never come back.
                { id: "paste", label: qsTr("Paste"),
                  detail: qsTr("Paste at current time"),
                  icon: Theme.icons.clipboardPaste },
                { id: "bookmark", label: qsTr("Bookmark"),
                  detail: qsTr("Add or remove a bookmark here"),
                  icon: Theme.icons.bookmark },
                { id: "workIn", label: qsTr("Work area in"),
                  detail: qsTr("Mark work area in at current time"),
                  icon: Theme.icons.setStart },
                { id: "workOut", label: qsTr("Work area out"),
                  detail: qsTr("Mark work area out at current time"),
                  icon: Theme.icons.setEnd },
                { id: "workClear", label: qsTr("Clear work area"),
                  detail: qsTr("Clear work area"),
                  icon: Theme.icons.x },
                { id: "shorterLayers", label: qsTr("Shorter layers"),
                  detail: qsTr("Shorter layers"),
                  icon: Theme.icons.listChevronsDownUp },
                { id: "tallerLayers", label: qsTr("Taller layers"),
                  detail: qsTr("Taller layers"),
                  icon: Theme.icons.listChevronsUpDown }
            ]
        }
    ]

    // Bindings, not baked booleans: each branch reads the same notifiable property the
    // button it replaces read, so a row appears the moment its action becomes possible.
    function rowVisible(id) {
        switch (id) {
        case "selectMultiple":
            return root.hasSelection && !!root.panel && root.panel.multiSelectActive === false
        case "cut":
        case "copy":
        case "rename":
        case "pasteAttributes":
            return root.hasSelection
        case "openComposite":
        case "flattenComposite":
            return root.hasSelection && root.clipInfo.kind === "composite"
        case "makeComposite":
            return EditorState.makeCompositeAvailable
        case "unlink":
            return root.hasSelection && !!root.clipInfo.linked && EditorState.unlinkAvailable
        case "separateAllAudio":
            return root.trackType === "video" && EditorState.separateAudioAvailable
                   && EditorState.clipAudioStreamCount(EditorState.selectedTrack,
                                                       EditorState.selectedClip) > 1
        case "editFriendly":
            return root.mediaAssetId.length > 0 && !AssetLibrary.isEditFriendly(root.mediaAssetId)
        case "mergeSubtitles":
        case "mergeAllSubtitles":
        case "subtitleToText":
            return root.trackType === "subtitle"
        case "textToSubtitle":
            return root.trackType === "text" && EditorState.textToSubtitleAvailable
        case "unlinkAdjustment":
            return root.clipInfo.kind === "adjustment" && !!root.clipInfo.linkedClipId
        case "adjustmentOwnTrack":
            return root.clipInfo.kind === "adjustment" && !!root.panel
                   && root.panel.tracks[EditorState.selectedTrack].isAdjustmentLane === true
        case "copyEffects":
        case "saveEffectsPreset":
            return root.clipHasEffects
        case "pasteEffects":
            return root.hasSelection
        case "trimStart":
        case "trimEnd":
        case "speed":
        case "closeGap":
            return root.hasSelection
        case "merge":
            return EditorState.mergeAvailable
        case "separateAudio":
            return EditorState.separateAudioAvailable
        case "workClear":
            return EditorState.workAreaInSeconds >= 0 || EditorState.workAreaOutSeconds >= 0
        case "shorterLayers":
            return EditorState.canShrinkTrackHeights
        case "tallerLayers":
            return EditorState.canGrowTrackHeights
        default:
            return true
        }
    }

    function rowEnabled(id) {
        switch (id) {
        case "beat":
            return !EditorState.beatAnalysisRunning && EditorState.durationSeconds > 0
        case "pasteAttributes":
            return root.canPasteAttributes
        case "pasteEffects":
            return root.canPasteEffects
        case "flattenComposite":
            return !EditorState.exportInProgress
        case "mergeSubtitles":
            return EditorState.mergeAvailable
        case "mergeAllSubtitles":
            return root.canMergeTrackSubtitles
        default:
            return true
        }
    }

    function rowChecked(id) {
        switch (id) {
        case "snap": return EditorState.snapEnabled
        case "ripple": return EditorState.rippleEnabled
        case "overlap": return EditorState.allowClipOverlap
        case "beat": return EditorState.beatGridVisible
        default: return false
        }
    }

    function rowDetail(row) {
        if (row.id === "beat" && EditorState.beatAnalysisRunning)
            return qsTr("Analyzing…")
        return row.detail
    }

    // Every body here is the one the button it replaces already had.
    function activate(id) {
        const track = EditorState.selectedTrack
        const clipIndex = EditorState.selectedClip
        switch (id) {
        case "selectMultiple":
            root.panel.multiSelectActive = true
            break
        case "cut":
            EditorState.cutSelection()
            break
        case "copy":
            EditorState.copySelection()
            break
        case "pasteAttributes":
            EditorState.requestPasteAttributes()
            break
        case "rename":
            root.panel.requestRenameClip(track, clipIndex)
            break
        case "openComposite":
            EditorState.openCompositeClip(track, clipIndex)
            break
        case "flattenComposite":
            EditorState.flattenComposite(track, clipIndex)
            break
        case "makeComposite":
            EditorState.makeCompositeFromSelection()
            break
        case "unlink":
            EditorState.unlinkSelectedClips()
            break
        case "separateAllAudio":
            EditorState.separateAllAudioTracks(track, clipIndex)
            break
        case "editFriendly":
            EditorState.convertAssetsToConstantFrameRate([root.mediaAssetId])
            break
        case "mergeSubtitles":
            EditorState.mergeSelectedClips()
            break
        case "mergeAllSubtitles":
            EditorState.mergeAllSubtitlesOnTrack(track)
            break
        case "subtitleToText":
            EditorState.convertSubtitleToTextClips(track, clipIndex)
            break
        case "textToSubtitle":
            root.panel.requestConvertTextToSubtitle()
            break
        case "unlinkAdjustment":
            EditorState.unlinkAdjustment(track, clipIndex)
            break
        case "adjustmentOwnTrack":
            EditorState.moveAdjustmentToOwnTrack(track, clipIndex)
            break
        case "copyEffects":
            EditorState.copyClipEffectsToClipboard(track, clipIndex)
            break
        case "pasteEffects":
            EditorState.pasteEffectsFromClipboard(track, clipIndex)
            break
        case "saveEffectsPreset":
            root.panel.requestSaveEffectPreset(track, clipIndex)
            break
        case "splitAll":
            EditorState.splitAtPlayhead()
            break
        case "trimStart":
            EditorState.splitSelectedClipLeft()
            break
        case "trimEnd":
            EditorState.splitSelectedClipRight()
            break
        case "speed":
            root.hostWindow.openSpeedCurve(EditorState.selectedTrack, EditorState.selectedClip)
            break
        case "freeze":
            EditorState.freezeFrameAtPlayhead()
            break
        case "merge":
            EditorState.mergeSelectedClips()
            break
        case "closeGap": {
            const clip = EditorState.clipAt(EditorState.selectedTrack, EditorState.selectedClip)
            if (clip && clip.start !== undefined)
                EditorState.closeGap(EditorState.selectedTrack, clip.start + clip.duration)
            break
        }
        case "separateAudio":
            EditorState.separateAudioFromSelection()
            break
        case "snap":
            EditorState.snapEnabled = !EditorState.snapEnabled
            break
        case "ripple":
            EditorState.rippleEnabled = !EditorState.rippleEnabled
            break
        case "overlap":
            EditorState.allowClipOverlap = !EditorState.allowClipOverlap
            break
        case "beat":
            // Analysis covers the whole timeline rather than a clip range: the markers and
            // the snap targets they feed are timeline-wide here.
            if (EditorState.beatGridVisible) {
                EditorState.beatGridVisible = false
            } else {
                EditorState.beatGridVisible = true
                EditorState.analyzeBeats(0, EditorState.durationSeconds)
            }
            break
        case "paste":
            EditorState.pasteAtPlayhead()
            break
        case "bookmark":
            EditorState.toggleBookmarkAtPlayhead()
            break
        case "workIn":
            EditorState.markWorkAreaIn()
            break
        case "workOut":
            EditorState.markWorkAreaOut()
            break
        case "workClear":
            EditorState.clearWorkArea()
            break
        case "shorterLayers":
            EditorState.nudgeAllTrackHeightScales(-1)
            break
        case "tallerLayers":
            EditorState.nudgeAllTrackHeightScales(1)
            break
        }
    }

    Flickable {
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: groupColumn.implicitHeight
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: groupColumn
            width: parent.width
            bottomPadding: Theme.spacing3xl
            spacing: Theme.spacingLg

            Repeater {
                model: root.groups

                delegate: Column {
                    id: group
                    required property var modelData
                    width: groupColumn.width

                    // A title with no rows under it is a heading for nothing.
                    readonly property bool anyVisible: {
                        for (let i = 0; i < group.modelData.rows.length; ++i) {
                            if (root.rowVisible(group.modelData.rows[i].id))
                                return true
                        }
                        return false
                    }

                    visible: group.anyVisible

                    // Just the heading's own height: the tiles carry their own top padding, and the
                    // old touch-target-tall band left a gap under the sheet title.
                    Item {
                        width: parent.width
                        height: groupHeading.implicitHeight

                        ThemedLabel {
                            id: groupHeading
                            anchors.left: parent.left
                            anchors.leftMargin: Theme.pagePadding + root.safeLeft
                            anchors.bottom: parent.bottom
                            tone: "default"
                            size: "sm"
                            text: group.modelData.title
                        }
                    }

                    // Four to a row: the sheet had grown past two screens as a list. Grid skips
                    // hidden tiles, so a group packs whatever applies to the selection.
                    Grid {
                        id: tileGrid
                        x: Theme.spacingMd + root.safeLeft
                        width: group.width - Theme.spacingMd * 2 - root.safeLeft - root.safeRight
                        columns: 4
                        readonly property real tileWidth: Math.floor(width / columns)

                        Repeater {
                            model: group.modelData.rows

                            delegate: SheetActionTile {
                                required property var modelData
                                width: tileGrid.tileWidth
                                visible: root.rowVisible(modelData.id)
                                enabled: root.rowEnabled(modelData.id)
                                label: modelData.label
                                detail: root.rowDetail(modelData)
                                glyph: modelData.icon
                                toggle: modelData.toggle === true
                                checked: root.rowChecked(modelData.id)

                                onClicked: {
                                    Haptics.select()
                                    // A toggle shows its effect on the timeline live, so it stays
                                    // put; an action is done and the sheet gets out of the way.
                                    if (!toggle)
                                        root.dismiss()
                                    root.activate(modelData.id)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
