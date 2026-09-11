import QtQuick
import QtQuick.Controls.Basic
import Drift
import "components"

// Every download at once, on the phone.
//
// Deliberately not auto-raised. The per-tile ring in MarketTab is the primary surface and the
// whole point of it is that browsing continues while things download; a sheet that opened itself
// the moment a job started would undo exactly that. This is reached from the badge, when the user
// asks "what is happening".
AndroidBottomSheet {
    id: root

    title: qsTr("Downloads")

    readonly property var jobs: {
        void Market.downloadsRevision
        return Market.downloads
    }

    readonly property bool anyFinished: {
        for (let i = 0; i < root.jobs.length; ++i) {
            if (root.jobs[i].finished === true)
                return true
        }
        return false
    }

    Item {
        anchors.fill: parent

        ThemedLabel {
            anchors.centerIn: parent
            width: parent.width - Theme.androidPagePadding * 2
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            visible: root.jobs.length === 0
            text: qsTr("Nothing downloading right now.")
        }

        ListView {
            id: list
            anchors.fill: parent
            anchors.bottomMargin: clearRow.visible ? clearRow.height : 0
            clip: true
            model: root.jobs
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar { }

            delegate: Item {
                id: row
                required property var modelData
                width: list.width
                height: Theme.androidAddRowHeight

                readonly property bool busy: modelData.running === true

                IconGlyph {
                    id: kindIcon
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.androidPagePadding + root.safeLeft
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: DownloadFormat.kindGlyph(row.modelData)
                    iconSize: Theme.iconSizeLg
                    iconColor: DownloadFormat.statusColor(row.modelData)
                }

                Column {
                    anchors.left: kindIcon.right
                    anchors.leftMargin: Theme.spacingLg
                    anchors.right: rowAction.left
                    anchors.rightMargin: Theme.spacingLg
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2

                    Text {
                        width: parent.width
                        text: row.modelData.title || ""
                        color: Theme.panelForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSm
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                    }

                    Text {
                        width: parent.width
                        text: DownloadFormat.detailLine(row.modelData)
                        color: row.modelData.status === "failed" ? Theme.destructive
                                                                 : Theme.mutedForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                        elide: Text.ElideRight
                    }
                }

                // One trailing control, and which one follows the job: cancel what is running,
                // retry what failed and can be, nothing for what is already done.
                Item {
                    id: rowAction
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.androidPagePadding + root.safeRight
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.androidMinTouchTarget
                    height: Theme.androidMinTouchTarget

                    CircularProgress {
                        anchors.centerIn: parent
                        visible: row.busy
                        value: Number(row.modelData.progress || 0)
                        indeterminate: Number(row.modelData.progress || 0) <= 0
                        size: Theme.iconSizeXl
                        strokeWidth: 2
                    }

                    IconButton {
                        anchors.fill: parent
                        visible: row.busy
                        buttonSize: Theme.androidMinTouchTarget
                        iconSize: Theme.iconSizeMd
                        glyph: Theme.icons.x
                        variant: "text"
                        tooltip: qsTr("Cancel download")
                        onClicked: Market.cancelDownload(row.modelData.itemId)
                    }

                    IconButton {
                        anchors.fill: parent
                        visible: !row.busy && row.modelData.retryable === true
                        buttonSize: Theme.androidMinTouchTarget
                        iconSize: Theme.iconSizeMd
                        glyph: Theme.icons.refresh
                        variant: "text"
                        tooltip: qsTr("Try again")
                        onClicked: Market.retryDownload(row.modelData.itemId)
                    }
                }
            }
        }

        Item {
            id: clearRow
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: Theme.androidMinTouchTarget + Theme.spacingLg * 2
            visible: root.anyFinished

            ThemedButton {
                anchors.centerIn: parent
                variant: "secondary"
                text: qsTr("Clear finished")
                onClicked: Market.clearFinishedDownloads()
            }
        }
    }
}
