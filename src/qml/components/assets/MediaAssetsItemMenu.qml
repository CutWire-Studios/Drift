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

    readonly property bool single: host.selectedAssetIds.length <= 1
    property var subMenus: []
    property bool hasSubMenus: false

    // A sub-menu cannot be hidden per asset, so each is built only when it has something to show.
    onAboutToShow: {
        for (let i = 0; i < subMenus.length; i++)
            removeMenu(subMenus[i])
        subMenus = []
        const specs = [
            { anchor: playbackAnchor, component: playbackMenuComponent,
              show: kind === "video" || host.selectedWithProxyState(["none", "ready", "queued", "building"]).length > 0 },
            { anchor: maskAnchor, component: maskMenuComponent,
              show: (kind === "video" || kind === "image") && single }
        ]
        for (let s = 0; s < specs.length; s++) {
            if (!specs[s].show)
                continue
            const sub = specs[s].component.createObject(null)
            for (let i = 0; i < count; i++) {
                if (itemAt(i) === specs[s].anchor) {
                    insertMenu(i + 1, sub)
                    break
                }
            }
            subMenus.push(sub)
        }
        hasSubMenus = subMenus.length > 0
    }

    ThemedMenuItem {
        text: qsTr("Open composite")
        icon.name: Theme.icons.layers
        visible: root.kind === "composite" && root.single
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
        text: root.kind === "video" ? qsTr("Preview and trim…") : qsTr("Preview and edit…")
        icon.name: Theme.icons.eye
        visible: root.kind !== "composite"
        onTriggered: root.host.previewRequested(root.assetIndex)
    }
    ThemedMenuSeparator { }
    ThemedMenuItem {
        text: qsTr("Rename…")
        icon.name: Theme.icons.pencil
        visible: root.single
        onTriggered: root.host.renameRequested(root.assetIndex)
    }
    ThemedMenuItem {
        text: qsTr("Replace media…")
        icon.name: Theme.icons.refresh
        visible: root.kind !== "composite" && root.single
        onTriggered: root.host.replaceRequested(root.assetIndex)
    }
    ThemedMenuItem {
        text: qsTr("Reload from disk")
        icon.name: Theme.icons.reset
        visible: root.kind !== "composite" && root.single
        onTriggered: {
            if (!EditorState.bin.refreshSource(root.assetIndex))
                Toasts.error(qsTr("Couldn’t reload this media. It may not be a file on disk, or is already being read."))
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
    ThemedMenuSeparator { }
    ThemedMenuItem { id: playbackAnchor; visible: false }
    ThemedMenuItem { id: maskAnchor; visible: false }
    ThemedMenuSeparator { visible: root.hasSubMenus }
    WorkspaceMenuItem {
        text: qsTr("Prepare in Source Lab…")
        icon.name: Theme.icons.sparkles
        visible: root.kind === "video" && root.single && !ShellBridge.phoneShell
        target: ({ panel: "sourceLab.viewer", assetId: root.assetId })
    }
    ThemedMenuItem {
        readonly property var ids: root.host.selectedWithProxyState(["ready", "queued", "building"])
        text: ids.length > 1 ? qsTr("Remove %n proxies", "", ids.length) : qsTr("Remove proxy")
        icon.name: Theme.icons.x
        visible: ids.length > 0
        onTriggered: AssetLibrary.removeProxies(ids)
    }
    ThemedMenuItem {
        text: qsTr("Convert to Apple ProRes 422…")
        icon.name: Theme.icons.rabbit
        visible: root.kind === "video" && !root.host.editFriendlyOf(root.assetId)
        onTriggered: {
            const targets = root.host.selectedAssetIds.length > 0 ? root.host.selectedAssetIds : [root.assetId]
            EditorState.bin.convertToProRes(targets)
        }
    }
    ThemedMenuItem {
        text: qsTr("Convert to edit-friendly format")
        icon.name: Theme.icons.rabbit
        visible: root.kind === "video" && root.host.selectedAssetIds.length <= 1
                 && !root.host.editFriendlyOf(root.assetId)
        onTriggered: EditorState.bin.convertToConstantFrameRate([root.assetId])
    }
    ThemedMenuItem {
        text: qsTr("Save a copy…")
        icon.name: Theme.icons.save
        visible: root.kind !== "composite" && root.single
        onTriggered: root.host.saveAsRequested(root.assetIndex)
    }
    ThemedMenuItem {
        text: qsTr("Export image…")
        icon.name: Theme.icons.save
        visible: root.kind === "image"
        onTriggered: root.host.exportRequested(root.assetIndex)
    }
    ThemedMenuSeparator { }
    ThemedMenuItem {
        text: root.host.selectedAssetIds.length > 1
              ? qsTr("Remove %n items from project", "", root.host.selectedAssetIds.length)
              : qsTr("Remove from project")
        icon.name: Theme.icons.trash
        onTriggered: root.host.removeRequested(root.host.selectedAssetIds)
    }

    Component {
        id: playbackMenuComponent
        ThemedContextMenu {
            title: qsTr("Playback")

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
                visible: root.kind === "video" && root.single
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
                visible: root.kind === "video" && root.single && !root.host.editFriendlyOf(root.assetId)
                onTriggered: EditorState.bin.convertToConstantFrameRate([root.assetId])
            }
        }
    }

    Component {
        id: maskMenuComponent
        ThemedContextMenu {
            title: qsTr("Use as mask")

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
}
