import QtQuick
import Drift

// Timeline toolbar: transport, user-arranged edit actions with a More menu, timeline
// switcher (main / composites), snap/ripple toggles, zoom controls and the overview toggle. Zoom and the
// time readout are read from and written back to the owning TimelinePanel via `panel`.
// New tracks are added from the plus button above the track headers.
Item {
    id: toolbar

    // Owning TimelinePanel; provides zoom (read/write via setZoom), zoom bounds and formatTime.
    property var panel

    height: Theme.timelineToolbarHeight

    // Appends an action's current binding to its tooltip. Every action here has one,
    // but only the header's Save button used to show it, so the keyboard route to
    // anything on this toolbar was undiscoverable. Rebound keys follow automatically
    // because shortcutFor reads the live map.
    function withShortcut(label, actionId) {
        const key = EditorState.shortcutFor(actionId)
        return key.length > 0 ? qsTr("%1 (%2)").arg(label).arg(key) : label
    }

    ThemeSlot {
        slot: "TimelineToolbarBackground"
        control: toolbar
        anchors.fill: parent
        z: -1
        fallback: Component {
            Rectangle {
                anchors.fill: parent
                color: Theme.c.timelineToolbar.background
            }
        }
    }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.c.timelineToolbar.border
    }

    TimelineToolbarActions {
        id: actions
    }

    readonly property var actionMeta: actions.actionMeta
    readonly property var layout: actions.layout

    function actionTooltip(id) {
        const meta = actionMeta[id]
        const label = meta.tip || meta.label
        return meta.shortcut ? withShortcut(label, meta.shortcut) : label
    }

    function triggerAction(id) {
        switch (id) {
        case "select": panel.timelineTool = ""; break
        case "cut": panel.timelineTool = panel.timelineTool === "split" ? "" : "split"; break
        case "trimStart": panel.timelineTool = panel.timelineTool === "trimStart" ? "" : "trimStart"; break
        case "trimEnd": panel.timelineTool = panel.timelineTool === "trimEnd" ? "" : "trimEnd"; break
        case "deleteLeft": EditorState.splitSelectedClipLeft(); break
        case "deleteRight": EditorState.splitSelectedClipRight(); break
        case "separateAudio": EditorState.separateAudioFromSelection(); break
        case "unlink": EditorState.unlinkSelectedClips(); break
        case "link": EditorState.linkSelectedClips(); break
        case "undo": EditorState.undo(); break
        case "redo": EditorState.redo(); break
        case "delete": EditorState.deleteSelectedClip(); break
        case "copy": EditorState.copySelection(); break
        case "paste": EditorState.pasteAtPlayhead(); break
        case "duplicate": EditorState.duplicateSelectedClip(); break
        case "bookmark": EditorState.toggleBookmarkAtPlayhead(); break
        case "markIn": EditorState.markWorkAreaIn(); break
        case "markOut": EditorState.markWorkAreaOut(); break
        case "loop": EditorState.toggleLoopWorkArea(); break
        case "clearWorkArea": EditorState.clearWorkArea(); break
        case "merge": EditorState.mergeSelectedClips(); break
        case "freeze": EditorState.freezeFrameAtPlayhead(); break
        case "adjustmentLayer": EditorState.addAdjustmentClip(-1, -1); break
        case "transformLayer": EditorState.addTransformLayerForSelection(); break
        case "camera": EditorState.addCameraTrack(); break
        }
    }

    function actionEnabled(id) {
        switch (id) {
        case "separateAudio": return EditorState.separateAudioAvailable
        case "unlink": return EditorState.unlinkAvailable
        case "link": return EditorState.linkAvailable
        case "undo": return EditorState.undoAvailable
        case "redo": return EditorState.redoAvailable
        case "loop": return EditorState.workAreaActive
        case "clearWorkArea": return EditorState.workAreaInSeconds >= 0
                                     || EditorState.workAreaOutSeconds >= 0
        case "merge": return EditorState.mergeAvailable
        }
        return true
    }

    function actionActive(id) {
        switch (id) {
        case "select": return panel.timelineTool === ""
        case "cut": return panel.timelineTool === "split"
        case "trimStart": return panel.timelineTool === "trimStart"
        case "trimEnd": return panel.timelineTool === "trimEnd"
        case "markIn": return EditorState.workAreaInSeconds >= 0
        case "markOut": return EditorState.workAreaOutSeconds >= 0
        case "loop": return EditorState.loopWorkAreaEnabled
        }
        return false
    }

    // On the toolbar the A/V pair only appears when it applies to the selection, so a
    // selection shows one or the other rather than two mostly-disabled buttons.
    function actionShownOnToolbar(id) {
        if (id === "separator")
            return true
        if (id === "separateAudio")
            return EditorState.separateAudioAvailable
        if (id === "unlink")
            return EditorState.unlinkAvailable
        if (id === "link")
            return EditorState.linkAvailable
        return true
    }

    // A separator only divides something: not at either end, and not next to another one
    // (which happens when the buttons between them are all contextually hidden).
    function separatorNeeded(list, i, shown) {
        var before = false
        for (var b = i - 1; b >= 0 && list[b] !== "separator"; b--) {
            if (shown(list[b])) { before = true; break }
        }
        if (!before)
            return false
        for (var a = i + 1; a < list.length; a++) {
            if (list[a] === "separator")
                return false
            if (shown(list[a]))
                return true
        }
        return false
    }


    TimelineToolbarHairline { }

    Row {
        id: leftControls
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacingLg
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.spacingXs
        // Never runs under the right-hand controls; buttons past the
        // available width are clipped rather than overlapping. Worked out from
        // widths, not x: under RTL mirroring the two groups swap edges.
        width: Math.max(0, toolbar.width - rightControls.width - Theme.spacingLg * 3)
        clip: true

        ThemedButton {
            variant: "primary"
            glyph: Theme.icons.plus
            text: qsTr("Import media")
            tooltip: qsTr("Add video or audio files directly to the timeline at the playhead")
            height: Math.min(Theme.controlHeightSm, toolbar.height - Theme.spacingXs * 2)
            topPadding: Theme.spacingXs
            bottomPadding: Theme.spacingXs
            enabled: !MediaImport.importing
            onClicked: toolbar.panel.importMediaDirectlyToTimeline()
        }

        TimelineToolbarSeparator { }

        TimelineToolbarToolButton {
            glyph: EditorState.playing ? Theme.icons.pause : Theme.icons.play
            tooltip: EditorState.playing ? qsTr("Pause") : qsTr("Play")
            onClicked: EditorState.togglePlayback()
        }

        TimelineToolbarTimecode {
            panel: toolbar.panel
        }

        TimelineToolbarSeparator { }

        Repeater {
            model: toolbar.layout.toolbar
            delegate: TimelineToolbarActionSlot {
                host: toolbar
            }
        }

        TimelineToolbarOverflowButton {
            id: overflowButton
            host: toolbar
            onCustomizeRequested: customizeDialog.open()
        }
    }

    TimelineToolbarCustomizeDialog {
        id: customizeDialog
    }

    TimelineSequenceTabs {
        leftGroup: leftControls
        rightGroup: rightControls
    }

    Row {
        id: rightControls
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingLg
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.spacingSm

        TimelineToolbarToolButton {
            glyph: Theme.icons.option
            tooltip: qsTr("Allow clip overlap")
            active: EditorState.allowClipOverlap
            onClicked: EditorState.allowClipOverlap = !EditorState.allowClipOverlap
        }
        TimelineToolbarToolButton {
            glyph: Theme.icons.locateFixed
            tooltip: qsTr("Auto-select: select the clip under the playhead")
            active: EditorState.preferences.autoSelectUnderPlayhead
            onClicked: EditorState.preferences.autoSelectUnderPlayhead = !EditorState.preferences.autoSelectUnderPlayhead
        }

        TimelineToolbarSeparator { }

        // Keep the magnetic tools next to zoom, where CapCut users expect them.
        TimelineToolbarToolButton {
            glyph: Theme.icons.magnet
            tooltip: qsTr("Magnetic snapping: align video and audio edges across tracks")
            active: EditorState.snapEnabled
            onClicked: EditorState.snapEnabled = !EditorState.snapEnabled
        }
        TimelineToolbarToolButton {
            glyph: Theme.icons.foldHorizontal
            tooltip: qsTr("Ripple editing: keep following clips together when trimming")
            active: EditorState.rippleEnabled
            onClicked: EditorState.rippleEnabled = !EditorState.rippleEnabled
        }

        TimelineToolbarZoomControl {
            host: toolbar
        }

        TimelineToolbarSeparator { }

        TimelineToolbarToolButton {
            glyph: Theme.icons.slidersVertical
            tooltip: qsTr("Toggle audio mixer strip")
            active: EditorState.preferences.audioMixerVisible
            onClicked: EditorState.preferences.audioMixerVisible = !EditorState.preferences.audioMixerVisible
        }

        TimelineToolbarToolButton {
            glyph: Theme.icons.panelTop
            tooltip: qsTr("Timeline overview — a minimap of the whole project; click or drag it to jump the view")
            active: EditorState.preferences.timelineOverviewVisible
            onClicked: EditorState.preferences.timelineOverviewVisible = !EditorState.preferences.timelineOverviewVisible
        }
    }
}
