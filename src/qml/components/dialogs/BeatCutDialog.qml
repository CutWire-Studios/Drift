import QtQuick
import QtQuick.Controls.Basic
import Drift

// Options for cutting the selected clips together on the music's beat. The music is the clip the
// menu was opened on; every other selected video or image clip is laid out against it.
ThemedDialog {
    id: root

    property int trackIndex: -1
    property int clipIndex: -1

    readonly property var beatChoices: [1, 2, 4, 8]

    title: qsTr("Auto-cut to beat")
    acceptText: qsTr("Cut")
    preferredWidth: Theme.dialogWidthSm

    function openFor(track, clip) {
        trackIndex = track
        clipIndex = clip
        beatsBox.currentIndex = 1
        playheadBox.checked = false
        syncBox.checked = true
        open()
    }

    onAccepted: EditorState.autoCutSelectionToBeat(trackIndex, clipIndex,
                                                    beatChoices[beatsBox.currentIndex],
                                                    playheadBox.checked, syncBox.checked)

    contentItem: Column {
        spacing: Theme.spacingLg
        width: parent ? parent.width : 320

        ThemedLabel {
            width: parent.width
            size: "sm"
            wrapMode: Text.WordWrap
            text: qsTr("%n selected clip(s) will be placed on a new track, one per cut.", "",
                       EditorState.beatCutTargetCount(root.trackIndex, root.clipIndex))
        }

        Row {
            spacing: Theme.spacingLg

            ThemedLabel {
                anchors.verticalCenter: parent.verticalCenter
                size: "sm"
                text: qsTr("Cut every")
            }

            ThemedComboBox {
                id: beatsBox
                width: 140
                model: root.beatChoices.map(function (n) { return qsTr("%n beat(s)", "", n) })
            }
        }

        ThemedCheckBox {
            id: playheadBox
            width: parent.width
            text: qsTr("Start at the playhead")
            tooltip: qsTr("Otherwise the first clip starts where the music starts")
        }

        ThemedCheckBox {
            id: syncBox
            width: parent.width
            checked: true
            text: qsTr("Cut on bar lines only")
            tooltip: qsTr("Start on the first downbeat and keep every cut on the music's strong beats")
        }
    }
}
