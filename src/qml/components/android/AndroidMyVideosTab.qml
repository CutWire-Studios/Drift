import QtQuick
import QtQuick.Controls.Basic
import Drift

// Home's My videos tab: everything exported, newest first. Entries whose file is gone are
// already filtered out by EditorState.exportHistory.
Item {
    id: root

    readonly property var videos: EditorState.exportHistory

    function durationText(seconds) {
        const total = Math.max(0, Math.round(seconds))
        const m = Math.floor(total / 60)
        const s = total % 60
        return m + ":" + (s < 10 ? "0" : "") + s
    }

    GridView {
        id: grid
        anchors.fill: parent
        anchors.leftMargin: Theme.androidPagePadding
        anchors.rightMargin: Theme.androidPagePadding
        anchors.topMargin: Theme.spacingLg
        visible: root.videos.length > 0
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: root.videos

        readonly property int columns: Math.max(2, Math.floor(width / 180))
        readonly property real gap: Theme.spacingMd
        cellWidth: width / columns
        cellHeight: (cellWidth - gap) * 9 / 16 + 56

        delegate: Item {
            id: cell
            required property var modelData

            width: grid.cellWidth
            height: grid.cellHeight

            Item {
                x: 0
                y: 0
                width: parent.width - grid.gap
                height: parent.height - grid.gap

                Rectangle {
                    id: thumb
                    width: parent.width
                    height: width * 9 / 16
                    radius: Theme.c.androidHomeRecentCard.thumbRadius
                    color: Theme.c.androidHomeRecentCard.thumbBackground
                    clip: true

                    IconGlyph {
                        anchors.centerIn: parent
                        visible: image.status !== Image.Ready
                        glyph: Theme.icons.film
                        iconSize: 18
                        iconColor: Theme.c.androidHomeRecentCard.icon
                    }

                    Image {
                        id: image
                        anchors.fill: parent
                        source: cell.modelData.thumbnail || ""
                        cache: false
                        asynchronous: true
                        fillMode: Image.PreserveAspectCrop
                    }

                    Rectangle {
                        visible: cell.modelData.durationSeconds > 0
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: Theme.spacingXs
                        width: badgeText.implicitWidth + Theme.spacingSm * 2
                        height: badgeText.implicitHeight + 2
                        radius: Theme.c.androidHomeMyVideos.badgeRadius
                        color: Theme.c.androidHomeMyVideos.badgeBackground

                        Text {
                            id: badgeText
                            anchors.centerIn: parent
                            text: root.durationText(cell.modelData.durationSeconds)
                            color: Theme.c.androidHomeMyVideos.badgeForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs
                        }
                    }
                }

                Text {
                    id: nameText
                    anchors.top: thumb.bottom
                    anchors.topMargin: Theme.spacingSm
                    anchors.left: parent.left
                    anchors.right: moreButton.left
                    text: cell.modelData.name
                    color: Theme.c.androidHomeRecentCard.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    font.weight: Font.Medium
                    elide: Text.ElideMiddle
                }

                Text {
                    anchors.top: nameText.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    text: {
                        const d = new Date(cell.modelData.created)
                        return isNaN(d.getTime()) ? "" : Qt.formatDateTime(d, Locale.ShortFormat)
                    }
                    color: Theme.c.androidHomeRecentCard.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    elide: Text.ElideRight
                }

                IconButton {
                    id: moreButton
                    anchors.right: parent.right
                    anchors.verticalCenter: nameText.verticalCenter
                    buttonSize: Theme.iconButtonSize
                    iconSize: Theme.iconSizeMd
                    glyph: Theme.icons.ellipsis
                    variant: "text"
                    tooltip: qsTr("Video actions")
                    onClicked: videoMenu.popup()
                }

                MouseArea {
                    z: -1
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: nameText.bottom
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    pressAndHoldInterval: 450
                    property bool heldMenu: false
                    onPressed: heldMenu = false
                    onPressAndHold: (mouse) => {
                        if (mouse.source === Qt.MouseEventNotSynthesized)
                            return
                        heldMenu = true
                        Haptics.pickUp()
                        videoMenu.popup()
                    }
                    onClicked: (mouse) => {
                        if (heldMenu)
                            return
                        if (mouse.button === Qt.RightButton) {
                            videoMenu.popup()
                            return
                        }
                        Haptics.select()
                        EditorState.viewFile(cell.modelData.uri)
                    }
                }

                ThemedContextMenu {
                    id: videoMenu

                    ThemedMenuItem {
                        text: qsTr("Share")
                        icon.name: Theme.icons.upload
                        onTriggered: EditorState.shareFile(cell.modelData.uri)
                    }

                    ThemedMenuItem {
                        text: qsTr("Remove from list")
                        icon.name: Theme.icons.trash
                        onTriggered: EditorState.removeExport(cell.modelData.uri)
                    }
                }
            }
        }
    }

    EmptyState {
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.spacing3xl, 300)
        visible: root.videos.length === 0
        glyph: Theme.icons.video
        title: qsTr("No exported videos yet")
        hint: qsTr("Exported videos appear here")
    }
}
