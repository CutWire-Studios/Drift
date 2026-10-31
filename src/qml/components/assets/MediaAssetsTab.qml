import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import Drift
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
    // Emitted from the video context menu. The parent owns the file picker and the pattern prompt.
    signal linkProxyRequested(string assetId)
    // Emitted from the card/row context menu. The parent owns the rename dialog.
    signal renameRequested(int assetIndex)
    // Emitted from the card/row context menu, image rows only. The parent owns the save dialog.
    signal exportRequested(int assetIndex)
    // Emitted from the card/row context menu. The parent owns the save dialog.
    signal saveAsRequested(int assetIndex)
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

    // "none", "queued", "building", "ready", "linked", or "" for rows a proxy can't apply to.
    function proxyStateOf(assetId) {
        void root._badgeRevision
        return AssetLibrary.proxyState(assetId)
    }
    function editFriendlyOf(assetId) {
        void root._badgeRevision
        return AssetLibrary.isEditFriendly(assetId)
    }
    // Converted media is constant-rate by construction, so the warning never sits beside the pill.
    // A missing file gets its own badge instead, so only one of the two shows.
    function vfrWarningOf(assetId) {
        void root._badgeRevision
        return !AssetLibrary.isEditFriendly(assetId) && !AssetLibrary.isMissing(assetId)
               && AssetLibrary.isVariableFrameRate(assetId)
    }
    function missingOf(assetId) {
        void root._badgeRevision
        return AssetLibrary.isMissing(assetId)
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
        if (EditorState.assetPreview.windowOpen)
            root._refreshPending = true
        else
            refreshCoalesceTimer.restart()
    }
    Connections {
        target: EditorState
        function onUndoStackChanged() { root._requestRefresh() }
    }
    Connections {
        target: EditorState.assetPreview
        function onWindowOpenChanged() {
            if (!EditorState.assetPreview.windowOpen && root._refreshPending) {
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
                    maskRole: "",
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
                    maskRole: asset.maskRole,
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
        const currentFolder = EditorState.bin.currentFolderId
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
                maskRole: "",
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
                maskRole: asset.maskRole,
                depth: 0,
                expanded: false,
                expandable: false
            })
        }

        return items
    }

    MediaAssetsEmptyState {
        importing: root.importing
        onImportRequested: root.importRequested()
        onImportFolderRequested: root.importFolderRequested()
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

    MediaAssetsViewToolbar {
        id: viewControls
        anchors.right: parent.right
        anchors.rightMargin: Theme.pagePadding
        anchors.verticalCenter: search.verticalCenter
        visible: search.visible
        gridMode: root.gridMode
        viewMode: root.viewMode
        treeMode: root.treeMode
        sortByKind: root.sortByKind
        onSortToggled: {
            if (root.sortByKind)
                AssetLibrary.sortByName()
            else
                AssetLibrary.sortByKind()
            root.sortByKind = !root.sortByKind
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
                                               && EditorState.bin.currentFolderId !== ""
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
            currentFolderId: EditorState.bin.currentFolderId
            onNavigate: (folderId) => EditorState.bin.currentFolderId = folderId
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
    MediaAssetsJobStatus {
        id: proxyStatusRow
        anchors.top: binNavRow.bottom
        anchors.topMargin: visible ? Theme.spacingSm : 0
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.pagePadding
        anchors.rightMargin: Theme.pagePadding
        visible: AssetLibrary.proxyBusy
        label: AssetLibrary.proxyQueuedCount > 0
               ? qsTr("Creating proxy for %1 (%2 more)").arg(AssetLibrary.proxyCurrentName)
                                                         .arg(AssetLibrary.proxyQueuedCount)
               : qsTr("Creating proxy for %1").arg(AssetLibrary.proxyCurrentName)
        progress: AssetLibrary.proxyProgress
        cancelTooltip: qsTr("Stop creating proxies")
        onCancelRequested: AssetLibrary.cancelProxies()
    }

    // A frame-rate conversion has no preview window to report in, so it reports here. Crops
    // keep theirs in the preview window.
    MediaAssetsJobStatus {
        id: conversionStatusRow
        anchors.top: proxyStatusRow.bottom
        anchors.topMargin: visible ? Theme.spacingSm : 0
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.pagePadding
        anchors.rightMargin: Theme.pagePadding
        visible: EditorState.bin.editing && EditorState.bin.editIsConversion
        label: qsTr("Converting %1 to an edit-friendly format").arg(EditorState.bin.editName)
        progress: EditorState.bin.editProgress
        cancelTooltip: qsTr("Stop converting")
        onCancelRequested: EditorState.bin.cancelEdit()
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
        delegate: MediaAssetsGridCard {
            host: root
            cardSize: grid.cardSize
        }
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
        delegate: MediaAssetsListRow {
            host: root
        }
    }
}
