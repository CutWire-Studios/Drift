import QtQuick
import QtQuick.Controls.Basic
import Drift
// One delegate type for both folder and asset rows — not a DelegateChooser. A
// DelegateChooser turned out to change enough about how the chosen delegate is parented
// that it broke the drag: the asset card's Drag.active binding started detecting a binding
// loop the instant a drag began, and the drag died before DragHandler.active ever went
// true. Keeping one concrete item type, with the folder/asset visuals as sibling blocks
// toggled by `isFolder`, keeps the exact Column/Rectangle/DragHandler nesting that already
// works for the timeline drop.
Column {
    id: root

    required property bool isFolder
    required property string folderId
    required property string assetId
    required property string name
    required property string kind
    required property string duration
    required property double durationSeconds
    required property string path
    required property string thumbnailPath
    required property string filmstripPath
    required property int assetIndex
    required property string maskRole

    required property var host
    required property real cardSize

    readonly property bool selected: !isFolder && host.isAssetSelected(assetId)

    // Bound to the model snapshot by default, but re-readable in place: a rotate/trim
    // edit only ever changes this one card's thumbnail, so refreshing it directly here
    // avoids MediaAssetsTab's full combinedItems rebuild (and the scroll-position
    // flicker that comes with reassigning a GridView's whole model) for what is really a
    // single-image update.
    property string liveThumbnailPath: thumbnailPath
    // The Connections below replaces the binding, so a recycled card has to get it back.
    GridView.onReused: root.liveThumbnailPath = Qt.binding(() => root.thumbnailPath)
    GridView.onPooled: frame.closeMenus()
    // The thumbnail file keeps its name when its media is re-read, so the image URL carries this
    // to make the Image fetch the new pixels.
    property int thumbnailRevision: 0

    Connections {
        target: AssetLibrary
        function onAssetMetadataChanged(assetId) {
            if (!root.isFolder && assetId === root.assetId) {
                root.liveThumbnailPath = AssetLibrary.thumbnailAt(root.assetIndex)
                root.thumbnailRevision++
            }
        }
    }

    width: cardSize
    spacing: 4

    // Lift on grab: dims and grows slightly, so the card reads as
    // picked up rather than just sitting there while a ghost moves.
    opacity: frame.dragActive ? 0.85 : 1
    scale: frame.dragActive ? 1.04 : 1.0

    Behavior on opacity {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }
    Behavior on scale {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }

    Drag.active: !isFolder && frame.dragActive
    Drag.dragType: Drag.Automatic
    Drag.supportedActions: Qt.CopyAction
    Drag.keys: ["text/plain"]
    Drag.mimeData: { "text/plain": assetIndex.toString() }
    // Default hotspot is (0,0) at the card top-left; Wayland
    // compositors (notably Mutter) then deliver drop Y far from
    // the grab point. Center on the thumbnail like effect cards.
    Drag.hotSpot.x: width / 2
    Drag.hotSpot.y: cardSize * 9 / 32

    // Grid cards had neither hover feedback nor a pointing
    // cursor, while the list rows had both.
    HoverHandler {
        id: cardHover
        cursorShape: root.isFolder ? Qt.PointingHandCursor
                     : (frame.dragActive ? Qt.ClosedHandCursor : Qt.OpenHandCursor)
    }

    ThemedToolTip {
        text: root.isFolder ? name
              : qsTr("%1 — drag to the timeline, right-click to preview").arg(name)
        visible: cardHover.hovered && !frame.menusVisible
        // Explicit rather than the default above-parent Popup placement: for the top
        // row, that placement had no room to spare and overlapped the folder row/search
        // field above the grid.
        y: parent.height + 4
    }

    MediaAssetsGridFrame {
        id: frame
        isFolder: root.isFolder
        folderId: root.folderId
        assetId: root.assetId
        name: root.name
        kind: root.kind
        duration: root.duration
        thumbnailPath: root.thumbnailPath
        assetIndex: root.assetIndex
        maskRole: root.maskRole
        liveThumbnailPath: root.liveThumbnailPath
        thumbnailRevision: root.thumbnailRevision
        cardSize: root.cardSize
        selected: root.selected
        hovered: cardHover.hovered
        host: root.host
    }

    MediaAssetsGridNameRow {
        assetName: root.name
        proxyState: root.isFolder ? "" : root.host.proxyStateOf(root.assetId)
        editFriendly: !root.isFolder && root.host.editFriendlyOf(root.assetId)
        maskRole: root.maskRole
    }
}
