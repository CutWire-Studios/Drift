import QtQuick
import QtQuick.Window
import Drift
// The contextual layer above the timeline: what you can do to the thing you have selected.
//
// Replaces the 26-icon horizontally-scrolling strip, of which four were visible at a time and
// the last needed six swipes. Four slots divide the width, so everything is on screen at once
// and nothing is a mode you have to arm first — Split and Trim act at the playhead on the
// selection rather than asking you to aim a tap afterwards.
//
// The rail below is navigation and never changes on selection; this is the layer that does.
Item {
    id: root

    property var panel: null

    signal moreRequested()
    signal fadeRequested()
    signal editTextRequested()

    readonly property bool hasSelection: {
        void EditorState.selectionRevision
        return EditorState.selectedTrack >= 0 && EditorState.selectedClip >= 0
    }
    readonly property bool hasTransition: EditorState.selectedTransitionTrack >= 0
    readonly property bool multiSelection: {
        void EditorState.selectionRevision
        return EditorState.selection.length > 1
    }
    readonly property bool transformClipSelected: {
        void EditorState.selectionRevision
        const data = root.hasSelection ? EditorState.selectedClipData : ({})
        return data.kind === "adjustment" && data.adjustmentKind === "transform"
    }

    readonly property bool textClipSelected: {
        void EditorState.selectionRevision
        const data = root.hasSelection ? EditorState.selectedClipData : ({})
        return data.kind === "text"
    }

    // The strip runs the full width of an edge-to-edge window, so in landscape the system
    // nav bar sits straight on top of whichever end the cutout is at.
    readonly property real leftInset: SafeArea.margins.left
    readonly property real rightInset: SafeArea.margins.right

    readonly property var entries: {
        if (root.hasTransition) {
            return [
                { id: "transitionDuration", label: qsTr("Duration"), icon: Theme.icons.clock },
                { id: "transitionCurve", label: qsTr("Curve"), icon: Theme.icons.blend },
                { id: "transitionReplace", label: qsTr("Replace"), icon: Theme.icons.chevronsRight },
                { id: "transitionDelete", label: qsTr("Delete"), icon: Theme.icons.trash,
                  destructive: true, gutter: true }
            ]
        }
        // Long-press only lifts a clip now, so everything its menu offered is reachable from
        // here: the everyday edits as buttons, the rest one tap away under More.
        // A transform clip has no fade, speed or source: its own transform is the edit.
        if (root.hasSelection && root.transformClipSelected) {
            return [
                { id: "split", label: qsTr("Split"), icon: Theme.icons.scissors },
                { id: "transform", label: qsTr("Transform"), icon: Theme.icons.maximize },
                { id: "duplicate", label: qsTr("Duplicate"), icon: Theme.icons.copyPlus },
                { id: "delete", label: qsTr("Delete"), icon: Theme.icons.trash,
                  destructive: true, gutter: true },
                { id: "more", label: qsTr("More"), icon: Theme.icons.ellipsis }
            ]
        }
        if (root.hasSelection && root.multiSelection && EditorState.transformTogetherAvailable) {
            return [
                { id: "transformTogether", label: qsTr("Transform together"), icon: Theme.icons.group },
                { id: "duplicate", label: qsTr("Duplicate"), icon: Theme.icons.copyPlus },
                { id: "delete", label: qsTr("Delete"), icon: Theme.icons.trash,
                  destructive: true, gutter: true },
                { id: "more", label: qsTr("More"), icon: Theme.icons.ellipsis }
            ]
        }
        // Typing is the edit a text clip wants most, and Speed means nothing to it, so Edit
        // text takes that slot rather than a seventh one the width cannot spare.
        if (root.hasSelection && root.textClipSelected) {
            return [
                { id: "editText", label: qsTr("Edit text"), icon: Theme.icons.type },
                { id: "split", label: qsTr("Split"), icon: Theme.icons.scissors },
                { id: "fade", label: qsTr("Fade"), icon: Theme.icons.blend },
                { id: "duplicate", label: qsTr("Duplicate"), icon: Theme.icons.copyPlus },
                { id: "delete", label: qsTr("Delete"), icon: Theme.icons.trash,
                  destructive: true, gutter: true },
                { id: "more", label: qsTr("More"), icon: Theme.icons.ellipsis }
            ]
        }
        if (root.hasSelection) {
            return [
                { id: "split", label: qsTr("Split"), icon: Theme.icons.scissors },
                { id: "fade", label: qsTr("Fade"), icon: Theme.icons.blend },
                { id: "speed", label: qsTr("Speed"), icon: Theme.icons.gauge },
                { id: "duplicate", label: qsTr("Duplicate"), icon: Theme.icons.copyPlus },
                { id: "delete", label: qsTr("Delete"), icon: Theme.icons.trash,
                  destructive: true, gutter: true },
                { id: "more", label: qsTr("More"), icon: Theme.icons.ellipsis }
            ]
        }
        // Honest rather than a row of greyed buttons — and the same height either way, so
        // the timeline below never shifts when the selection changes.
        return [
            { id: "fit", label: qsTr("Fit"), icon: Theme.icons.zoomFit },
            { id: "more", label: qsTr("More"), icon: Theme.icons.ellipsis }
        ]
    }

    readonly property bool showHint: !root.hasSelection && !root.hasTransition

    height: Theme.androidClipToolbarHeight
    width: parent ? parent.width : 0
    clip: true

    function run(actionId) {
        switch (actionId) {
        // The selected clip, not everything under the playhead: that is what a button on the
        // selected clip's own toolbar reads as. "Split all tracks" is under More.
        case "split": {
            const clip = EditorState.clipAt(EditorState.selectedTrack, EditorState.selectedClip)
            const t = EditorState.playheadSeconds
            if (clip && t > clip.start && t < clip.start + clip.duration)
                EditorState.splitClipAt(EditorState.selectedTrack, EditorState.selectedClip, t)
            else
                Toasts.info(qsTr("Move the playhead over the clip to split it"))
            break
        }
        case "editText":
            root.editTextRequested()
            break
        case "fade":
            if (ShellBridge.main.editorPage)
                ShellBridge.main.editorPage.openTool("audio")
            break
        case "speed":
            if (ShellBridge.main.editorPage)
                ShellBridge.main.editorPage.openTool("speed")
            break
        case "duplicate":
            EditorState.duplicateSelectedClip()
            break
        case "transform":
            if (ShellBridge.main.editorPage)
                ShellBridge.main.editorPage.openTool("transform")
            break
        case "transformTogether":
            EditorState.makeTransformLayerFromSelection()
            break
        case "delete":
            EditorState.deleteSelectedClip()
            break
        // Duration is a field, not a button: the transition page's inspector owns it.
        case "transitionDuration":
            if (ShellBridge.main.editorPage)
                ShellBridge.main.editorPage.openTool("transition")
            break
        case "transitionCurve":
            ShellBridge.main.openTransitionCurve(EditorState.selectedTransitionTrack,
                                              EditorState.selectedTransitionData.id)
            break
        case "transitionReplace":
            if (ShellBridge.main.editorPage)
                ShellBridge.main.editorPage.openAssetsTab("transitions")
            break
        case "transitionDelete":
            EditorState.removeTransition(EditorState.selectedTransitionTrack,
                                         EditorState.selectedTransitionData.id)
            break
        case "fit":
            if (root.panel)
                root.panel.fitZoom()
            break
        case "more":
            root.moreRequested()
            break
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.c.androidClipToolbar.background
    }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: Theme.c.androidClipToolbar.borderWidth
        color: Theme.c.androidClipToolbar.border
    }

    Item {
        id: barBody
        anchors.fill: parent
        anchors.leftMargin: root.leftInset
        anchors.rightMargin: root.rightInset

        AndroidClipToolbarHint {
            anchors.left: parent.left
            anchors.leftMargin: Theme.androidPagePadding
            anchors.right: toolRow.left
            anchors.rightMargin: Theme.spacingLg
            anchors.verticalCenter: parent.verticalCenter
            visible: root.showHint
        }

        Row {
            id: toolRow
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            // Full row centres so wide screens don't leave it hugging one edge; the
            // two-slot hint state stays right-hand where Fit and More always sit.
            x: root.entries.length >= 4
               ? Math.round((barBody.width - width) / 2)
               : barBody.width - width

            // Four slots divide the bar; the two-slot hint state keeps them at a fixed
            // width so Fit and More sit where they always sit rather than sprawling.
            readonly property real slotWidth: root.entries.length >= 4
                                              ? Math.min(Math.floor(barBody.width / root.entries.length), 120)
                                              : Theme.androidMinTouchTarget + Theme.spacing2xl

            Repeater {
                model: root.entries

                delegate: AndroidClipToolbarSlot {
                    width: toolRow.slotWidth
                    height: toolRow.height
                    onActivated: (actionId) => root.run(actionId)
                }
            }
        }
    }
}
