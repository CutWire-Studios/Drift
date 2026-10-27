import QtQuick
import Drift
// Context menu for one media item, shared by the grid card and the list row.
ThemedContextMenu {
    id: root

    required property var host
    required property string kind
    required property string assetId
    required property int assetIndex
    required property string maskRole

    ThemedMenuItem {
        text: qsTr("Open composite")
        icon.name: Theme.icons.layers
        visible: root.kind === "composite" && root.host.selectedAssetIds.length <= 1
        onTriggered: EditorState.openCompositeAsset(root.assetId)
    }
    ThemedMenuItem {
        text: root.host.selectedAssetIds.length > 1
              ? qsTr("Add %n items to timeline", "", root.host.selectedAssetIds.length)
              : qsTr("Add to timeline")
        icon.name: Theme.icons.plus
        onTriggered: root.host.addToTimelineRequested(root.host.selectedAssetIds)
    }
    ThemedMenuItem {
        text: root.kind === "video" ? qsTr("Trim, crop and upscale…") : qsTr("Preview and edit…")
        icon.name: Theme.icons.eye
        visible: root.kind !== "composite"
        onTriggered: root.host.previewRequested(root.assetIndex)
    }
    ThemedMenuItem {
        text: qsTr("Rename…")
        icon.name: Theme.icons.pencil
        visible: root.host.selectedAssetIds.length <= 1
        onTriggered: root.host.renameRequested(root.assetIndex)
    }
    ThemedMenuItem {
        text: qsTr("Replace media…")
        icon.name: Theme.icons.refresh
        visible: root.kind !== "composite" && root.host.selectedAssetIds.length <= 1
        onTriggered: root.host.replaceRequested(root.assetIndex)
    }
    ThemedMenuItem {
        text: qsTr("Use as mask")
        icon.name: Theme.icons.mask
        visible: (root.kind === "video" || root.kind === "image") && root.host.selectedAssetIds.length <= 1
        subMenu: ThemedContextMenu {
            ThemedMenuItem {
                text: qsTr("Off")
                icon.name: root.maskRole === "" ? Theme.icons.check : ""
                onTriggered: EditorState.masking.setAssetMaskRole(root.assetIndex, "")
            }
            ThemedMenuItem {
                text: qsTr("Luma (brightness)")
                icon.name: root.maskRole === "luma" ? Theme.icons.check : ""
                onTriggered: EditorState.masking.setAssetMaskRole(root.assetIndex, "luma")
            }
            ThemedMenuItem {
                text: qsTr("Alpha (transparency)")
                icon.name: root.maskRole === "alpha" ? Theme.icons.check : ""
                onTriggered: EditorState.masking.setAssetMaskRole(root.assetIndex, "alpha")
            }
        }
    }
    ThemedMenuItem {
        // "the selection" once it covers more than the clicked card — matches
        // the count the bulk actions below will actually act on.
        text: root.host.selectedAssetIds.length > 1
              ? qsTr("Move %n items to folder…", "", root.host.selectedAssetIds.length)
              : qsTr("Move to folder…")
        icon.name: Theme.icons.folder
        visible: BinFolderModel.count > 0
        onTriggered: root.host.moveToFolderRequested(root.host.selectedAssetIds)
    }
    ThemedMenuItem {
        readonly property var ids: root.host.selectedWithProxyState(["none"])
        text: ids.length > 1 ? qsTr("Create %n proxies", "", ids.length) : qsTr("Create proxy")
        icon.name: Theme.icons.filePlay
        visible: ids.length > 0
        onTriggered: AssetLibrary.createProxies(ids)
    }
    ThemedMenuItem {
        text: qsTr("Link proxy…")
        icon.name: Theme.icons.filePlay
        visible: root.kind === "video" && root.host.selectedAssetIds.length <= 1
                 && root.host.selectedWithProxyState(["none", "linked"]).length === 1
        onTriggered: root.host.linkProxyRequested(root.assetId)
    }
    ThemedMenuItem {
        readonly property var ids: root.host.selectedWithProxyState(["ready", "queued", "building"])
        text: ids.length > 1 ? qsTr("Remove %n proxies", "", ids.length) : qsTr("Remove proxy")
        icon.name: Theme.icons.x
        visible: ids.length > 0
        onTriggered: AssetLibrary.removeProxies(ids)
    }
    ThemedMenuItem {
        text: qsTr("Convert to edit-friendly format")
        icon.name: Theme.icons.rabbit
        visible: root.kind === "video" && root.host.selectedAssetIds.length <= 1
                 && !root.host.editFriendlyOf(root.assetId)
        onTriggered: EditorState.bin.convertToConstantFrameRate([root.assetId])
    }
    ThemedMenuItem {
        text: qsTr("Export image…")
        icon.name: Theme.icons.save
        visible: root.kind === "image"
        onTriggered: root.host.exportRequested(root.assetIndex)
    }
    ThemedMenuItem {
        text: qsTr("Save As…")
        icon.name: Theme.icons.save
        visible: root.kind !== "composite" && root.host.selectedAssetIds.length <= 1
        onTriggered: root.host.saveAsRequested(root.assetIndex)
    }
    ThemedMenuItem {
        text: root.host.selectedAssetIds.length > 1
              ? qsTr("Remove %n items from project", "", root.host.selectedAssetIds.length)
              : qsTr("Remove from project")
        icon.name: Theme.icons.trash
        onTriggered: root.host.removeRequested(root.host.selectedAssetIds)
    }
}
