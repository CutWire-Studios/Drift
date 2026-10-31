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
Rectangle {
    id: root
    width: ListView.view ? ListView.view.width : 0
    height: 48
    radius: Theme.c.mediaAssetsListRow.radius
    color: rowHover.hovered ? Theme.c.mediaAssetsListRow.backgroundHover : Theme.c.mediaAssetsListRow.background
    // Matches the grid card's selection ring.
    border.width: root.selected ? Theme.c.mediaAssetsListRow.borderWidth : 0
    border.color: Theme.c.mediaAssetsListRow.border
    opacity: rowDrag.active ? Theme.c.mediaAssetsListRow.dragOpacity : 1
    scale: rowDrag.active ? 1.02 : 1.0

    Behavior on color {
        ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }
    Behavior on opacity {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }
    Behavior on scale {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }
    Behavior on border.width {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }

    required property bool isFolder
    required property string folderId
    required property string assetId
    required property string name
    required property string kind
    required property string duration
    required property string thumbnailPath
    required property int assetIndex
    required property string maskRole
    required property int depth
    required property bool expanded
    required property bool expandable

    // A typed reference to MediaAssetsTab creates a QML type-resolution cycle
    // because the tab also instantiates this delegate.
    required property var host

    // Tree rows step in by depth, and every row leaves room for a disclosure
    // arrow so names still line up under folders that have none.
    readonly property real treeIndent: host.treeMode ? root.depth * Theme.spacing2xl : 0
    readonly property real disclosureSpace: host.treeMode ? Theme.iconButtonSize : 0

    readonly property bool replaceBusy:
        !isFolder && EditorState.bin.replacingAssetId.length > 0
        && EditorState.bin.replacingAssetId === AssetLibrary.assetIdAt(assetIndex)
    readonly property bool selected: !isFolder && host.isAssetSelected(assetId)
    readonly property bool alreadyOnTimeline: {
        void EditorState.tracksRevision
        return !root.isFolder && root.assetIndex >= 0
               && EditorState.clipCountForAsset(root.assetIndex) > 0
    }

    // See the matching property on gridDelegate's cardRoot: lets a rotate/trim edit
    // refresh just this row's thumbnail without MediaAssetsTab rebuilding the whole list.
    property string liveThumbnailPath: thumbnailPath
    ListView.onReused: root.liveThumbnailPath = Qt.binding(() => root.thumbnailPath)
    ListView.onPooled: {
        rowMenu.close()
        folderRowMenu.close()
    }
    // See MediaAssetsGridCard: the thumbnail's name is unchanged across a re-read.
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

    Drag.active: !isFolder && rowDrag.active
    Drag.dragType: Drag.Automatic
    Drag.supportedActions: Qt.CopyAction
    Drag.keys: ["text/plain"]
    Drag.mimeData: { "text/plain": assetIndex.toString() }
    Drag.hotSpot.x: width / 2
    Drag.hotSpot.y: height / 2

    HoverHandler {
        id: rowHover
        cursorShape: isFolder ? Qt.PointingHandCursor
                     : (rowDrag.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor)
    }

    Row {
        id: listRowContent
        anchors.fill: parent
        anchors.margins: Theme.spacingLg
        anchors.leftMargin: Theme.spacingLg + root.treeIndent + root.disclosureSpace
        spacing: Theme.spacingLg + Theme.spacingXs

        MediaAssetsListThumb {
            id: listThumbFrame
            folder: root.isFolder
            liveThumbnailPath: root.liveThumbnailPath
            thumbnailRevision: root.thumbnailRevision
            kind: root.kind
            replaceBusy: root.replaceBusy
            showVfr: !root.isFolder && root.kind === "video" && host.vfrWarningOf(root.assetId)
            showMissing: !root.isFolder && host.missingOf(root.assetId)
        }

        MediaAssetsListLabels {
            folder: root.isFolder
            assetName: root.name
            kind: root.kind
            duration: root.duration
            proxyState: root.isFolder ? "" : host.proxyStateOf(root.assetId)
            editFriendly: !root.isFolder && host.editFriendlyOf(root.assetId)
            maskRole: root.maskRole
            // Derived from the actual thumbnail width
            // rather than a magic constant.
            width: Math.max(0, parent.width - listThumbFrame.width
                            - listRowContent.spacing - (root.isFolder ? 0 : quickAdd.width + Theme.spacingLg)
                            - (root.alreadyOnTimeline ? 78 : 0))
        }
    }

    ThemedToolTip {
        text: root.isFolder ? name
              : qsTr("%1 — drag to the timeline, right-click to preview").arg(name)
        visible: rowHover.hovered && !rowMenu.visible && !folderRowMenu.visible
        y: parent.height + 4
    }

    Item {
        anchors.fill: parent
        enabled: !replaceBusy

        TapHandler {
            id: leftRowTap
            acceptedButtons: Qt.LeftButton
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
            enabled: !rowDrag.active
            onTapped: {
                if (root.isFolder)
                    return

                // Keep keyboard actions scoped to this surface.
                host.forceActiveFocus()

                const mods = leftRowTap.point.modifiers
                if ((mods & Qt.ShiftModifier) !== 0) {
                    host.selectAssetRange(host.selectionAnchorId || root.assetId, root.assetId)
                } else if ((mods & Qt.ControlModifier) !== 0) {
                    host.toggleAssetSelection(root.assetId)
                    host.selectionAnchorId = root.assetId
                } else {
                    host.selectOnlyAsset(root.assetId)
                    host.selectionAnchorId = root.assetId
                }
            }
            onDoubleTapped: {
            if (root.isFolder)
                EditorState.bin.currentFolderId = root.folderId
            else if (kind === "composite")
                EditorState.openCompositeAsset(root.assetId)
        }
        }
        DragHandler {
            id: rowDrag
            target: null
            enabled: !root.isFolder
            acceptedButtons: Qt.LeftButton
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
            onActiveChanged: {
                if (active) {
                    EditorState.draggingAssetIndex = assetIndex
                } else {
                    Qt.callLater(function() {
                        if (!rowDrag.active)
                            EditorState.draggingAssetIndex = -1
                    })
                }
            }
        }
        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: {
                if (root.isFolder) {
                    folderRowMenu.popup()
                    return
                }
                host.forceActiveFocus()
                host.ensureAssetSelected(root.assetId)
                rowMenu.popup()
            }
        }

        // See the grid card: hold lifts, tap opens the menu — also a folder row's
        // only touch surface, so it stays enabled there too (empty dragKind so a
        // stray press-and-hold doesn't start a lift no drop target recognizes).
        TouchLiftArea {
            dragKind: root.isFolder ? "" : "media"
            payload: root.isFolder ? root.folderId : assetIndex
            label: name
            thumbnail: thumbnailPath
            glyph: kind === "audio" ? Theme.icons.music
                    : kind === "image" ? Theme.icons.image
                    : kind === "vector" ? Theme.icons.layers
                    : kind === "pdf" ? Theme.icons.fileText
                    : kind === "model3d" ? Theme.icons.box
                    : kind === "composite" ? Theme.icons.layers
                    : Theme.icons.film
            onLiftTapped: {
                if (root.isFolder) {
                    folderRowMenu.popup()
                    return
                }
                host.ensureAssetSelected(root.assetId)
                rowMenu.popup()
            }
        }
    }

    // Match the grid thumbnail's quick-add action in list and tree modes.
    AssetQuickAddButton {
        id: quickAdd
        z: 5
        visible: !root.isFolder
        revealed: rowHover.hovered
        enabled: !root.replaceBusy && root.assetIndex >= 0
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.rightMargin: Theme.spacingLg
        tooltip: qsTr("Add to timeline")
        onClicked: root.host.addToTimelineRequested([root.assetId])
    }

    Rectangle {
        z: 4
        visible: root.alreadyOnTimeline
        anchors.right: quickAdd.left
        anchors.rightMargin: 5
        anchors.verticalCenter: parent.verticalCenter
        width: rowAddedText.implicitWidth + 10
        height: rowAddedText.implicitHeight + 4
        radius: 3
        color: Theme.c.mediaAssetsAddedBadge.background
        Text {
            id: rowAddedText
            anchors.centerIn: parent
            text: qsTr("Added")
            color: Theme.c.mediaAssetsAddedBadge.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeTiny
        }
    }

    // Declared after the tap overlay above so it sits on top of it: the arrow has
    // to take the press itself, or the overlay's handlers would swallow it and the
    // row would only ever open on a double-click.
    IconButton {
        visible: host.treeMode && root.isFolder && root.expandable
        x: Theme.spacingLg + root.treeIndent
        anchors.verticalCenter: parent.verticalCenter
        buttonSize: Theme.iconButtonSize
        iconSize: Theme.iconSizeMd
        variant: "ghost"
        glyph: root.expanded ? Theme.icons.chevronDown : Theme.icons.chevronRight
        tooltip: root.expanded ? qsTr("Collapse folder") : qsTr("Expand folder")
        onClicked: host.toggleFolderExpanded(root.folderId)
    }

    MediaAssetsItemMenu {
        id: rowMenu
        host: root.host
        kind: root.kind
        assetId: root.assetId
        assetIndex: root.assetIndex
        maskRole: root.maskRole
    }

    MediaAssetsFolderMenu {
        id: folderRowMenu
        host: root.host
        folderId: root.folderId
        folderName: root.name
    }
}
