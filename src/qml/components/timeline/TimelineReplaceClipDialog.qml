pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Drift

// Picks another library asset to put under an existing timeline clip, keeping its place.
ThemedDialog {
    id: root

    property int trackIndex: -1
    property int clipIndex: -1
    property var candidates: []
    property var shown: []

    title: qsTr("Replace with…")
    showAccept: false
    rejectText: qsTr("Cancel")
    preferredWidth: Theme.dialogWidthMd
    acceptOnReturn: false

    function openFor(track, clip) {
        trackIndex = track
        clipIndex = clip
        candidates = EditorState.replaceCandidatesForClip(track, clip)
        searchField.text = ""
        refilter()
        open()
    }

    function refilter() {
        const needle = searchField.text.trim().toLowerCase()
        shown = needle === "" ? candidates
                              : candidates.filter(c => c.name.toLowerCase().indexOf(needle) >= 0)
        list.currentIndex = shown.length > 0 ? 0 : -1
    }

    function pick(row) {
        if (row < 0 || row >= shown.length)
            return
        const assetIndex = shown[row].assetIndex
        close()
        EditorState.replaceClipMedia(trackIndex, clipIndex, assetIndex)
    }

    onOpened: searchField.forceActiveFocus()

    contentItem: Column {
        width: parent ? parent.width : root.preferredWidth
        spacing: Theme.spacingMd

        ThemedTextField {
            id: searchField
            width: parent.width
            placeholderText: qsTr("Search media")
            font.family: Theme.fontFamily
            onTextChanged: root.refilter()
            Keys.onDownPressed: list.incrementCurrentIndex()
            Keys.onUpPressed: list.decrementCurrentIndex()
            Keys.onReturnPressed: root.pick(list.currentIndex)
            Keys.onEnterPressed: root.pick(list.currentIndex)
        }

        ListView {
            id: list
            width: parent.width
            height: Math.min(360, root.availableContentHeight - searchField.height - Theme.spacingMd)
            clip: true
            spacing: Theme.spacingXs
            model: root.shown
            currentIndex: -1
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar { }

            delegate: Rectangle {
                id: row
                required property int index
                required property var modelData
                readonly property bool highlighted: ListView.isCurrentItem

                width: ListView.view.width
                height: 48
                radius: Theme.c.mediaAssetsListRow.radius
                color: rowHover.hovered ? Theme.c.mediaAssetsListRow.backgroundHover
                                        : Theme.c.mediaAssetsListRow.background
                border.width: row.highlighted ? Theme.c.mediaAssetsListRow.borderWidth : 0
                border.color: Theme.c.mediaAssetsListRow.border

                Row {
                    id: rowContent
                    anchors.fill: parent
                    anchors.margins: Theme.spacingLg
                    spacing: Theme.spacingLg + Theme.spacingXs

                    MediaAssetsListThumb {
                        id: thumb
                        liveThumbnailPath: row.modelData.thumbnail
                        kind: row.modelData.kind
                    }
                    MediaAssetsListLabels {
                        assetName: row.modelData.name
                        kind: row.modelData.kind
                        duration: row.modelData.duration
                        width: Math.max(0, parent.width - thumb.width - rowContent.spacing)
                    }
                }

                HoverHandler {
                    id: rowHover
                    cursorShape: Qt.PointingHandCursor
                }
                TapHandler {
                    onTapped: root.pick(row.index)
                }
            }

            ThemedLabel {
                anchors.centerIn: parent
                visible: root.shown.length === 0
                size: "sm"
                text: qsTr("No matching media")
            }
        }
    }
}
