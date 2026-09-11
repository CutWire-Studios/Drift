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
        void EditorState.selection
        return EditorState.selectedTrack >= 0 && EditorState.selectedClip >= 0
    }

    // Trim, timing and the rest are ordered by likely reach, not by source-file order:
    // trim start, trim end and speed are the first three rows so they sit above the fold
    // at the resting detent.
    readonly property var groups: [
        {
            title: qsTr("Trim & timing"),
            rows: [
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
        if (id === "beat")
            return !EditorState.beatAnalysisRunning && EditorState.durationSeconds > 0
        return true
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
        switch (id) {
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
            topPadding: Theme.spacingLg
            bottomPadding: Theme.spacing3xl

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

                    Item {
                        width: parent.width
                        height: Theme.androidMinTouchTarget - Theme.spacingLg

                        ThemedLabel {
                            anchors.left: parent.left
                            anchors.leftMargin: Theme.pagePadding + root.safeLeft
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: Theme.spacingSm
                            tone: "default"
                            size: "sm"
                            text: group.modelData.title
                        }
                    }

                    Repeater {
                        model: group.modelData.rows

                        delegate: SheetActionRow {
                            required property var modelData
                            width: group.width
                            visible: root.rowVisible(modelData.id)
                            enabled: root.rowEnabled(modelData.id)
                            label: modelData.label
                            detail: root.rowDetail(modelData)
                            glyph: modelData.icon
                            toggle: modelData.toggle === true
                            checked: root.rowChecked(modelData.id)
                            sideInset: root.safeLeft
                            sideInsetRight: root.safeRight

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
