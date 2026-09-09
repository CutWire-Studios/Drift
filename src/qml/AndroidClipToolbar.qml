import QtQuick
import QtQuick.Window
import Drift
import "components"

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

    // Quick edit's reduced set. The three that survive are the ones a one-clip trim is
    // actually made of; Duplicate is a multi-clip move, and More is kept because it is
    // where Trim start/end live and they are the point of the whole mode.
    property bool compact: false

    signal moreRequested()

    readonly property bool hasSelection: {
        void EditorState.selection
        return EditorState.selectedTrack >= 0 && EditorState.selectedClip >= 0
    }
    readonly property bool hasTransition: EditorState.selectedTransitionTrack >= 0

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
        if (root.hasSelection) {
            if (root.compact) {
                return [
                    { id: "split", label: qsTr("Split"), icon: Theme.icons.scissors },
                    { id: "delete", label: qsTr("Delete"), icon: Theme.icons.trash,
                      destructive: true, gutter: true },
                    { id: "more", label: qsTr("More"), icon: Theme.icons.ellipsis }
                ]
            }
            return [
                { id: "split", label: qsTr("Split"), icon: Theme.icons.scissors },
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
        case "split":
            EditorState.splitAtPlayhead()
            break
        case "duplicate":
            EditorState.duplicateSelectedClip()
            break
        case "delete":
            EditorState.deleteSelectedClip()
            break
        // Duration is a field, not a button: the transition inspector already owns it, and
        // the properties sheet is what shows the inspector for the current selection.
        case "transitionDuration":
            if (Window.window.editorPage)
                Window.window.editorPage.openPropertiesSheet()
            break
        case "transitionCurve":
            Window.window.openTransitionCurve(EditorState.selectedTransitionTrack,
                                              EditorState.selectedTransitionData.id)
            break
        case "transitionReplace":
            if (Window.window.editorPage)
                Window.window.editorPage.openAssetsTab("transitions")
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
        color: Theme.panelBackground
    }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: Theme.borderWidth
        color: Theme.panelBorder
    }

    Item {
        id: barBody
        anchors.fill: parent
        anchors.leftMargin: root.leftInset
        anchors.rightMargin: root.rightInset

        Text {
            anchors.left: parent.left
            anchors.leftMargin: Theme.androidPagePadding
            anchors.right: toolRow.left
            anchors.rightMargin: Theme.spacingLg
            anchors.verticalCenter: parent.verticalCenter
            visible: root.showHint
            text: qsTr("Tap a clip to edit")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
            elide: Text.ElideRight
        }

        Row {
            id: toolRow
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom

            // Four slots divide the bar; the two-slot hint state keeps them at a fixed
            // width so Fit and More sit where they always sit rather than sprawling.
            readonly property real slotWidth: root.entries.length >= 4
                                              ? Math.floor(barBody.width / root.entries.length)
                                              : Theme.androidMinTouchTarget + Theme.spacing2xl

            Repeater {
                model: root.entries

                delegate: Item {
                    id: slot
                    required property var modelData
                    width: toolRow.slotWidth
                    height: toolRow.height

                    // Delete sits next to the two edits it cannot be undone from as easily;
                    // the rule separating it is cheaper than the width a real gutter costs.
                    Rectangle {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        visible: slot.modelData.gutter === true
                        width: Theme.borderWidth
                        height: Theme.spacing3xl
                        color: Theme.panelBorder
                    }

                    NavRailButton {
                        anchors.fill: parent
                        entry: slot.modelData
                        tint: slot.modelData.destructive === true ? Theme.destructive
                                                                  : Theme.mutedForeground
                        onClicked: {
                            // Explicit rather than inherited: a signal handler declared on the
                            // instance replaces the one in NavRailButton's own definition.
                            if (slot.modelData.destructive === true)
                                Haptics.confirm()
                            else
                                Haptics.select()
                            root.run(slot.modelData.id)
                        }
                    }
                }
            }
        }
    }
}
