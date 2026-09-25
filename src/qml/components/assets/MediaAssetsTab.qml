import QtQuick
import QtQuick.Window
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

    // "grid" | "list" | "tree", owned by EditorState so it saves with the project.
    readonly property string viewMode: EditorState.mediaViewMode
    readonly property bool gridMode: viewMode === "grid"
    readonly property bool treeMode: viewMode === "tree"
    // Sorting is a bin-wide reorder of AssetLibrary itself, so this only tracks
    // which of the two orders the toolbar applied last.
    property bool sortByKind: false

    // Folder ids currently open in the tree. Pure view state: not saved with the
    // project and not undoable, so it is a plain map rather than a model column.
    property var expandedFolderIds: ({})

    function toggleFolderExpanded(folderId) {
        const next = Object.assign({}, root.expandedFolderIds)
        if (next[folderId] === true)
            delete next[folderId]
        else
            next[folderId] = true
        root.expandedFolderIds = next
    }

    // Expands every folder in the bin, not just the ones under the tree's current
    // root — ids outside it cost nothing and survive navigating up.
    function expandAllFolders() {
        const next = {}
        for (let i = 0; i < BinFolderModel.count; ++i)
            next[BinFolderModel.folderAt(i).id] = true
        root.expandedFolderIds = next
    }

    function collapseAllFolders() {
        root.expandedFolderIds = ({})
    }
    // True while an import is running, so the empty state can step aside.
    property bool importing: false
    // Kind visibility filter supplied by the parent (depends on the active tab).
    property var assetVisibleFn: function(kind) { return true }
    property alias searchText: search.text
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
    // Emitted when the empty-state action asks to import a whole folder. The parent owns the
    // directory picker and the tree walk.
    signal importFolderRequested()
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

    // What a badge binding names so it re-reads: the AssetLibrary badge queries are function calls
    // QML can't track, and proxy "ready" also depends on the proxy size setting.
    readonly property int _badgeRevision: AssetLibrary.badgeRevision + EditorState.playback.proxySize

    // "none", "queued", "building", "ready", or "" for rows a proxy can't apply to.
    function proxyStateOf(assetId) {
        void root._badgeRevision
        return AssetLibrary.proxyState(assetId)
    }
    function editFriendlyOf(assetId) {
        void root._badgeRevision
        return AssetLibrary.isEditFriendly(assetId)
    }
    // Converted media is constant-rate by construction, so the warning never sits beside the pill.
    function vfrWarningOf(assetId) {
        void root._badgeRevision
        return !AssetLibrary.isEditFriendly(assetId) && AssetLibrary.isVariableFrameRate(assetId)
    }

    // The selected assets whose proxy state is one of `states`.
    function selectedWithProxyState(states) {
        void root._badgeRevision
        const ids = []
        for (let i = 0; i < root.selectedAssetIds.length; ++i) {
            const id = root.selectedAssetIds[i]
            if (states.indexOf(AssetLibrary.proxyState(id)) >= 0)
                ids.push(id)
        }
        return ids
    }

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
    onCombinedItemsChanged: root.pruneSelection()
    function pruneSelection() {
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
    // Set instead of restarting the timer while a preview window has a metadata edit in flight
    // (rotate, trim) — nothing behind that modal-ish window needs to reflect the change until it
    // closes, and rebuilding while it's open is a visible, pointless flicker for the user.
    property bool _refreshPending: false
    Timer {
        id: refreshCoalesceTimer
        interval: 100
        onTriggered: {
            // Reassigning a freshly-built array to `model:` below is exactly the "different
            // model, not an in-place update" case GridView/ListView cannot tell apart from a
            // real reset, so it snaps contentY back to the top on every coalesced rebuild —
            // disruptive when a metadata edit (rotate, trim, a probe landing) fires while the
            // user is scrolled down. Save/restore across it instead of trying to avoid the
            // rebuild; the restore is deferred a couple of turns so it runs after the view has
            // actually relaid out its delegates against the new model.
            const gridY = grid.contentY
            const listY = listColumn.contentY
            root._refreshTick++
            Qt.callLater(() => Qt.callLater(() => {
                grid.contentY = Math.min(gridY, Math.max(0, grid.contentHeight - grid.height))
                listColumn.contentY =
                    Math.min(listY, Math.max(0, listColumn.contentHeight - listColumn.height))
            }))
        }
    }
    function _requestRefresh() {
        if (EditorState.assetPreviewWindowOpen)
            root._refreshPending = true
        else
            refreshCoalesceTimer.restart()
    }
    Connections {
        target: EditorState
        function onUndoStackChanged() { root._requestRefresh() }
        function onAssetPreviewWindowOpenChanged() {
            if (!EditorState.assetPreviewWindowOpen && root._refreshPending) {
                root._refreshPending = false
                refreshCoalesceTimer.restart()
            }
        }
    }
    Connections {
        target: AssetLibrary
        // Only a card-level field (name/kind/duration/path) warrants rebuilding combinedItems;
        // a thumbnail-only change (rotate, a thumbnail regenerating) refreshes its own delegate
        // directly instead (see cardRoot/listRow's own Connections below) with no rebuild at all.
        function onAssetCardChanged(assetId) { root._requestRefresh() }
    }

    // Tree view's flattened rows: the current folder's contents, with each open folder's
    // own contents spliced in right after it. `folderCount`/`assetCount` are handed in
    // rather than read here so the binding below registers a dependency on them — a
    // function call gives it nothing to re-evaluate on by itself.
    function _buildTree(rootFolderId, q, expanded, folderCount, assetCount) {
        const items = []

        // One pass over each model instead of a rescan per level.
        const childFolders = {}
        for (let j = 0; j < folderCount; ++j) {
            const folder = BinFolderModel.folderAt(j)
            if (!childFolders[folder.parentId])
                childFolders[folder.parentId] = []
            childFolders[folder.parentId].push(folder)
        }
        const childAssets = {}
        for (let i = 0; i < assetCount; ++i) {
            const asset = AssetLibrary.assetAt(i)
            if (!root.assetVisibleFn(asset.kind))
                continue
            if (!childAssets[asset.folderId])
                childAssets[asset.folderId] = []
            childAssets[asset.folderId].push(asset)
        }

        // A folder stays in a search result when anything below it matches; otherwise a
        // hit three levels down would have no visible path leading to it.
        const subtreeHit = {}
        function subtreeMatches(folderId, seen) {
            if (subtreeHit[folderId] !== undefined)
                return subtreeHit[folderId]
            // Project deserialization doesn't reject a cycle in parentId, so guard the
            // walk the same way BinBreadcrumb.qml guards its trail.
            if (seen.has(folderId))
                return false
            seen.add(folderId)
            let hit = false
            const assets = childAssets[folderId] || []
            for (let i = 0; i < assets.length && !hit; ++i)
                hit = assets[i].name.toLowerCase().indexOf(q) >= 0
            const folders = childFolders[folderId] || []
            for (let j = 0; j < folders.length && !hit; ++j)
                hit = folders[j].name.toLowerCase().indexOf(q) >= 0
                      || subtreeMatches(folders[j].id, seen)
            subtreeHit[folderId] = hit
            return hit
        }

        function walk(parentId, depth, seen) {
            if (seen.has(parentId))
                return
            seen.add(parentId)

            const folders = childFolders[parentId] || []
            for (const folder of folders) {
                const deepHit = q.length > 0 && subtreeMatches(folder.id, new Set())
                if (q.length > 0 && folder.name.toLowerCase().indexOf(q) < 0 && !deepHit)
                    continue
                const hasChildren = (childFolders[folder.id] || []).length > 0
                                    || (childAssets[folder.id] || []).length > 0
                // While searching, every folder on the path to a hit opens itself; the
                // user's own expansion state is restored when the query clears.
                const open = hasChildren && (q.length > 0 ? deepHit : expanded[folder.id] === true)
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
                    filmstripPath: "",
                    depth: depth,
                    expanded: open,
                    expandable: hasChildren
                })
                if (open)
                    walk(folder.id, depth + 1, seen)
            }

            const assets = childAssets[parentId] || []
            for (const asset of assets) {
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
                    filmstripPath: asset.filmstripPath,
                    depth: depth,
                    expanded: false,
                    expandable: false
                })
            }
        }

        walk(rootFolderId, 0, new Set())
        return items
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

        if (root.treeMode)
            return root._buildTree(currentFolder, q, root.expandedFolderIds,
                                   BinFolderModel.count, AssetLibrary.count)

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
                filmstripPath: "",
                depth: 0,
                expanded: false,
                expandable: false
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
                filmstripPath: asset.filmstripPath,
                depth: 0,
                expanded: false,
                expandable: false
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
            width: grid.cardSize
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

            // Bound to the model snapshot by default, but re-readable in place: a rotate/trim
            // edit only ever changes this one card's thumbnail, so refreshing it directly here
            // avoids MediaAssetsTab's full combinedItems rebuild (and the scroll-position
            // flicker that comes with reassigning a GridView's whole model) for what is really a
            // single-image update.
            property string _liveThumbnailPath: thumbnailPath
            // The Connections below replaces the binding, so a recycled card has to get it back.
            GridView.onReused: cardRoot._liveThumbnailPath = Qt.binding(() => cardRoot.thumbnailPath)
            GridView.onPooled: {
                cardMenu.close()
                folderMenu.close()
            }
            Connections {
                target: AssetLibrary
                function onAssetMetadataChanged(assetId) {
                    if (!cardRoot.isFolder && assetId === cardRoot.assetId)
                        cardRoot._liveThumbnailPath = AssetLibrary.thumbnailAt(cardRoot.assetIndex)
                }
            }

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
            Drag.hotSpot.y: grid.cardSize * 9 / 32

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
                width: grid.cardSize
                height: grid.cardSize * 9 / 16
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
                    visible: !cardRoot.isFolder && cardRoot._liveThumbnailPath.length > 0
                                && gridThumb.status === Image.Loading
                }

                Image {
                    id: gridThumb
                    anchors.fill: parent
                    visible: !cardRoot.isFolder && cardRoot._liveThumbnailPath.length > 0 && status === Image.Ready
                    source: !cardRoot.isFolder && cardRoot._liveThumbnailPath.length > 0
                            ? EditorState.imageUrl(cardRoot._liveThumbnailPath) : ""
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    sourceSize: Qt.size(Math.ceil(grid.cardSize * Screen.devicePixelRatio),
                                         Math.ceil(grid.cardSize * 9 / 16 * Screen.devicePixelRatio))
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
                             && (cardRoot._liveThumbnailPath.length === 0 || gridThumb.status === Image.Error)
                    glyph: kind === "audio" ? Theme.icons.music
                            : kind === "image" ? Theme.icons.image
                            : kind === "vector" ? Theme.icons.layers
                            : kind === "model3d" ? Theme.icons.box
                            : kind === "composite" ? Theme.icons.layers
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

                VfrWarning {
                    z: 3
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.spacingSm
                    visible: !cardRoot.isFolder && kind === "video" && root.vfrWarningOf(cardRoot.assetId)
                }

                // Proxy build progress along the thumbnail's bottom edge; the pill beside the
                // name below says which state it is in.
                ThemedProgressBar {
                    z: 1
                    visible: !cardRoot.isFolder && AssetLibrary.proxyBusy
                             && root._badgeRevision >= 0
                             && AssetLibrary.proxyState(cardRoot.assetId) === "building"
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    barHeight: Theme.spacingXs
                    value: AssetLibrary.proxyProgress
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
                    onDoubleTapped: {
                        if (cardRoot.isFolder)
                            EditorState.currentBinFolderId = cardRoot.folderId
                        else if (kind === "composite")
                            EditorState.openCompositeAsset(cardRoot.assetId)
                    }
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
                            : kind === "vector" ? Theme.icons.layers
                            : kind === "model3d" ? Theme.icons.box
                            : kind === "composite" ? Theme.icons.layers
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
                        text: qsTr("Open composite")
                        icon.name: Theme.icons.layers
                        visible: kind === "composite" && root.selectedAssetIds.length <= 1
                        onTriggered: EditorState.openCompositeAsset(assetId)
                    }
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
                        visible: kind !== "composite"
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
                        visible: kind !== "composite" && root.selectedAssetIds.length <= 1
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
                        readonly property var ids: root.selectedWithProxyState(["none"])
                        text: ids.length > 1 ? qsTr("Create %n proxies", "", ids.length) : qsTr("Create proxy")
                        icon.name: Theme.icons.filePlay
                        visible: ids.length > 0
                        onTriggered: AssetLibrary.createProxies(ids)
                    }
                    ThemedMenuItem {
                        readonly property var ids: root.selectedWithProxyState(["ready", "queued", "building"])
                        text: ids.length > 1 ? qsTr("Remove %n proxies", "", ids.length) : qsTr("Remove proxy")
                        icon.name: Theme.icons.x
                        visible: ids.length > 0
                        onTriggered: AssetLibrary.removeProxies(ids)
                    }
                    ThemedMenuItem {
                        text: qsTr("Convert to edit-friendly format")
                        icon.name: Theme.icons.rabbit
                        visible: kind === "video" && root.selectedAssetIds.length <= 1
                                 && !root.editFriendlyOf(assetId)
                        onTriggered: EditorState.convertAssetsToConstantFrameRate([assetId])
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

            Row {
                id: cardNameRow
                width: parent.width
                spacing: Theme.spacingSm
                readonly property string proxyState: cardRoot.isFolder ? ""
                                                                       : root.proxyStateOf(cardRoot.assetId)

                MediaPill {
                    id: cardProxyPill
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Proxy")
                    visible: cardNameRow.proxyState === "ready" || cardNameRow.proxyState === "queued"
                             || cardNameRow.proxyState === "building"
                    waiting: cardNameRow.proxyState !== "ready"
                    fontSize: Theme.fontSizeTiny
                }
                MediaPill {
                    id: cardEditFriendlyPill
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Edit-friendly")
                    tooltip: qsTr("Converted to a constant frame rate for smooth editing")
                    accent: Theme.clipEditFriendly
                    foreground: Theme.clipEditFriendlyForeground
                    visible: !cardRoot.isFolder && root.editFriendlyOf(cardRoot.assetId)
                    fontSize: Theme.fontSizeTiny
                }
                Text {
                    width: Math.max(0, parent.width
                                       - (cardProxyPill.visible ? cardProxyPill.width + parent.spacing : 0)
                                       - (cardEditFriendlyPill.visible
                                          ? cardEditFriendlyPill.width + parent.spacing : 0))
                    text: name
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeCard
                    elide: Text.ElideRight
                }
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
            required property int depth
            required property bool expanded
            required property bool expandable

            // Tree rows step in by depth, and every row leaves room for a disclosure
            // arrow so names still line up under folders that have none.
            readonly property real treeIndent: root.treeMode ? listRow.depth * Theme.spacing2xl : 0
            readonly property real disclosureSpace: root.treeMode ? Theme.iconButtonSize : 0

            readonly property bool replaceBusy:
                !isFolder && EditorState.replacingAssetId.length > 0
                && EditorState.replacingAssetId === AssetLibrary.assetIdAt(assetIndex)
            readonly property bool selected: !isFolder && root.isAssetSelected(assetId)

            // See the matching property on gridDelegate's cardRoot: lets a rotate/trim edit
            // refresh just this row's thumbnail without MediaAssetsTab rebuilding the whole list.
            property string _liveThumbnailPath: thumbnailPath
            ListView.onReused: listRow._liveThumbnailPath = Qt.binding(() => listRow.thumbnailPath)
            ListView.onPooled: {
                rowMenu.close()
                folderRowMenu.close()
            }
            Connections {
                target: AssetLibrary
                function onAssetMetadataChanged(assetId) {
                    if (!listRow.isFolder && assetId === listRow.assetId)
                        listRow._liveThumbnailPath = AssetLibrary.thumbnailAt(listRow.assetIndex)
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
                anchors.leftMargin: Theme.spacingLg + listRow.treeIndent + listRow.disclosureSpace
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
                        visible: !listRow.isFolder && listRow._liveThumbnailPath.length > 0
                                    && listThumb.status === Image.Loading
                    }

                    Image {
                        id: listThumb
                        anchors.fill: parent
                        visible: !listRow.isFolder && listRow._liveThumbnailPath.length > 0 && status === Image.Ready
                        source: !listRow.isFolder && listRow._liveThumbnailPath.length > 0
                                ? EditorState.imageUrl(listRow._liveThumbnailPath) : ""
                        fillMode: Image.PreserveAspectFit
                        // Was missing, so list thumbnails decoded
                        // on the UI thread and stalled scrolling.
                        asynchronous: true
                        sourceSize: Qt.size(Math.ceil(listThumbFrame.width * Screen.devicePixelRatio),
                                             Math.ceil(listThumbFrame.height * Screen.devicePixelRatio))
                    }

                    IconGlyph {
                        anchors.centerIn: parent
                        visible: listRow.isFolder
                                    || listRow._liveThumbnailPath.length === 0 || listThumb.status === Image.Error
                        glyph: listRow.isFolder ? Theme.icons.folder
                               : (kind === "audio" ? Theme.icons.music
                                  : kind === "vector" ? Theme.icons.layers
                                  : kind === "model3d" ? Theme.icons.box
                                  : kind === "composite" ? Theme.icons.layers : Theme.icons.film)
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

                    VfrWarning {
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 1
                        size: Theme.iconSizeSm
                        visible: !listRow.isFolder && kind === "video" && root.vfrWarningOf(assetId)
                    }
                }

                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    // Derived from the actual thumbnail width
                    // rather than a magic constant.
                    width: parent.width - listThumbFrame.width - listRowContent.spacing
                    Row {
                        id: rowNameRow
                        width: parent.width
                        spacing: Theme.spacingSm
                        readonly property string proxyState: listRow.isFolder ? ""
                                                                              : root.proxyStateOf(assetId)

                        MediaPill {
                            id: rowProxyPill
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Proxy")
                            visible: rowNameRow.proxyState === "ready"
                                     || rowNameRow.proxyState === "queued"
                                     || rowNameRow.proxyState === "building"
                            waiting: rowNameRow.proxyState !== "ready"
                        }
                        MediaPill {
                            id: rowEditFriendlyPill
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Edit-friendly")
                            tooltip: qsTr("Converted to a constant frame rate for smooth editing")
                            accent: Theme.clipEditFriendly
                            foreground: Theme.clipEditFriendlyForeground
                            visible: !listRow.isFolder && root.editFriendlyOf(assetId)
                        }
                        Text {
                            text: name
                            color: Theme.panelForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeSm
                            elide: Text.ElideRight
                            width: Math.max(0, parent.width
                                               - (rowProxyPill.visible ? rowProxyPill.width + parent.spacing : 0)
                                               - (rowEditFriendlyPill.visible
                                                  ? rowEditFriendlyPill.width + parent.spacing : 0))
                        }
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
                    onDoubleTapped: {
                    if (listRow.isFolder)
                        EditorState.currentBinFolderId = listRow.folderId
                    else if (kind === "composite")
                        EditorState.openCompositeAsset(listRow.assetId)
                }
                    onLongPressed: listRow.isFolder ? folderRowMenu.popup() : rowMenu.popup()
                }
                DragHandler {
                    id: rowDrag
                    target: null
                    enabled: !Theme.touchUi && !listRow.isFolder
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
                            : kind === "vector" ? Theme.icons.layers
                            : kind === "model3d" ? Theme.icons.box
                            : kind === "composite" ? Theme.icons.layers
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

            // Declared after the tap overlay above so it sits on top of it: the arrow has
            // to take the press itself, or the overlay's handlers would swallow it and the
            // row would only ever open on a double-click.
            IconButton {
                visible: root.treeMode && listRow.isFolder && listRow.expandable
                x: Theme.spacingLg + listRow.treeIndent
                anchors.verticalCenter: parent.verticalCenter
                buttonSize: Theme.iconButtonSize
                iconSize: Theme.iconSizeMd
                variant: "ghost"
                glyph: listRow.expanded ? Theme.icons.chevronDown : Theme.icons.chevronRight
                tooltip: listRow.expanded ? qsTr("Collapse folder") : qsTr("Expand folder")
                onClicked: root.toggleFolderExpanded(listRow.folderId)
            }

            ThemedContextMenu {
                id: rowMenu

                ThemedMenuItem {
                    text: qsTr("Open composite")
                    icon.name: Theme.icons.layers
                    visible: kind === "composite" && root.selectedAssetIds.length <= 1
                    onTriggered: EditorState.openCompositeAsset(assetId)
                }
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
                    visible: kind !== "composite"
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
                    visible: kind !== "composite" && root.selectedAssetIds.length <= 1
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
                    readonly property var ids: root.selectedWithProxyState(["none"])
                    text: ids.length > 1 ? qsTr("Create %n proxies", "", ids.length) : qsTr("Create proxy")
                    icon.name: Theme.icons.filePlay
                    visible: ids.length > 0
                    onTriggered: AssetLibrary.createProxies(ids)
                }
                ThemedMenuItem {
                    readonly property var ids: root.selectedWithProxyState(["ready", "queued", "building"])
                    text: ids.length > 1 ? qsTr("Remove %n proxies", "", ids.length) : qsTr("Remove proxy")
                    icon.name: Theme.icons.x
                    visible: ids.length > 0
                    onTriggered: AssetLibrary.removeProxies(ids)
                }
                ThemedMenuItem {
                    text: qsTr("Convert to edit-friendly format")
                    icon.name: Theme.icons.rabbit
                    visible: kind === "video" && root.selectedAssetIds.length <= 1
                             && !root.editFriendlyOf(assetId)
                    onTriggered: EditorState.convertAssetsToConstantFrameRate([assetId])
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
    // Import button exists. Hand-built rather than the shared EmptyState: it carries two
    // actions and the supported-format table, which no other empty state needs, and it
    // centres in the whole panel instead of sitting against the top edge.
    Item {
        id: mediaEmptyState
        anchors.fill: parent
        visible: AssetLibrary.count === 0 && BinFolderModel.count === 0 && !root.importing

        Column {
            anchors.centerIn: parent
            width: Math.min(parent.width - Theme.spacing3xl * 2, 300)
            spacing: Theme.spacingXl

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 48
                height: 48
                radius: Theme.radiusMd
                color: Theme.panelAccent

                IconGlyph {
                    anchors.centerIn: parent
                    glyph: Theme.icons.film
                    iconSize: Theme.iconSizeXl
                    iconColor: Theme.mutedForeground
                }
            }

            Text {
                width: parent.width
                text: qsTr("No media yet")
                color: Theme.panelForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                font.weight: Font.Medium
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }

            Text {
                width: parent.width
                // The formats table below now carries the kind list, so the hint only has to
                // say how media gets in and what to do with it once it is here.
                text: qsTr("Import files or drop them here, then drag them onto the timeline. Right-click a clip to preview and trim it first.")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Theme.spacingLg

                ThemedButton {
                    text: qsTr("Import media")
                    variant: "primary"
                    glyph: Theme.icons.upload
                    onClicked: root.importRequested()
                }

                ThemedButton {
                    text: qsTr("Import folder")
                    glyph: Theme.icons.folderInput
                    onClicked: root.importFolderRequested()
                }
            }

            Rectangle {
                width: parent.width
                height: formatColumn.height + Theme.spacingXl * 2
                radius: Theme.radiusMd
                color: "transparent"
                border.width: Theme.borderWidth
                border.color: Theme.panelBorder

                Column {
                    id: formatColumn
                    anchors.centerIn: parent
                    width: parent.width - Theme.spacingXl * 2
                    spacing: Theme.spacingMd

                    Text {
                        width: parent.width
                        text: qsTr("Supported formats")
                        color: Theme.mutedForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeTiny
                        font.weight: Font.Medium
                        font.capitalization: Font.AllUppercase
                        font.letterSpacing: 0.5
                    }

                    Repeater {
                        model: [
                            { glyph: Theme.icons.video, label: qsTr("Video"),
                              formats: "MP4 · MOV · MKV · WEBM · AVI" },
                            { glyph: Theme.icons.music, label: qsTr("Audio"),
                              formats: "MP3 · WAV · FLAC · AAC · OGG" },
                            { glyph: Theme.icons.image, label: qsTr("Images"),
                              formats: "PNG · JPG · WEBP · HEIC · GIF" },
                            { glyph: Theme.icons.shapes, label: qsTr("Vector"),
                              formats: "SVG · Lottie (.json, .lottie)" },
                            { glyph: Theme.icons.box, label: qsTr("3D"),
                              formats: "glTF binary (.glb)" }
                        ]

                        delegate: Row {
                            id: formatRow
                            required property var modelData

                            width: formatColumn.width
                            spacing: Theme.spacingMd

                            IconGlyph {
                                anchors.verticalCenter: parent.verticalCenter
                                glyph: formatRow.modelData.glyph
                                iconSize: Theme.iconSizeSm
                                iconColor: Theme.mutedForeground
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 48
                                text: formatRow.modelData.label
                                color: Theme.panelForeground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeXs
                                elide: Text.ElideRight
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                width: formatColumn.width - Theme.iconSizeSm - 48
                                       - Theme.spacingMd * 2
                                text: formatRow.modelData.formats
                                color: Theme.mutedForeground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeTiny
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }
        }
    }

    ThemedTextField {
        id: search
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.pagePadding
        // Shares its row with the view/sort toggles, which the panel header no
        // longer has room for alongside the import actions.
        anchors.rightMargin: Theme.pagePadding + viewControls.width + Theme.spacingMd
        visible: AssetLibrary.count > 0 || BinFolderModel.count > 0
        placeholderText: qsTr("Search media")
        font.family: Theme.fontFamily
    }

    Row {
        id: viewControls
        anchors.right: parent.right
        anchors.rightMargin: Theme.pagePadding
        anchors.verticalCenter: search.verticalCenter
        spacing: Theme.spacingSm
        visible: search.visible

        IconButton {
            glyph: Theme.icons.layoutGrid
            variant: "ghost"
            tooltip: qsTr("Grid view")
            active: root.gridMode
            onClicked: EditorState.mediaViewMode = "grid"
        }

        IconButton {
            glyph: Theme.icons.list
            variant: "ghost"
            tooltip: qsTr("List view")
            active: root.viewMode === "list"
            onClicked: EditorState.mediaViewMode = "list"
        }

        IconButton {
            glyph: Theme.icons.listTree
            variant: "ghost"
            tooltip: qsTr("Tree view")
            active: root.treeMode
            onClicked: EditorState.mediaViewMode = "tree"
        }

        IconButton {
            glyph: root.sortByKind ? Theme.icons.sortByKind : Theme.icons.sortByName
            variant: "ghost"
            tooltip: root.sortByKind ? qsTr("Sort by name") : qsTr("Sort by type")
            onClicked: {
                if (root.sortByKind)
                    AssetLibrary.sortByName()
                else
                    AssetLibrary.sortByKind()
                root.sortByKind = !root.sortByKind
            }
        }
    }

    // Where you are in the bin, and — in tree view — what the whole tree does at once.
    Item {
        id: binNavRow
        anchors.top: search.visible ? search.bottom : parent.top
        anchors.topMargin: visible ? Theme.spacingSm : 0
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.pagePadding
        anchors.rightMargin: Theme.pagePadding

        // Derived from the same conditions the two children use rather than from their
        // `visible`: Item.visible reads back the *effective* visibility, so keying the row
        // off a child's would have been a loop through the child's own parent.
        readonly property bool showBreadcrumb: BinFolderModel.count > 0
                                               && EditorState.currentBinFolderId !== ""
        readonly property bool showTreeActions: root.treeMode && BinFolderModel.count > 0

        visible: showBreadcrumb || showTreeActions
        // Collapses completely when empty so it doesn't push the items down for nothing.
        height: visible ? Math.max(showBreadcrumb ? breadcrumb.implicitHeight : 0,
                                   showTreeActions ? treeActions.implicitHeight : 0)
                        : 0

        BinBreadcrumb {
            id: breadcrumb
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            currentFolderId: EditorState.currentBinFolderId
            onNavigate: (folderId) => EditorState.currentBinFolderId = folderId
        }

        Row {
            id: treeActions
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingSm
            visible: binNavRow.showTreeActions

            IconButton {
                glyph: Theme.icons.listChevronsUpDown
                variant: "ghost"
                tooltip: qsTr("Expand all")
                onClicked: root.expandAllFolders()
            }

            IconButton {
                glyph: Theme.icons.listChevronsDownUp
                variant: "ghost"
                tooltip: qsTr("Collapse all")
                onClicked: root.collapseAllFolders()
            }
        }
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

    // Proxy jobs run in the background; this is their one progress surface and the way to stop
    // them.
    Item {
        id: proxyStatusRow
        anchors.top: binNavRow.bottom
        anchors.topMargin: visible ? Theme.spacingSm : 0
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.pagePadding
        anchors.rightMargin: Theme.pagePadding
        visible: AssetLibrary.proxyBusy
        height: visible ? Math.max(proxyStatusText.implicitHeight + Theme.spacingSm
                                   + proxyStatusBar.height, proxyCancel.height)
                        : 0

        Text {
            id: proxyStatusText
            anchors.left: parent.left
            anchors.right: proxyCancel.left
            anchors.rightMargin: Theme.spacingMd
            anchors.top: parent.top
            text: AssetLibrary.proxyQueuedCount > 0
                  ? qsTr("Creating proxy for %1 (%2 more)").arg(AssetLibrary.proxyCurrentName)
                                                            .arg(AssetLibrary.proxyQueuedCount)
                  : qsTr("Creating proxy for %1").arg(AssetLibrary.proxyCurrentName)
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            elide: Text.ElideMiddle
        }
        ThemedProgressBar {
            id: proxyStatusBar
            anchors.left: parent.left
            anchors.right: proxyCancel.left
            anchors.rightMargin: Theme.spacingMd
            anchors.top: proxyStatusText.bottom
            anchors.topMargin: Theme.spacingSm
            barHeight: Theme.spacingXs
            value: AssetLibrary.proxyProgress
        }
        IconButton {
            id: proxyCancel
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            glyph: Theme.icons.x
            variant: "ghost"
            tooltip: qsTr("Stop creating proxies")
            onClicked: AssetLibrary.cancelProxies()
        }
    }

    // A frame-rate conversion has no preview window to report in, so it reports here. Crops
    // keep theirs in the preview window.
    Item {
        id: conversionStatusRow
        anchors.top: proxyStatusRow.bottom
        anchors.topMargin: visible ? Theme.spacingSm : 0
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.pagePadding
        anchors.rightMargin: Theme.pagePadding
        visible: EditorState.editingAsset && EditorState.assetEditIsConversion
        height: visible ? Math.max(conversionStatusText.implicitHeight + Theme.spacingSm
                                   + conversionStatusBar.height, conversionCancel.height)
                        : 0

        Text {
            id: conversionStatusText
            anchors.left: parent.left
            anchors.right: conversionCancel.left
            anchors.rightMargin: Theme.spacingMd
            anchors.top: parent.top
            text: qsTr("Converting %1 to an edit-friendly format").arg(EditorState.assetEditName)
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            elide: Text.ElideMiddle
        }
        ThemedProgressBar {
            id: conversionStatusBar
            anchors.left: parent.left
            anchors.right: conversionCancel.left
            anchors.rightMargin: Theme.spacingMd
            anchors.top: conversionStatusText.bottom
            anchors.topMargin: Theme.spacingSm
            barHeight: Theme.spacingXs
            value: EditorState.assetEditProgress
        }
        IconButton {
            id: conversionCancel
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            glyph: Theme.icons.x
            variant: "ghost"
            tooltip: qsTr("Stop converting")
            onClicked: EditorState.cancelAssetEdit()
        }
    }

    GridView {
        id: grid
        // As many columns as fit at the nominal card size, rounded, with the cards stretched or
        // squeezed a little to fill the row — a fixed card left a column's worth of empty space.
        readonly property int columnCount: Math.max(1, Math.round(width / (Theme.assetCardWidth + Theme.assetCardGap)))
        readonly property real cardSize: Math.floor(width / columnCount) - Theme.assetCardGap
        visible: root.gridMode && root.combinedItems.length > 0

        anchors.top: conversionStatusRow.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom

        anchors.margins: Theme.pagePadding
        anchors.topMargin: Theme.spacingMd
        anchors.rightMargin: Theme.pagePadding - Theme.assetCardGap

        cellWidth: Math.floor(width / columnCount)
        cellHeight: (cardSize * 9 / 16) + Theme.spacing3xl + Theme.assetCardGap

        clip: true
        reuseItems: true
        // A mouse drag on a card drags the asset to the timeline; the wheel and touchpad
        // scroll. Letting the grid take mouse drags too made the two fight over every gesture.
        acceptedButtons: Theme.touchUi ? Qt.LeftButton : Qt.NoButton
        ScrollBar.vertical: AppScrollBar { }

        // Only the view on screen gets the items; a hidden view would still build delegates.
        model: root.gridMode ? root.combinedItems : []
        delegate: gridDelegate
    }

    ListView {
        id: listColumn
        visible: !root.gridMode && root.combinedItems.length > 0

        anchors.top: conversionStatusRow.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom

        anchors.margins: Theme.pagePadding
        anchors.topMargin: Theme.spacingMd

        spacing: Theme.spacingMd

        clip: true
        reuseItems: true
        acceptedButtons: Theme.touchUi ? Qt.LeftButton : Qt.NoButton
        ScrollBar.vertical: AppScrollBar { }

        model: root.gridMode ? [] : root.combinedItems
        delegate: listDelegate
    }
}
