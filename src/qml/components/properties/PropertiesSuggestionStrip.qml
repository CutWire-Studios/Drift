import QtQuick
import Drift
import "../suggestions/ClipSuggestions.js" as ClipSuggestions

// One "what to do next" suggestion for the selected clip, from the same rules as the clip menu.
// "Not now" is remembered per suggestion id under QSettings "tips/suggestion.<id>".
Rectangle {
    id: root

    property int revision: 0
    property var dismissedIds: ({})

    readonly property var suggestion: {
        void root.revision
        void EditorState.selectedClipData
        void EditorState.tracks
        const data = EditorState.selectedClipData
        if (EditorState.selectedTrack < 0 || EditorState.selectedClip < 0 || !data || !data.kind)
            return null
        const list = ClipSuggestions.suggest({
            editor: EditorState, addons: Addons, tracks: EditorState.tracks,
            reframe: ShellBridge.main && ShellBridge.main.timelinePanel
                     ? ShellBridge.main.timelinePanel.requestAutoReframe : null,
            dismissed: (id) => root.dismissedIds[id] === true
                               || EditorState.preferences.tipDismissed("suggestion." + id)
        }, {
            clipData: data, trackType: "", trackIndex: EditorState.selectedTrack,
            clipIndex: EditorState.selectedClip, selected: true
        }, 1)
        return list.length > 0 ? list[0] : null
    }

    visible: suggestion !== null && EditorState.preferences.showTips
    height: visible ? Math.max(row.implicitHeight, Theme.controlHeightSm) + Theme.spacingMd * 2 : 0
    radius: Theme.c.propertiesSuggestionStrip.radius
    color: Theme.c.propertiesSuggestionStrip.background
    border.width: Theme.c.propertiesSuggestionStrip.borderWidth
    border.color: Theme.c.propertiesSuggestionStrip.border

    Row {
        id: row
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: Theme.spacingLg
        anchors.rightMargin: Theme.spacingMd
        spacing: Theme.spacingMd

        IconGlyph {
            anchors.verticalCenter: parent.verticalCenter
            glyph: Theme.icons.sparkles
            iconSize: Theme.iconSizeSm
            iconColor: Theme.c.propertiesSuggestionStrip.icon
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - parent.spacing * 3 - Theme.iconSizeSm - runButton.width - dismissButton.width
            text: root.suggestion ? root.suggestion.label : ""
            elide: Text.ElideRight
            color: Theme.c.propertiesSuggestionStrip.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
        }

        ThemedButton {
            id: runButton
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Do it")
            variant: "primary"
            onClicked: root.suggestion.run()
        }

        ThemedButton {
            id: dismissButton
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Not now")
            variant: "ghost"
            onClicked: {
                const id = root.suggestion.id
                EditorState.preferences.dismissTip("suggestion." + id)
                const next = Object.assign({}, root.dismissedIds)
                next[id] = true
                root.dismissedIds = next
            }
        }
    }
}
