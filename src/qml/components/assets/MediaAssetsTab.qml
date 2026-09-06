import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

// Shared media browser: the kinds-filtered AssetLibrary grid/list used by the
// Media tab. The parent owns width/height/visible and the import orchestration,
// wiring preview/import intents back through the callbacks below.
Item {
    id: root

    // Delete/Backspace belongs only to the editing surface that currently
    // owns keyboard focus. A media selection may remain highlighted while the
    // user returns to the timeline, so this must NOT be a global shortcut.
    focus: false

    Keys.onPressed: function(event) {
        if ((event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace)
                && root.selectedAssetIds.length > 0) {
            root.removeRequested(root.selectedAssetIds)
            event.accepted = true
        }
    }

    // Mirrors the header's view toggle, owned by the parent so the toolbar and
    // grid stay in sync.
    property bool gridMode: true
    // True while an import is running, so the empty state can step aside.
    property bool importing: false
    // Kind visibility filter supplied by the parent (depends on the active tab).
    property var assetVisibleFn: function(kind) { return true }
    readonly property string query: search.text.trim().toLowerCase()

    // Emitted from the card/row context menu — adds the selected asset(s) to the timeline in
    // selection order. The parent routes a single id to the existing single-asset add so that
    // case behaves exactly as it always has; a multi-selection goes through the batch add.
    signal addToTimelineRequested(var assetIds)
    // Emitted from the card/row context menu. Opens the preview/edit window.
    signal previewRequested(int assetIndex)
    // Emitted from the card/row context menu. Carries every selected asset's id, not just the
    // one clicked — see selectedAssetIds below. The parent owns the in-use check and the
    // confirmation.
    signal removeRequested(var assetIds)
    // Emitted from the card/row context menu. The parent owns the file picker.
    signal replaceRequested(int assetIndex)
    // Emitted from the card/row context menu. The parent owns the rename dialog.
    signal renameRequested(int assetIndex)
    // Emitted from the card/row context menu, image rows only. The parent owns the save dialog.
    signal exportRequested(int assetIndex)
    // Emitted when the empty-state action asks to import media.
    signal importRequested()
    // Emitted from the card/row context menu — the only way to move an asset into a folder;
    // there is no drag-onto-a-folder-tile path on desktop or touch. Carries every selected
    // asset's id, not just the one clicked — see selectedAssetIds below. The parent owns the
    // folder-picker dialog.
    signal moveToFolderRequested(var assetIds)
    // Emitted from a folder tile's context menu. The parent owns the rename dialog.
    signal folderRenameRequested(string folderId, string folderName)
    // Emitted from a folder tile's context menu. The parent owns the folder-picker dialog —
    // the same one moveToFolderRequested above opens for assets.
    signal folderMoveRequested(string folderId)

    // Multi-select, by asset id rather than position: assetIndex is a live row position that
    // shifts on removal/reorder, so an id survives everything except the asset itself going
    // away. Folders are never selectable — only individual media items are, per the request
    // this was built for. Plain click replaces the selection with just that card; Ctrl/Cmd+click
    // (Qt remaps ControlModifier to Cmd on macOS already, so no platform branch is needed) toggles
    // one card in or out of it. Right-click (or touch's tap-for-menu) on a card already in the
    // selection keeps the whole selection and extends the bulk menu items to it; on a card that
    // isn't, it replaces the selection with just that card first, matching how the timeline's
    // clip selection already treats a right-click (TimelineClipItem.qml).
    property var selectedAssetIds: []

    function isAssetSelected(assetId) {
        return assetId.length > 0 && root.selectedAssetIds.indexOf(assetId) >= 0
    }
    function selectOnlyAsset(assetId) {
        root.selectedAssetIds = [assetId]
    }
    function clearAssetSelection() {
        root.selectedAssetIds = []
        root.selectionAnchorId = ""
    }
    function toggleAssetSelection(assetId) {
        const at = root.selectedAssetIds.indexOf(assetId)
        if (at < 0) {
            root.selectedAssetIds = root.selectedAssetIds.concat([assetId])
        } else {
            const next = root.selectedAssetIds.slice()
            next.splice(at, 1)
            root.selectedAssetIds = next
        }
    }
    // Ensures `assetId` is part of the selection before a menu opens on it, without disturbing
    // an existing multi-selection it's already a member of.
    function ensureAssetSelected(assetId) {
        if (!root.isAssetSelected(assetId))
            root.selectOnlyAsset(assetId)
    }

    // Anchor for Shift-click range selection: the asset id from the last plain or Ctrl/Cmd
    // click. A Shift-click itself doesn't move it, so a run of Shift-clicks grows or shrinks
    // the same range instead of walking it forward each time.
    property string selectionAnchorId: ""

    // combinedItems in visual/tab order, assets only — folders can't be range-selected and
    // never appear in selectedAssetIds.
    function orderedAssetIds() {
        const ids = []
        for (let i = 0; i < root.combinedItems.length; ++i) {
            const item = root.combinedItems[i]
            if (!item.isFolder)
                ids.push(item.assetId)
        }
        return ids
    }

    // Adds every asset between anchorId and targetId (inclusive of both ends) to the
    // selection, in whatever order they currently render in. Purely additive — ids already
    // selected outside the range are left alone, matching how every other selection tool
    // in this bin (Ctrl-click, bulk menu actions) never silently drops a prior pick.
    function selectAssetRange(anchorId, targetId) {
        const order = root.orderedAssetIds()
        const anchorAt = order.indexOf(anchorId)
        const targetAt = order.indexOf(targetId)
        if (anchorAt < 0 || targetAt < 0) {
            // No valid anchor to range from — fall back to a plain-click selection, and
            // promote the target to the new anchor so the next Shift-click ranges from
            // here instead of silently repeating this fallback.
            root.selectOnlyAsset(targetId)
            root.selectionAnchorId = targetId
            return
        }
        const lo = Math.min(anchorAt, targetAt)
        const hi = Math.max(anchorAt, targetAt)
        const next = root.selectedAssetIds.slice()
        for (let i = lo; i <= hi; ++i) {
            if (next.indexOf(order[i]) < 0)
                next.push(order[i])
        }
        root.selectedAssetIds = next
    }

    // Prunes ids that dropped out of view — removed, moved to a different folder, filtered out
    // by search/kind, or the folder was navigated away from (combinedItems is scoped to the
    // current folder, so leaving it empties out every id that was in it). Also drops the range
    // anchor if it dropped out along with them — left stale, it would make every future
    // Shift-click silently fall back to a single-item selection instead of a range. Reassigning
    // only when something actually changed avoids spamming dependents on every unrelated
    // combinedItems recompute.
    onCombinedItemsChanged: {
        if (root.selectedAssetIds.length === 0 && root.selectionAnchorId.length === 0)
            return
        const present = {}
        for (let i = 0; i < root.combinedItems.length; ++i) {
            const item = root.combinedItems[i]
            if (!item.isFolder)
                present[item.assetId] = true
        }
        const pruned = root.selectedAssetIds.filter(id => present[id] === true)
        if (pruned.length !== root.selectedAssetIds.length)
            root.selectedAssetIds = pruned
        if (root.selectionAnchorId.length > 0 && present[root.selectionAnchorId] !== true)
            root.selectionAnchorId = ""
    }

    // Bumped on every project edit (rename, folder create/delete/reparent, move-to-folder,
    // undo/redo — anything that goes through pushProjectEdit) and on every async metadata
    // update (import probe/thumbnail landing, replace finishing) so combinedItems below
    // recomputes for changes a plain JS array wouldn't otherwise notice: unlike binding a
    // view's `model` straight to AssetLibrary, this array is a one-shot snapshot with no
    // subscription to the model's own dataChanged — without this tick, a card would keep
    // showing its provisional kind/duration/thumbnail until an unrelated edit happened to
    // rebuild the array.
    //
    // Routed through a short debounce rather than bumped directly: assigning a freshly-built
    // array to a view's `model` reconstructs every delegate, not just the row that changed.
    // A bulk import lands a metadata signal per file per probe/thumbnail stage in quick
    // succession, and without coalescing that into one rebuild, the whole grid would be torn
    // down and rebuilt once per signal — quadratic work, and a chance of destroying a delegate
    // out from under a card that owns an active drag or has its context menu open.
    property int _refreshTick: 0
    Timer {
        id: refreshCoalesceTimer
        interval: 100
        onTriggered: root._refreshTick++
    }
    Connections {
        target: EditorState
        function onUndoStackChanged() { refreshCoalesceTimer.restart() }
    }
    Connections {
        target: AssetLibrary
        function onAssetMetadataChanged(assetId) { refreshCoalesceTimer.restart() }
    }

    // Single source of truth for what's shown: folders in the current bin folder first, then
    // its media, one flat array — not two separately-scrolling views. Every entry carries the
    // same set of keys regardless of kind (folder rows get placeholder asset fields and vice
    // versa) so one delegate below can bind every field as `required` without runtime errors.
    readonly property var combinedItems: {
        void root._refreshTick
        const q = root.query
        const currentFolder = EditorState.currentBinFolderId
        const items = []

        for (let j = 0; j < BinFolderModel.count; ++j) {
            const folder = BinFolderModel.folderAt(j)
            if (folder.parentId !== currentFolder)
                continue
            if (q.length > 0 && folder.name.toLowerCase().indexOf(q) < 0)
                continue
            items.push({
                isFolder: true,
                folderId: folder.id,
                assetId: "",
                name: folder.name,
                assetIndex: -1,
                kind: "",
                duration: "",
                durationSeconds: 0,
                path: "",
                thumbnailPath: "",
                filmstripPath: ""
            })
        }

        for (let i = 0; i < AssetLibrary.count; ++i) {
            const asset = AssetLibrary.assetAt(i)
            if (asset.folderId !== currentFolder)
                continue
            if (!root.assetVisibleFn(asset.kind))
                continue
            if (q.length > 0 && asset.name.toLowerCase().indexOf(q) < 0)
                continue
            items.push({
                isFolder: false,
                folderId: "",
                assetId: asset.id,
                name: asset.name,
                assetIndex: asset.assetIndex,
                kind: asset.kind,
                duration: asset.duration,
                durationSeconds: asset.durationSeconds,
                path: asset.path,
                thumbnailPath: asset.thumbnailPath,
                filmstripPath: asset.filmstripPath
            })
        }

        return items
    }

    // One delegate type for both folder and asset rows — not a DelegateChooser. A
    // DelegateChooser turned out to change enough about how the chosen delegate is parented
    // that it broke the drag: the asset card's Drag.active binding started detecting a binding
    // loop the instant a drag began, and the drag died before DragHandler.active ever went
    // true. Keeping one concrete item type, with the folder/asset visuals as sibling blocks
    // toggled by `isFolder`, keeps the exact Column/Rectangle/DragHandler nesting that already
    // works for the timeline drop.
    Component {
        id: gridDelegate
        Column {
            id: cardRoot
            width: Theme.assetCardWidth
            spacing: 4

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

            readonly property bool selected: !isFolder && root.isAssetSelected(assetId)

            // Lift on grab: dims and grows slightly, so the card reads as
            // picked up rather than just sitting there while a ghost moves.
            opacity: assetDrag.active ? 0.85 : 1
            scale: assetDrag.active ? 1.04 : 1.0

            Behavior on opacity {
                NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
            }
            Behavior on scale {
                NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
            }

            Drag.active: !isFolder && assetDrag.active
            Drag.dragType: Drag.Automatic
            Drag.supportedActions: Qt.CopyAction
            Drag.keys: ["text/plain"]
            Drag.mimeData: { "text/plain": assetIndex.toString() }
            // Default hotspot is (0,0) at the card top-left; Wayland
            // compositors (notably Mutter) then deliver drop Y far from
            // the grab point. Center on the thumbnail like effect cards.
            Drag.hotSpot.x: width / 2
            Drag.hotSpot.y: Theme.assetCardWidth * 9 / 32

            // Grid cards had neither hover feedback nor a pointing
            // cursor, while the list rows had both.
            HoverHandler {
                id: cardHover
                cursorShape: cardRoot.isFolder ? Qt.PointingHandCursor
                             : (assetDrag.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor)
            }

            ThemedToolTip {
                text: cardRoot.isFolder ? name
                      : qsTr("%1 — drag to the timeline, right-click to preview").arg(name)
                visible: cardHover.hovered
                // Explicit rather than the default above-parent Popup placement: for the top
                // row, that placement had no room to spare and overlapped the folder row/search
                // field above the grid.
                y: parent.height + 4
            }

            Rectangle {
                width: Theme.assetCardWidth
                height: Theme.assetCardWidth * 9 / 16
                radius: Theme.radiusSm
                color: Theme.panelAccent
                clip: true
                // Hover ring only here; the selection ring is a separate overlay
                // (below) so it always paints on top of the thumbnail image.
                border.width: (!cardRoot.selected && cardHover.hovered) ? Theme.borderWidth : 0
                border.color: Theme.primary
                scale: (!cardRoot.isFolder && cardHover.hovered) ? 1.03 : 1.0

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
                    visible: cardRoot.isFolder
                    anchors.centerIn: parent
                    glyph: Theme.icons.folder
                    iconSize: Theme.spacing3xl
                    iconColor: Theme.mutedForeground
                }

                // Placeholder while the thumbnail decodes. The
                // card used to sit empty, indistinguishable from
                // a failed load.
                SkeletonBox {
                    anchors.fill: parent
                    radius: parent.radius
                    visible: !cardRoot.isFolder && thumbnailPath.length > 0
                                && gridThumb.status === Image.Loading
                }

                Image {
                    id: gridThumb
                    anchors.fill: parent
                    visible: !cardRoot.isFolder && thumbnailPath.length > 0 && status === Image.Ready
                    source: !cardRoot.isFolder && thumbnailPath.length > 0 ? EditorState.imageUrl(thumbnailPath) : ""
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
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
                    visible: !cardRoot.isFolder
                             && (thumbnailPath.length === 0 || gridThumb.status === Image.Error)
                    glyph: kind === "audio" ? Theme.icons.music
                            : kind === "image" ? Theme.icons.image
                            : Theme.icons.film
                    iconSize: Theme.spacing3xl
                    iconColor: Theme.mutedForeground
                }

                // Reading the replacement off disk is the one wait in the swap with
                // nothing on screen to show for it. Scrims this card only, so the rest of
                // the bin stays usable.
                Rectangle {
                    id: gridBusy
                    // Above the duration badge, which is a later sibling.
                    z: 1
                    anchors.fill: parent
                    radius: parent.radius
                    color: Theme.scrimStrong
                    readonly property bool busy:
                        !cardRoot.isFolder && EditorState.replacingAssetId.length > 0
                        && EditorState.replacingAssetId === AssetLibrary.assetIdAt(assetIndex)
                    visible: opacity > 0
                    opacity: busy ? 1 : 0

                    Behavior on opacity {
                        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                    }

                    // Swallows taps and drags while the swap is in flight, so the card
                    // cannot be added to the timeline against media that is changing.
                    MouseArea {
                        anchors.fill: parent
                        enabled: gridBusy.busy
                        acceptedButtons: Qt.AllButtons
                    }

                    CircularProgress {
                        anchors.centerIn: parent
                        indeterminate: true
                        size: Theme.spacing3xl
                        progressColor: Theme.onMedia
                    }
                }

                Rectangle {
                    visible: !cardRoot.isFolder && duration.length > 0
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: Theme.spacingSm
                    color: Theme.scrimStrong
                    radius: Theme.radiusXs
                    width: durationLabel.implicitWidth + Theme.spacingLg
                    height: durationLabel.implicitHeight + Theme.spacingSm
                    Text {
                        id: durationLabel
                        anchors.centerIn: parent
                        text: duration
                        color: Theme.onMedia
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                }

                // A dedicated overlay for the selection ring, painted above the
                // thumbnail image and busy scrim (both earlier siblings) so it
                // is never covered by them. Square corners on purpose — a
                // rounded ring here reads chunkier than the thumbnail's own
                // radius at this border width.
                Rectangle {
                    z: 2
                    anchors.fill: parent
                    radius: 0
                    color: "transparent"
                    border.width: cardRoot.selected ? Theme.clipSelectionRingWidth : 0
                    border.color: Theme.primary

                    Behavior on border.width {
                        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                    }
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
                    // Touch keeps press-and-hold for the lift below — the menu
                    // is a tap there.
                    enabled: !Theme.touchUi && !assetDrag.active
                    onTapped: {
                        if (cardRoot.isFolder)
                            return

                        // The most recently interacted editing surface owns Delete.
                        root.forceActiveFocus()

                        // Qt remaps ControlModifier to Cmd on macOS, so this is already the
                        // right "system key" on every desktop platform without branching.
                        const mods = leftTap.point.modifiers
                        if ((mods & Qt.ShiftModifier) !== 0) {
                            root.selectAssetRange(root.selectionAnchorId || cardRoot.assetId, cardRoot.assetId)
                        } else if ((mods & Qt.ControlModifier) !== 0) {
                            root.toggleAssetSelection(cardRoot.assetId)
                            root.selectionAnchorId = cardRoot.assetId
                        } else {
                            root.selectOnlyAsset(cardRoot.assetId)
                            root.selectionAnchorId = cardRoot.assetId
                        }
                    }
                    onDoubleTapped: if (cardRoot.isFolder) EditorState.currentBinFolderId = cardRoot.folderId
                    onLongPressed: cardRoot.isFolder ? folderMenu.popup() : cardMenu.popup()
                }
                DragHandler {
                    id: assetDrag
                    // Without target: null the handler moves the card itself,
                    // clobbering the Grid positioner's x/y.
                    target: null
                    // Touch lifts through TouchDrag instead: a platform drag has
                    // no touch gesture and cannot leave the sheet. Folders aren't
                    // draggable at all — moving an asset into one is a context-menu
                    // action only ("Move to folder…"), not a drag-and-drop target.
                    enabled: !Theme.touchUi && !cardRoot.isFolder
                    acceptedButtons: Qt.LeftButton
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
                        if (cardRoot.isFolder) {
                            folderMenu.popup()
                            return
                        }
                        // Right-clicking a card that's part of the current multi-selection
                        // extends the bulk menu items to all of it; right-clicking anything
                        // else replaces the selection with just that card first — same
                        // "select if not already selected" rule the timeline's clip right-click
                        // already uses (TimelineClipItem.qml).
                        root.forceActiveFocus()
                        root.ensureAssetSelected(cardRoot.assetId)
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
                    dragKind: cardRoot.isFolder ? "" : "media"
                    payload: cardRoot.isFolder ? cardRoot.folderId : assetIndex
                    label: name
                    thumbnail: thumbnailPath
                    glyph: kind === "audio" ? Theme.icons.music
                            : kind === "image" ? Theme.icons.image
                            : Theme.icons.film
                    onLiftTapped: {
                        if (cardRoot.isFolder) {
                            folderMenu.popup()
                            return
                        }
                        // Touch has no modifier-key multi-select, so a lift-tap always means
                        // "just this card" unless it's already part of a selection built some
                        // other way (there isn't one yet, but this keeps the same rule as the
                        // desktop right-click rather than special-casing touch).
                        root.ensureAssetSelected(cardRoot.assetId)
                        cardMenu.popup()
                    }
                }

                ThemedContextMenu {
                    id: cardMenu

                    ThemedMenuItem {
                        text: root.selectedAssetIds.length > 1
                              ? qsTr("Add %n items to timeline", "", root.selectedAssetIds.length)
                              : qsTr("Add to timeline")
                        icon.name: Theme.icons.plus
                        onTriggered: root.addToTimelineRequested(root.selectedAssetIds)
                    }
                    ThemedMenuItem {
                        text: qsTr("Preview and edit…")
                        icon.name: Theme.icons.eye
                        onTriggered: root.previewRequested(assetIndex)
                    }
                    ThemedMenuItem {
                        text: qsTr("Rename…")
                        icon.name: Theme.icons.pencil
                        visible: root.selectedAssetIds.length <= 1
                        onTriggered: root.renameRequested(assetIndex)
                    }
                    ThemedMenuItem {
                        text: qsTr("Replace media…")
                        icon.name: Theme.icons.refresh
                        visible: root.selectedAssetIds.length <= 1
                        onTriggered: root.replaceRequested(assetIndex)
                    }
                    ThemedMenuItem {
                        // "the selection" once it covers more than the clicked card — matches
                        // the count the bulk actions below will actually act on.
                        text: root.selectedAssetIds.length > 1
                              ? qsTr("Move %n items to folder…", "", root.selectedAssetIds.length)
                              : qsTr("Move to folder…")
                        icon.name: Theme.icons.folder
                        visible: BinFolderModel.count > 0
                        onTriggered: root.moveToFolderRequested(root.selectedAssetIds)
                    }
                    ThemedMenuItem {
                        text: qsTr("Export image…")
                        icon.name: Theme.icons.save
                        visible: kind === "image"
                        onTriggered: root.exportRequested(assetIndex)
                    }
                    ThemedMenuItem {
                        text: root.selectedAssetIds.length > 1
                              ? qsTr("Remove %n items from project", "", root.selectedAssetIds.length)
                              : qsTr("Remove from project")
                        icon.name: Theme.icons.trash
                        onTriggered: root.removeRequested(root.selectedAssetIds)
                    }
                }

                ThemedContextMenu {
                    id: folderMenu

                    ThemedMenuItem {
                        text: qsTr("Open")
                        icon.name: Theme.icons.folder
                        onTriggered: EditorState.currentBinFolderId = cardRoot.folderId
                    }
                    ThemedMenuItem {
                        text: qsTr("Rename…")
                        icon.name: Theme.icons.pencil
                        onTriggered: root.folderRenameRequested(cardRoot.folderId, cardRoot.name)
                    }
                    ThemedMenuItem {
                        text: qsTr("Move to folder…")
                        icon.name: Theme.icons.folder
                        onTriggered: root.folderMoveRequested(cardRoot.folderId)
                    }
                    ThemedMenuSeparator { }
                    ThemedMenuItem {
                        text: qsTr("Delete")
                        icon.name: Theme.icons.trash
                        onTriggered: EditorState.deleteBinFolder(cardRoot.folderId)
                    }
                }
            }

            Text {
                width: parent.width
                text: name
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeCard
                elide: Text.ElideRight
            }
        }
    }

    Component {
        id: listDelegate
        Rectangle {
            id: listRow
            width: ListView.view ? ListView.view.width : 0
            height: 48
            radius: Theme.radiusSm
            color: rowHover.hovered ? Theme.popoverHover : Theme.panelAccent
            // Matches the grid card's selection ring.
            border.width: listRow.selected ? Theme.clipSelectionRingWidth : 0
            border.color: Theme.primary
            opacity: rowDrag.active ? 0.85 : 1
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
            readonly property bool replaceBusy:
                !isFolder && EditorState.replacingAssetId.length > 0
                && EditorState.replacingAssetId === AssetLibrary.assetIdAt(assetIndex)
            readonly property bool selected: !isFolder && root.isAssetSelected(assetId)

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
                spacing: Theme.spacingLg + Theme.spacingXs

                Rectangle {
                    id: listThumbFrame
                    width: 56
                    height: 32
                    radius: Theme.radiusSm
                    color: Theme.panelBackground
                    clip: true

                    SkeletonBox {
                        anchors.fill: parent
                        radius: parent.radius
                        visible: !listRow.isFolder && thumbnailPath.length > 0
                                    && listThumb.status === Image.Loading
                    }

                    Image {
                        id: listThumb
                        anchors.fill: parent
                        visible: !listRow.isFolder && thumbnailPath.length > 0 && status === Image.Ready
                        source: !listRow.isFolder && thumbnailPath.length > 0 ? EditorState.imageUrl(thumbnailPath) : ""
                        fillMode: Image.PreserveAspectFit
                        // Was missing, so list thumbnails decoded
                        // on the UI thread and stalled scrolling.
                        asynchronous: true
                    }

                    IconGlyph {
                        anchors.centerIn: parent
                        visible: listRow.isFolder
                                    || thumbnailPath.length === 0 || listThumb.status === Image.Error
                        glyph: listRow.isFolder ? Theme.icons.folder
                               : (kind === "audio" ? Theme.icons.music : Theme.icons.film)
                        iconSize: Theme.iconSizeBase
                        iconColor: Theme.mutedForeground
                    }

                    // Same busy treatment as the grid card, scaled to the row thumbnail.
                    Rectangle {
                        anchors.fill: parent
                        radius: parent.radius
                        color: Theme.scrimStrong
                        visible: opacity > 0
                        opacity: replaceBusy ? 1 : 0

                        Behavior on opacity {
                            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                        }

                        CircularProgress {
                            anchors.centerIn: parent
                            indeterminate: true
                            size: Theme.iconSizeBase
                            strokeWidth: 2
                            progressColor: Theme.onMedia
                        }
                    }
                }

                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    // Derived from the actual thumbnail width
                    // rather than a magic constant.
                    width: parent.width - listThumbFrame.width - listRowContent.spacing
                    Text {
                        text: name
                        color: Theme.panelForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSm
                        elide: Text.ElideRight
                        width: parent.width
                    }
                    Text {
                        visible: !listRow.isFolder
                        text: kind + (duration.length > 0 ? " · " + duration : "")
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                        // Was unbounded, so it overflowed the row
                        // at narrow panel widths.
                        elide: Text.ElideRight
                        width: parent.width
                    }
                }
            }

            ThemedToolTip {
                text: listRow.isFolder ? name
                      : qsTr("%1 — drag to the timeline, right-click to preview").arg(name)
                visible: rowHover.hovered
                y: parent.height + 4
            }

            Item {
                anchors.fill: parent
                enabled: !replaceBusy

                TapHandler {
                    id: leftRowTap
                    acceptedButtons: Qt.LeftButton
                    enabled: !Theme.touchUi && !rowDrag.active
                    onTapped: {
                        if (listRow.isFolder)
                            return

                        // Keep keyboard actions scoped to this surface.
                        root.forceActiveFocus()

                        const mods = leftRowTap.point.modifiers
                        if ((mods & Qt.ShiftModifier) !== 0) {
                            root.selectAssetRange(root.selectionAnchorId || listRow.assetId, listRow.assetId)
                        } else if ((mods & Qt.ControlModifier) !== 0) {
                            root.toggleAssetSelection(listRow.assetId)
                            root.selectionAnchorId = listRow.assetId
                        } else {
                            root.selectOnlyAsset(listRow.assetId)
                            root.selectionAnchorId = listRow.assetId
                        }
                    }
                    onDoubleTapped: if (listRow.isFolder) EditorState.currentBinFolderId = listRow.folderId
                    onLongPressed: listRow.isFolder ? folderRowMenu.popup() : rowMenu.popup()
                }
                DragHandler {
                    id: rowDrag
                    target: null
                    enabled: !Theme.touchUi && !listRow.isFolder
                    acceptedButtons: Qt.LeftButton
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
                        if (listRow.isFolder) {
                            folderRowMenu.popup()
                            return
                        }
                        root.forceActiveFocus()
                        root.ensureAssetSelected(listRow.assetId)
                        rowMenu.popup()
                    }
                }

                // See the grid card: hold lifts, tap opens the menu — also a folder row's
                // only touch surface, so it stays enabled there too (empty dragKind so a
                // stray press-and-hold doesn't start a lift no drop target recognizes).
                TouchLiftArea {
                    dragKind: listRow.isFolder ? "" : "media"
                    payload: listRow.isFolder ? listRow.folderId : assetIndex
                    label: name
                    thumbnail: thumbnailPath
                    glyph: kind === "audio" ? Theme.icons.music
                            : kind === "image" ? Theme.icons.image
                            : Theme.icons.film
                    onLiftTapped: {
                        if (listRow.isFolder) {
                            folderRowMenu.popup()
                            return
                        }
                        root.ensureAssetSelected(listRow.assetId)
                        rowMenu.popup()
                    }
                }
            }

            ThemedContextMenu {
                id: rowMenu

                ThemedMenuItem {
                    text: root.selectedAssetIds.length > 1
                          ? qsTr("Add %n items to timeline", "", root.selectedAssetIds.length)
                          : qsTr("Add to timeline")
                    icon.name: Theme.icons.plus
                    onTriggered: root.addToTimelineRequested(root.selectedAssetIds)
                }
                ThemedMenuItem {
                    text: qsTr("Preview and edit…")
                    icon.name: Theme.icons.eye
                    onTriggered: root.previewRequested(assetIndex)
                }
                ThemedMenuItem {
                    text: qsTr("Rename…")
                    icon.name: Theme.icons.pencil
                    visible: root.selectedAssetIds.length <= 1
                    onTriggered: root.renameRequested(assetIndex)
                }
                ThemedMenuItem {
                    text: qsTr("Replace media…")
                    icon.name: Theme.icons.refresh
                    visible: root.selectedAssetIds.length <= 1
                    onTriggered: root.replaceRequested(assetIndex)
                }
                ThemedMenuItem {
                    text: root.selectedAssetIds.length > 1
                          ? qsTr("Move %n items to folder…", "", root.selectedAssetIds.length)
                          : qsTr("Move to folder…")
                    icon.name: Theme.icons.folder
                    visible: BinFolderModel.count > 0
                    onTriggered: root.moveToFolderRequested(root.selectedAssetIds)
                }
                ThemedMenuItem {
                    text: qsTr("Export image…")
                    icon.name: Theme.icons.save
                    visible: kind === "image"
                    onTriggered: root.exportRequested(assetIndex)
                }
                ThemedMenuItem {
                    text: root.selectedAssetIds.length > 1
                          ? qsTr("Remove %n items from project", "", root.selectedAssetIds.length)
                          : qsTr("Remove from project")
                    icon.name: Theme.icons.trash
                    onTriggered: root.removeRequested(root.selectedAssetIds)
                }
            }

            ThemedContextMenu {
                id: folderRowMenu

                ThemedMenuItem {
                    text: qsTr("Open")
                    icon.name: Theme.icons.folder
                    onTriggered: EditorState.currentBinFolderId = listRow.folderId
                }
                ThemedMenuItem {
                    text: qsTr("Rename…")
                    icon.name: Theme.icons.pencil
                    onTriggered: root.folderRenameRequested(listRow.folderId, listRow.name)
                }
                ThemedMenuItem {
                    text: qsTr("Move to folder…")
                    icon.name: Theme.icons.folder
                    onTriggered: root.folderMoveRequested(listRow.folderId)
                }
                ThemedMenuSeparator { }
                ThemedMenuItem {
                    text: qsTr("Delete")
                    icon.name: Theme.icons.trash
                    onTriggered: EditorState.deleteBinFolder(listRow.folderId)
                }
            }
        }
    }

    // First-run screen for a project with no media at all — folders included. This area used
    // to render as a blank rectangle, with no hint that the panel accepts drops or that an
    // Import button exists.
    EmptyState {
        width: parent.width
        height: parent.height
        visible: AssetLibrary.count === 0 && BinFolderModel.count === 0 && !root.importing
        glyph: Theme.icons.film
        title: qsTr("No media yet")
        hint: qsTr("Import video, audio or images, then drag them onto the timeline. Right-click a clip to preview and trim it first.")
        actionText: qsTr("Import media")
        actionVariant: "primary"
        onActionTriggered: root.importRequested()
    }

    BinBreadcrumb {
        id: breadcrumb
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.pagePadding
        anchors.bottomMargin: 0
        currentFolderId: EditorState.currentBinFolderId
        onNavigate: (folderId) => EditorState.currentBinFolderId = folderId
    }

    ThemedTextField {
        id: search
        anchors.top: breadcrumb.visible ? breadcrumb.bottom : parent.top
        anchors.topMargin: breadcrumb.visible ? Theme.spacingSm : 0
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.pagePadding
        visible: AssetLibrary.count > 0 || BinFolderModel.count > 0
        placeholderText: qsTr("Search media")
        font.family: Theme.fontFamily
    }

    // Search matched nothing in this folder.
    EmptyState {
        anchors.centerIn: parent
        width: parent.width
        visible: root.query.length > 0 && root.combinedItems.length === 0
        compact: true
        glyph: Theme.icons.search
        title: qsTr("No media match “%1”").arg(search.text.trim())
        hint: qsTr("Try a different name.")
    }

    // This folder (or root, once other folders exist) has nothing in it, but the project
    // isn't empty — distinct from the "No media yet" first-run state above.
    EmptyState {
        anchors.centerIn: parent
        width: parent.width
        visible: root.query.length === 0 && root.combinedItems.length === 0
                 && (AssetLibrary.count > 0 || BinFolderModel.count > 0)
        compact: true
        glyph: Theme.icons.folder
        title: qsTr("This folder is empty")
        hint: qsTr("Drag media here, or import more.")
    }

    GridView {
        id: grid
        visible: root.gridMode && root.combinedItems.length > 0

        anchors.top: search.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom

        anchors.margins: Theme.pagePadding
        anchors.topMargin: Theme.spacingMd
        anchors.rightMargin: Theme.pagePadding - Theme.assetCardGap

        cellWidth: Theme.assetCardWidth + Theme.assetCardGap
        cellHeight: (Theme.assetCardWidth * 9 / 16) + Theme.spacing3xl + Theme.assetCardGap

        clip: true
        ScrollBar.vertical: AppScrollBar { }

        model: root.combinedItems
        delegate: gridDelegate
    }

    ListView {
        id: listColumn
        visible: !root.gridMode && root.combinedItems.length > 0

        anchors.top: search.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom

        anchors.margins: Theme.pagePadding
        anchors.topMargin: Theme.spacingMd

        spacing: Theme.spacingMd

        clip: true
        ScrollBar.vertical: AppScrollBar { }

        model: root.combinedItems
        delegate: listDelegate
    }
}
