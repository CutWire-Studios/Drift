import QtQuick
import Drift

// One download: kind glyph, title, status, and the one trailing control the
// job's state allows — cancel, retry, or nothing.
Item {
    id: row

    required property var modelData
    property real sideInset: 0
    property real sideInsetRight: 0

    height: Theme.androidAddRowHeight

    readonly property bool busy: modelData.running === true

    IconGlyph {
        id: kindIcon
        anchors.left: parent.left
        anchors.leftMargin: Theme.androidPagePadding + row.sideInset
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
            color: Theme.c.androidDownloadJobRow.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
            font.weight: Font.Medium
            elide: Text.ElideRight
        }

        Text {
            width: parent.width
            text: DownloadFormat.detailLine(row.modelData)
            color: row.modelData.status === "failed"
                  ? Theme.c.androidDownloadJobRow.foregroundFailed
                  : Theme.c.androidDownloadJobRow.foregroundMuted
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
        anchors.rightMargin: Theme.androidPagePadding + row.sideInsetRight
        anchors.verticalCenter: parent.verticalCenter
        width: Theme.androidMinTouchTarget
        height: Theme.androidMinTouchTarget

        CircularProgress {
            anchors.centerIn: parent
            visible: row.busy
            value: Number(row.modelData.progress || 0)
            indeterminate: Number(row.modelData.progress || 0) <= 0
            size: Theme.iconSizeXl
            strokeWidth: Theme.c.androidDownloadJobRow.progressStrokeWidth
        }

        IconButton {
            anchors.fill: parent
            visible: row.busy
            buttonSize: Theme.androidMinTouchTarget
            iconSize: Theme.iconSizeMd
            glyph: Theme.icons.x
            variant: "text"
            tooltip: qsTr("Cancel download")
            onClicked: Downloads.cancel(row.modelData.itemId)
        }

        IconButton {
            anchors.fill: parent
            visible: !row.busy && row.modelData.retryable === true
            buttonSize: Theme.androidMinTouchTarget
            iconSize: Theme.iconSizeMd
            glyph: Theme.icons.refresh
            variant: "text"
            tooltip: qsTr("Try again")
            onClicked: Downloads.retry(row.modelData.itemId)
        }
    }
}
