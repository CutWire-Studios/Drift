import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
// Thumbnail tile of a media grid card. Handlers live on this rectangle, not on the
// column that owns the Drag attached property — see MediaAssetsGridCard.
Rectangle {
    id: root

    required property bool isFolder
    required property string folderId
    required property string assetId
    required property string name
    required property string kind
    required property string duration
    required property string thumbnailPath
    required property int assetIndex
    required property string maskRole
    required property string liveThumbnailPath
    required property int thumbnailRevision
    required property real cardSize
    required property bool selected
    required property bool hovered
    // Avoid a QML type-resolution cycle: MediaAssetsTab instantiates
    // MediaAssetsGridCard, which in turn instantiates this frame.
    required property var host

    readonly property alias dragActive: assetDrag.active
    readonly property bool menusVisible: cardMenu.visible || folderMenu.visible
    // tracksRevision is the lightweight change notification; clipCountForAsset()
    // is a query, so by itself it would not update when clips are removed or undone.
    readonly property bool alreadyOnTimeline: {
        void EditorState.tracksRevision
        return !root.isFolder && root.assetIndex >= 0
               && EditorState.clipCountForAsset(root.assetIndex) > 0
    }

    function closeMenus() {
        cardMenu.close()
        folderMenu.close()
    }

    width: cardSize
    height: cardSize * 9 / 16
    radius: Theme.c.mediaAssetsGridFrame.radius
    color: Theme.c.mediaAssetsGridFrame.background
    clip: true
    // Hover ring only here; the selection ring is a separate overlay
    // (below) so it always paints on top of the thumbnail image.
    border.width: (!root.selected && root.hovered) ? Theme.c.mediaAssetsGridFrame.borderWidth : 0
    border.color: Theme.c.mediaAssetsGridFrame.border
    scale: (!root.isFolder && root.hovered) ? 1.03 : 1.0

    Behavior on scale {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }
    Behavior on border.width {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }
    Behavior on color {
        ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }

    IconGlyph {
        visible: root.isFolder
        anchors.centerIn: parent
        glyph: Theme.icons.folder
        iconSize: Theme.spacing3xl
        iconColor: Theme.c.mediaAssetsGridFrame.icon
    }

    // Placeholder while the thumbnail decodes. The
    // card used to sit empty, indistinguishable from
    // a failed load.
    SkeletonBox {
        anchors.fill: parent
        radius: parent.radius
        visible: !root.isFolder && root.liveThumbnailPath.length > 0
                    && gridThumb.status === Image.Loading
    }

    Image {
        id: gridThumb
        anchors.fill: parent
        visible: !root.isFolder && root.liveThumbnailPath.length > 0 && status === Image.Ready
        source: !root.isFolder && root.liveThumbnailPath.length > 0
                ? EditorState.imageUrl(root.liveThumbnailPath) + "?rev=" + root.thumbnailRevision : ""
        fillMode: Image.PreserveAspectFit
        asynchronous: true
        sourceSize: Qt.size(Math.ceil(cardSize * Screen.devicePixelRatio),
                             Math.ceil(cardSize * 9 / 16 * Screen.devicePixelRatio))
        // Fades in rather than popping at full opacity.
        opacity: status === Image.Ready ? 1 : 0

        Behavior on opacity {
            NumberAnimation { duration: Theme.durationBase; easing.type: Theme.easing }
        }
    }

    IconGlyph {
        anchors.centerIn: parent
        // Also covers Image.Error, so a missing
        // thumbnail file falls back to the kind icon
        // instead of staying blank forever.
        visible: !root.isFolder && root.hovered
                 && (root.liveThumbnailPath.length === 0 || gridThumb.status === Image.Error)
        glyph: kind === "audio" ? Theme.icons.music
                : kind === "image" ? Theme.icons.image
                : kind === "vector" ? Theme.icons.layers
                : kind === "pdf" ? Theme.icons.fileText
                : kind === "model3d" ? Theme.icons.box
                : kind === "composite" ? Theme.icons.layers
                : Theme.icons.film
        iconSize: Theme.spacing3xl
        iconColor: Theme.c.mediaAssetsGridFrame.icon
    }

    MediaAssetsGridBusyOverlay {
        id: busyOverlay
        folder: root.isFolder
        assetIndex: assetIndex
    }

    VfrWarning {
        z: 3
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.spacingSm
        visible: !root.isFolder && kind === "video" && host.vfrWarningOf(root.assetId)
    }

    VfrWarning {
        z: 3
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.spacingSm
        background: Theme.c.missingMediaWarning.background
        foreground: Theme.c.missingMediaWarning.foreground
        cornerRadius: Theme.c.missingMediaWarning.radius
        tip: qsTr("Media file not found. Use Replace media… on this item to point it at the file again.")
        visible: !root.isFolder && host.missingOf(root.assetId)
    }

    // Proxy build progress along the thumbnail's bottom edge; the pill beside the
    // name below says which state it is in.
    ThemedProgressBar {
        z: 1
        visible: !root.isFolder && AssetLibrary.proxyBusy
                 && host._badgeRevision >= 0
                 && AssetLibrary.proxyState(root.assetId) === "building"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        barHeight: Theme.spacingXs
        value: AssetLibrary.proxyProgress
    }

    MediaAssetsDurationBadge {
        label: !root.isFolder ? duration : ""
    }

    // Like CapCut's media bin: the label reflects actual clip references rather
    // than only the last click. The + stays available to insert another instance.
    Rectangle {
        z: 4
        visible: root.alreadyOnTimeline
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: 4
        width: addedLabel.implicitWidth + 10
        height: addedLabel.implicitHeight + 4
        radius: 3
        color: Theme.c.mediaAssetsAddedBadge.background

        Text {
            id: addedLabel
            anchors.centerIn: parent
            text: qsTr("Added")
            color: Theme.c.mediaAssetsAddedBadge.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeTiny
            font.weight: Font.Medium
        }
    }

    AssetQuickAddButton {
        id: addToTimelineButton
        z: 5
        visible: !root.isFolder
        revealed: root.hovered
        enabled: !busyOverlay.busy && root.assetIndex >= 0
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 4
        tooltip: qsTr("Add to timeline")
        onClicked: root.host.addToTimelineRequested([root.assetId])
    }

    MediaAssetsSelectionRing {
        selected: root.selected
    }

    // Both handlers live on this child, not on the Column
    // that owns the Drag attached property. With them on the
    // Column, QDrag::exec() ungrabs the very item Drag.active
    // is attached to, the handler deactivates inside
    // setActive(true), and the `Drag.active: assetDrag.active`
    // binding re-enters it — "Binding loop detected for
    // property active", and the drag dies mid-flight.
    // EffectBrowser and ShapesTab already nest them this way.
    TapHandler {
        id: leftTap
        acceptedButtons: Qt.LeftButton
        // A finger is handled by the lift area below — the menu
        // is a tap there.
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
        enabled: !assetDrag.active
        onTapped: {
            if (root.isFolder)
                return

            // The most recently interacted editing surface owns Delete.
            host.forceActiveFocus()

            // Qt remaps ControlModifier to Cmd on macOS, so this is already the
            // right "system key" on every desktop platform without branching.
            const mods = leftTap.point.modifiers
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
        id: assetDrag
        // Without target: null the handler moves the card itself,
        // clobbering the Grid positioner's x/y.
        target: null
        // A finger lifts through TouchDrag instead: a platform drag has
        // no touch gesture and cannot leave the sheet. Folders aren't
        // draggable at all — moving an asset into one is a context-menu
        // action only ("Move to folder…"), not a drag-and-drop target.
        enabled: !root.isFolder
        acceptedButtons: Qt.LeftButton
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
        onActiveChanged: {
            if (active) {
                EditorState.draggingAssetIndex = assetIndex
            } else {
                Qt.callLater(function() {
                    if (!assetDrag.active)
                        EditorState.draggingAssetIndex = -1
                })
            }
        }
    }
    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: {
            if (root.isFolder) {
                folderMenu.popup()
                return
            }
            // Right-clicking a card that's part of the current multi-selection
            // extends the bulk menu items to all of it; right-clicking anything
            // else replaces the selection with just that card first — same
            // "select if not already selected" rule the timeline's clip right-click
            // already uses (TimelineClipItem.qml).
            host.forceActiveFocus()
            host.ensureAssetSelected(root.assetId)
            cardMenu.popup()
        }
    }

    // Hold to carry the asset onto the timeline, tap for the menu. The phone's
    // asset browser is a modal sheet, which the platform drag above cannot leave,
    // so touch gets the lift instead. Folders aren't draggable, but this is still
    // the only tap surface touch has here — leaving it disabled for folder rows
    // left touch with no way to open a folder or reach its rename/delete menu at
    // all. A folder's dragKind is left empty so a stray press-and-hold doesn't
    // start a lift no drop target recognizes as media.
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
                folderMenu.popup()
                return
            }
            // Touch has no modifier-key multi-select, so a lift-tap always means
            // "just this card" unless it's already part of a selection built some
            // other way (there isn't one yet, but this keeps the same rule as the
            // desktop right-click rather than special-casing touch).
            host.ensureAssetSelected(root.assetId)
            cardMenu.popup()
        }
    }

    MediaAssetsItemMenu {
        id: cardMenu
        host: root.host
        kind: root.kind
        assetId: root.assetId
        assetIndex: root.assetIndex
        maskRole: root.maskRole
    }

    MediaAssetsFolderMenu {
        id: folderMenu
        host: root.host
        folderId: root.folderId
        folderName: root.name
    }
}
