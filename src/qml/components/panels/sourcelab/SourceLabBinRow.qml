import QtQuick
import Drift

// One source in the Source Lab bin. The panel decides what a press loads.
Rectangle {
    id: root

    required property var asset
    property bool current: false

    readonly property string assetId: root.asset.id
    readonly property string kind: root.asset.kind
    readonly property string details: {
        const a = root.asset
        if (a.kind === "audio")
            return a.sampleRate > 0 ? qsTr("%1 kHz").arg(+(a.sampleRate / 1000).toFixed(1)) : ""
        if (a.width <= 0 || a.height <= 0)
            return ""
        const size = a.width + "×" + a.height
        return a.kind === "video" && a.fps > 0 ? qsTr("%1 · %2 fps").arg(size).arg(+a.fps.toFixed(2)) : size
    }

    property string liveThumbnailPath: root.asset.thumbnailPath
    property int thumbnailRevision: 0
    // An asset imported without previews gets its thumbnail when a row shows it.
    Component.onCompleted: {
        if (root.liveThumbnailPath === "")
            AssetLibrary.ensurePreview(AssetLibrary.indexOfId(root.assetId))
    }

    signal pressed()

    height: 44
    radius: Theme.c.sourceLabPanel.rowRadius
    color: root.current ? Theme.c.sourceLabPanel.rowSelected
           : rowHover.hovered ? Theme.c.sourceLabPanel.rowHover : "transparent"
    border.width: root.current ? Theme.c.mediaAssetsListRow.borderWidth : 0
    border.color: Theme.c.sourceLabPanel.rowSelectedBorder

    Connections {
        target: AssetLibrary
        function onAssetMetadataChanged(changedId) {
            if (changedId !== root.assetId)
                return
            root.liveThumbnailPath = AssetLibrary.thumbnailAt(AssetLibrary.indexOfId(changedId))
            root.thumbnailRevision++
        }
    }

    HoverHandler { id: rowHover }

    TapHandler {
        acceptedButtons: Qt.LeftButton
        onTapped: root.pressed()
    }

    MediaAssetsListThumb {
        id: thumb
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacingLg
        anchors.verticalCenter: parent.verticalCenter
        liveThumbnailPath: root.liveThumbnailPath
        thumbnailRevision: root.thumbnailRevision
        kind: root.kind
    }

    Row {
        id: trailing
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingLg
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.spacingMd

        SourceLabBinBadge {
            anchors.verticalCenter: parent.verticalCenter
            rating: root.asset.rating
            rangeCount: root.asset.rangeCount
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: root.kind !== "image"
            text: root.asset.duration
            color: Theme.c.mediaAssetsListLabels.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }

    Column {
        anchors.left: thumb.right
        anchors.leftMargin: Theme.spacingLg
        anchors.right: trailing.left
        anchors.rightMargin: Theme.spacingMd
        anchors.verticalCenter: parent.verticalCenter

        Text {
            width: parent.width
            text: root.asset.name
            color: Theme.c.mediaAssetsListLabels.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
            elide: Text.ElideRight
        }

        Text {
            width: parent.width
            visible: root.details.length > 0
            text: root.details
            color: Theme.c.mediaAssetsListLabels.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            elide: Text.ElideRight
        }
    }
}
