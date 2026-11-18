import QtQuick
import Drift

ThemedDialog {
    id: root

    property int trackIndex: -1
    property int clipIndex: -1

    readonly property var modeIds: ["face", "center"]
    readonly property var aspectValues: [9 / 16, 1, 4 / 5, 16 / 9]

    title: qsTr("Auto reframe")
    acceptText: qsTr("Reframe")
    preferredWidth: Theme.dialogWidthSm

    function openFor(track, clip) {
        trackIndex = track
        clipIndex = clip
        open()
    }

    onAccepted: EditorState.autoReframe(trackIndex, clipIndex, aspectValues[aspectBox.currentIndex],
                                        modeIds[modeBox.currentIndex])

    contentItem: Column {
        width: parent ? parent.width : root.preferredWidth
        spacing: Theme.spacingMd

        ThemedLabel {
            text: qsTr("Track")
        }
        ThemedComboBox {
            id: modeBox
            width: parent.width
            model: [qsTr("Follow face"), qsTr("Keep centred")]
        }
        ThemedLabel {
            width: parent.width
            wrapMode: Text.WordWrap
            visible: modeBox.currentIndex === 0
            text: qsTr("Follows the clip's face track. Without one, the crop stays centred.")
        }
        ThemedLabel {
            text: qsTr("Shape")
        }
        ThemedComboBox {
            id: aspectBox
            width: parent.width
            model: [qsTr("Vertical 9:16"), qsTr("Square 1:1"), qsTr("Portrait 4:5"), qsTr("Wide 16:9")]
        }
    }
}
