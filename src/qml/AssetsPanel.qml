import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import QtQuick.Dialogs
import Drift
import "components"
import "components/assets"

PanelFrame {
    id: root

    // When true (Android bottom sheet), hide the side tab rail — the phone
    // bottom rail already picks the active library tab.
    property bool sheetMode: false

    // One of the click-to-add tabs (text, subtitles, stickers, shapes) put a clip
    // on the timeline. The phone shell closes the sheet on this; desktop, where
    // the panel is docked, simply leaves it unconnected.
    signal addCompleted()
    border.width: sheetMode ? 0 : 1
    radius: sheetMode ? 0 : Theme.radiusSm
    color: sheetMode ? "transparent" : Theme.panelBackground

    Component.onCompleted: AssetLibrary.ensureAllMedia()

    // Imports and reports the outcome. `importUrls` skips anything it cannot
    // probe, so a bad file used to just never appear with no explanation at all.
    // Import policy lives in the MediaImport singleton so surfaces without an AssetsPanel —
    // the home screen, the Android share target — can import too. Kept as a wrapper because
    // several call sites and the DropArea below already speak this name.
    function importUrlsReporting(urls, fromDrop) {
        MediaImport.importUrls(urls, fromDrop)
    }

    // True while an import is running, so the panel can show progress. The folder walk counts:
    // it is the half that can take a while on a deep tree or a sandboxed (portal) mount.
    readonly property bool importing: AssetLibrary.importing || EditorState.importingFolder

    // A single id goes through the existing single-asset add so that case is byte-for-byte the
    // behavior it always was; only an actual multi-selection goes through the batch add, which
    // places each clip back to back in selection order instead of stacking them all at the
    // playhead.
    function requestAddToTimeline(assetIds) {
        if (assetIds.length === 0)
            return

        function runAdd() {
            if (assetIds.length === 1)
                EditorState.addClipFromAsset(AssetLibrary.indexOfId(assetIds[0]))
            else
                EditorState.addClipsFromAssets(assetIds)
        }

        // On a pristine project, the first video/image clip offers to set up the canvas
        // (resolution/orientation) — same flow as dragging onto the timeline (see
        // TimelinePanel.qml/AndroidTimeline.qml). For a multi-selection, offer it from the
        // first asset that actually needs it, not always assetIds[0].
        if (typeof Window === "undefined" || !Window.window || !Window.window.configureAndAddAsset) {
            runAdd()
            return
        }
        for (const id of assetIds) {
            const index = AssetLibrary.indexOfId(id)
            if (index >= 0 && EditorState.shouldConfigureProjectForAsset(index)) {
                Window.window.configureAndAddAsset(index, runAdd)
                return
            }
        }
        runAdd()
    }

    // Asset ids awaiting confirmation in confirmAssetRemoval — a single-element array for a
    // plain right-click, or the whole multi-selection. The label is held separately because
    // the rows are gone by the time the toast reports on them.
    property var pendingRemovalIds: []
    property string pendingRemovalLabel: ""
    property int pendingRemovalClipCount: 0

    // Removing an asset a clip still points at would leave that clip playing but unable to
    // trim past its cut or merge, so refuse rather than confirm — for a bulk removal, refusing
    // the whole batch over one in-use item beats silently dropping it and surprising the user
    // with a smaller removal than they asked for.
    function requestRemoveAsset(assetIds) {
        const names = []
        let clipCount = 0

        for (const id of assetIds) {
            const index = AssetLibrary.indexOfId(id)
            if (index < 0)
                continue

            names.push(AssetLibrary.assetAt(index).name)
            clipCount += EditorState.clipCountForAsset(index)
        }

        if (names.length === 0)
            return

        root.pendingRemovalIds = assetIds
        root.pendingRemovalLabel =
            names.length === 1 ? names[0] : qsTr("%n items", "", names.length)
        root.pendingRemovalClipCount = clipCount
        confirmAssetRemoval.open()
    }

    ThemedDialog {
        id: confirmAssetRemoval
        title: root.pendingRemovalIds.length === 1 ? qsTr("Remove this media?") : qsTr("Remove these items?")
        acceptText: qsTr("Remove")
        acceptVariant: "destructive"
        preferredWidth: Theme.dialogWidthSm
        // Enter must not commit a destructive action.
        acceptOnReturn: false

        contentItem: ThemedLabel {
            width: parent ? parent.width : Theme.dialogWidthSm
            wrapMode: Text.WordWrap
            size: "sm"
            text: root.pendingRemovalClipCount > 0
                ? (root.pendingRemovalClipCount === 1
                    ? qsTr("“%1” is used by 1 clip on the timeline. Removing this media will also remove that clip and any transitions connected to it. The file on disk is not deleted.")
                        .arg(root.pendingRemovalLabel)
                    : qsTr("“%1” is used by %2 clips on the timeline. Removing this media will also remove those clips and any transitions connected to them. The files on disk are not deleted.")
                        .arg(root.pendingRemovalLabel)
                        .arg(root.pendingRemovalClipCount))
                : qsTr("“%1” will be removed from this project. The file on disk is not deleted.")
                    .arg(root.pendingRemovalLabel)
        }

        onAccepted: {
            const removed = root.pendingRemovalClipCount > 0
                ? EditorState.removeAssetsAndClips(root.pendingRemovalIds)
                : EditorState.removeAssets(root.pendingRemovalIds)
            if (removed > 0) {
                Toasts.success(removed === 1
                    ? qsTr("Removed “%1”.").arg(root.pendingRemovalLabel)
                    : qsTr("Removed %n items.", "", removed))
            }
            root.pendingRemovalIds = []
        }
        onRejected: root.pendingRemovalIds = []
    }

    property int pendingRenameIndex: -1

    function requestRenameAsset(assetIndex) {
        const asset = AssetLibrary.assetAt(assetIndex)
        if (!asset || Object.keys(asset).length === 0)
            return
        root.pendingRenameIndex = assetIndex
        assetRenameField.text = asset.name || ""
        assetRenameDialog.open()
    }

    ThemedDialog {
        id: assetRenameDialog
        title: qsTr("Rename media")
        acceptText: qsTr("Rename")
        preferredWidth: Theme.dialogWidthSm

        contentItem: Column {
            width: parent ? parent.width : Theme.dialogWidthSm
            spacing: Theme.spacingMd

            ThemedLabel {
                width: parent.width
                text: qsTr("Name")
                size: "sm"
            }
            ThemedTextField {
                id: assetRenameField
                width: parent.width
                placeholderText: qsTr("Media name")
            }
        }

        onOpened: {
            assetRenameField.forceActiveFocus()
            assetRenameField.selectAll()
        }
        onAccepted: {
            if (root.pendingRenameIndex < 0)
                return
            const label = assetRenameField.text.trim()
            if (label.length > 0)
                EditorState.renameAsset(root.pendingRenameIndex, label)
            root.pendingRenameIndex = -1
        }
        onRejected: root.pendingRenameIndex = -1
    }

    ThemedDialog {
        id: newFolderDialog
        title: qsTr("New folder")
        acceptText: qsTr("Create")
        preferredWidth: Theme.dialogWidthSm

        contentItem: Column {
            width: parent ? parent.width : Theme.dialogWidthSm
            spacing: Theme.spacingMd

            ThemedLabel {
                width: parent.width
                text: qsTr("Name")
                size: "sm"
            }
            ThemedTextField {
                id: newFolderNameField
                width: parent.width
                placeholderText: qsTr("Folder name")
            }
        }

        onOpened: {
            newFolderNameField.text = ""
            newFolderNameField.forceActiveFocus()
        }
        onAccepted: {
            const label = newFolderNameField.text.trim()
            if (label.length > 0)
                EditorState.createBinFolder(label, EditorState.currentBinFolderId)
        }
    }

    property string pendingFolderRenameId: ""

    function requestRenameFolder(folderId, folderName) {
        root.pendingFolderRenameId = folderId
        folderRenameField.text = folderName || ""
        folderRenameDialog.open()
    }

    ThemedDialog {
        id: folderRenameDialog
        title: qsTr("Rename folder")
        acceptText: qsTr("Rename")
        preferredWidth: Theme.dialogWidthSm

        contentItem: Column {
            width: parent ? parent.width : Theme.dialogWidthSm
            spacing: Theme.spacingMd

            ThemedLabel {
                width: parent.width
                text: qsTr("Name")
                size: "sm"
            }
            ThemedTextField {
                id: folderRenameField
                width: parent.width
                placeholderText: qsTr("Folder name")
            }
        }

        onOpened: {
            folderRenameField.forceActiveFocus()
            folderRenameField.selectAll()
        }
        onAccepted: {
            if (root.pendingFolderRenameId.length === 0)
                return
            const label = folderRenameField.text.trim()
            if (label.length > 0)
                EditorState.renameBinFolder(root.pendingFolderRenameId, label)
            root.pendingFolderRenameId = ""
        }
        onRejected: root.pendingFolderRenameId = ""
    }

    // The "move to folder" path — right-click on a card (or a multi-selection), choose a
    // destination from a flat list. Also doubles as the folder-move picker (pendingMoveFolderId)
    // for reparenting a folder itself; the two are mutually exclusive, never both set.
    property var pendingMoveAssetIds: []
    // The folder every selected asset is in right now, so the picker can omit it — moving them
    // "into" the folder they're already in isn't a real destination. A single common value is
    // safe here (not a per-asset lookup): MediaAssetsTab's grid only ever shows one folder's
    // contents at a time, so anything selectable there already shares this folder.
    property string pendingMoveAssetCurrentFolderId: ""
    // Non-empty while the picker is choosing a new parent for this folder rather than a
    // destination for assets.
    property string pendingMoveFolderId: ""

    function requestMoveAssetToFolder(assetIds) {
        root.pendingMoveFolderId = ""
        root.pendingMoveAssetIds = assetIds
        root.pendingMoveAssetCurrentFolderId = EditorState.currentBinFolderId
        folderPickerDialog.open()
    }

    function requestMoveFolder(folderId) {
        root.pendingMoveAssetIds = []
        root.pendingMoveFolderId = folderId
        folderPickerDialog.open()
    }

    // True if candidateId is folderId itself or nested anywhere inside it — walked the same
    // way BinBreadcrumb.qml walks a trail, with the same cycle guard folderPath() uses, since
    // this runs on the same possibly-malformed parentId chains.
    function isFolderOrDescendant(candidateId, folderId) {
        const visited = new Set()
        let id = candidateId
        while (id !== "" && !visited.has(id)) {
            if (id === folderId)
                return true
            visited.add(id)
            const folder = BinFolderModel.folderById(id)
            if (!folder || Object.keys(folder).length === 0)
                break
            id = folder.parentId
        }
        return false
    }

    // Matches BinBreadcrumb.qml's own separator glyph, so a folder's path reads the same way
    // here as it does in the "where you are" trail above the grid.
    readonly property string folderPathSeparator: ">"

    // Full path from the bin root down to folderId, e.g. "Interviews > B-Roll" — walks
    // parentId the same way BinBreadcrumb.qml does, since a flat name alone can't
    // distinguish two same-named folders nested under different parents.
    function folderPath(folderId) {
        const names = []
        const visited = new Set()
        let id = folderId
        // Project deserialization doesn't reject a self- or mutually-parented folder, and
        // this runs once per folder every time the picker opens — an undetected cycle would
        // spin this loop forever and hang the UI, so bail the moment an id repeats.
        while (id !== "" && !visited.has(id)) {
            visited.add(id)
            const folder = BinFolderModel.folderById(id)
            if (!folder || Object.keys(folder).length === 0)
                break
            names.unshift(folder.name)
            id = folder.parentId
        }
        return names.join(" " + root.folderPathSeparator + " ")
    }

    ThemedDialog {
        id: folderPickerDialog
        title: qsTr("Move to folder")
        showFooter: false
        preferredWidth: Theme.dialogWidthSm

        // Flat list, root first, minus destinations that aren't real moves. Each entry shows
        // its full path rather than just its own name, so two folders that happen to share a
        // name (nested under different parents) still read as distinct destinations.
        //
        // folderAt() is a plain invokable call, not a property read, so it isn't by itself
        // enough to make this binding re-evaluate after a rename (BinFolderModel.count doesn't
        // change either). Reading undoAvailable is a cheap way to add that dependency: its
        // NOTIFY is undoStackChanged, which fires after every project edit including a rename.
        readonly property var folderOptions: {
            void EditorState.undoAvailable
            const movingFolderId = root.pendingMoveFolderId
            const out = []
            if (movingFolderId !== "") {
                // Moving a folder itself: exclude the folder, anything already its parent (no-op),
                // and every one of its own descendants — landing there would create a cycle.
                const currentParentId = BinFolderModel.folderById(movingFolderId).parentId || ""
                if (currentParentId !== "")
                    out.push({ id: "", name: qsTr("Media"), path: qsTr("Media") })
                for (let i = 0; i < BinFolderModel.count; ++i) {
                    const folder = BinFolderModel.folderAt(i)
                    if (folder.id === currentParentId)
                        continue
                    if (root.isFolderOrDescendant(folder.id, movingFolderId))
                        continue
                    out.push({ id: folder.id, name: folder.name, path: root.folderPath(folder.id) })
                }
                return out
            }

            const currentFolderId = root.pendingMoveAssetCurrentFolderId
            if (currentFolderId !== "")
                out.push({ id: "", name: qsTr("Media"), path: qsTr("Media") })
            for (let i = 0; i < BinFolderModel.count; ++i) {
                const folder = BinFolderModel.folderAt(i)
                if (folder.id !== currentFolderId)
                    out.push({ id: folder.id, name: folder.name, path: root.folderPath(folder.id) })
            }
            return out
        }

        // A Rectangle used directly as contentItem never reports its explicit `height` as
        // `implicitHeight`, so the Dialog (which sizes off contentItem.implicitHeight) sees zero
        // and clips the list away entirely. Wrapping in a Column — which does propagate its
        // children's real heights into implicitHeight — is the same fix LanguageChooserDialog
        // already uses for the identical list-in-a-dialog shape.
        contentItem: Column {
            width: parent ? parent.width : Theme.dialogWidthSm

            Rectangle {
                width: parent.width
                height: Math.min(pickerList.contentHeight + 2, 280)
                radius: Theme.radiusSm
                color: Theme.appBackground
                border.width: Theme.borderWidth
                border.color: Theme.panelBorder
                clip: true

                ListView {
                    id: pickerList
                    anchors.fill: parent
                    anchors.margins: 1
                    clip: true
                    model: folderPickerDialog.folderOptions
                    interactive: contentHeight > height
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: AppScrollBar { }

                    delegate: ItemDelegate {
                        id: optionRow
                        required property var modelData
                        width: pickerList.width
                        height: 40
                        hoverEnabled: true

                        HoverHandler {
                            cursorShape: Qt.PointingHandCursor
                        }

                        background: Rectangle {
                            color: optionRow.hovered ? Theme.popoverHover : "transparent"
                        }

                        contentItem: Text {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            verticalAlignment: Text.AlignVCenter
                            text: optionRow.modelData.path
                            elide: Text.ElideRight
                            color: Theme.panelForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeSm
                        }

                        onClicked: {
                            // Closed before the move runs, not after: moving a folder changes
                            // undoAvailable, which folderOptions depends on, which swaps the
                            // ListView's model out from under this very delegate — closing
                            // first avoids racing that live update instead of fighting it.
                            const targetId = optionRow.modelData.id
                            folderPickerDialog.close()
                            if (root.pendingMoveFolderId !== "") {
                                EditorState.moveBinFolder(root.pendingMoveFolderId, targetId)
                                root.pendingMoveFolderId = ""
                            } else if (root.pendingMoveAssetIds.length > 0) {
                                EditorState.moveAssetsToFolder(root.pendingMoveAssetIds, targetId)
                                root.pendingMoveAssetIds = []
                            }
                        }
                    }
                }
            }
        }
    }

    // Points a bin row at a different file while every clip using it stays put, so a project set
    // up once — music, outro, CTA — can be re-pointed at the next video instead of rebuilt.
    function requestReplaceAsset(assetIndex) {
        var url = FileDialogs.openFile(qsTr("Replace Media"), [AssetLibrary.mediaNameFilter()])
        if (!url || url.toString() === "")
            return
        EditorState.replaceAssetSource(assetIndex, url)
    }

    // Writes an image row back out to disk — the way a freeze frame captured in the preview
    // leaves the project. The format follows the name the user picks, not the filter.
    function requestExportAsset(assetIndex) {
        const asset = AssetLibrary.assetAt(assetIndex)
        if (!asset || asset.kind !== "image")
            return
        var url = FileDialogs.saveFile(qsTr("Export Image"), [
            qsTr("PNG image (*.png)"),
            qsTr("JPEG image (*.jpg *.jpeg)")
        ], asset.name, "png")
        if (!url || url.toString() === "")
            return
        if (EditorState.exportAssetImage(assetIndex, url))
            Toasts.success(qsTr("Exported “%1”.").arg(asset.name))
        else
            Toasts.error(qsTr("Couldn’t export that image."))
    }

    Connections {
        target: EditorState

        // Hitting the limit outranks the skipped count: the walk stopped early, so what it passed
        // over is only part of the story and saying both would suggest otherwise.
        function onFolderImportFinished(folders, files, skipped, truncated) {
            if (folders === 0) {
                Toasts.error(qsTr("Couldn’t import that folder."))
            } else if (truncated) {
                Toasts.warning(qsTr("Imported %n files into %1 folders — as many as one folder import takes. Import the remaining subfolders separately.", "", files).arg(folders))
            } else if (skipped > 0) {
                Toasts.warning(qsTr("Imported %n files into %1 folders. %2 files were skipped — Drift does not recognize their format. Drag them onto the bin to try anyway.", "", files).arg(folders).arg(skipped))
            } else {
                Toasts.success(qsTr("Imported %n files into %1 folders.", "", files).arg(folders))
            }
        }

        // The probe runs off-thread, so the outcome comes back here rather than from the call.
        function onAssetReplaceFinished(ok, message, adjustedClips) {
            if (!ok) {
                Toasts.warning(message)
            } else if (adjustedClips > 0) {
                Toasts.warning(qsTr("Replaced with “%1”. %n clips were shortened to fit the new file.",
                                    "", adjustedClips).arg(message))
            } else {
                Toasts.success(qsTr("Replaced with “%1”.").arg(message))
            }
        }
        function onAssetEditFinished(ok, message) {
            if (!ok) {
                if (message && message.length > 0)
                    Toasts.warning(message)
            } else if (EditorState.assetEditIsConversion) {
                Toasts.success(qsTr("“%1” is now in an edit-friendly format.").arg(message))
            } else {
                Toasts.success(qsTr("Saved “%1”. Drag it onto the timeline.").arg(message))
            }
        }
    }

    function importMedia() {
        var urls = FileDialogs.openFiles(qsTr("Import Media"),
                                         [AssetLibrary.mediaNameFilter(),
                                          qsTr("All Files (*)")])
        root.importUrlsReporting(urls)
    }

    // Imports a whole directory: a new bin folder mirrors the picked folder (and everything
    // nested under it), and every media file lands in the bin folder matching its containing
    // directory. EditorState.importFolder does the walk synchronously — probing and
    // thumbnailing each file still happens in the background the same as any other import.
    function importFolder() {
        var url = FileDialogs.openDirectory(qsTr("Import Folder"))
        if (!url || url.toString() === "")
            return
        // The walk runs off-thread, so the outcome arrives as onFolderImportFinished below.
        if (!EditorState.importFolder(url))
            Toasts.error(qsTr("Couldn’t import that folder."))
    }

    // Selects a tab by id. Used by cross-panel jumps such as the properties
    // panel's "Browse effects" / "Browse audio effects" empty-state actions.
    function showTab(tabId) {
        for (var i = 0; i < tabsModel.count; ++i) {
            if (tabsModel.get(i).tabId === tabId) {
                root.activeTab = i
                return
            }
        }
    }

    function tabLabel(tabId) {
        return tabLabels[tabId] || ""
    }

    function kindsForTab(tabId) {
        if (tabId === "media") return ["video", "image", "audio", "vector", "model3d"]
        return []
    }

    function assetVisible(kind) {
        const tabId = tabsModel.get(activeTab).tabId
        if (tabId === "text" || tabId === "subtitles" || tabId === "stickers" || tabId === "shapes"
                || tabId === "effects" || tabId === "templates" || tabId === "adjustment"
                || tabId === "sounds" || tabId === "transitions" || tabId === "masks"
                || tabId === "shortcuts" || tabId === "scenes" || tabId === "market")
            return false
        const kinds = kindsForTab(tabId)
        return kinds.length === 0 || kinds.indexOf(kind) >= 0
    }

    // ListElement only accepts literal values; qsTr() calls are not
    // evaluated. Labels are translated via tabLabels below.
    property var tabLabels: ({
        "media": qsTr("Media"),
        "market": qsTr("Market"),
        "text": qsTr("Text"),
        "subtitles": qsTr("Subtitles"),
        "stickers": qsTr("Stickers"),
        "shapes": qsTr("Shapes"),
        "scenes": qsTr("Scenes"),
        "masks": qsTr("Masks"),
        "effects": qsTr("Effects"),
        "templates": qsTr("Templates"),
        "transitions": qsTr("Transitions"),
        "sounds": qsTr("Audio FX"),
        "shortcuts": qsTr("Shortcuts")
    })

    // Rail order: project media → on-canvas graphics → processing → prefs.
    // `separatorAfter` draws a hairline under the tab so groups read as sections.
    // tabId "sounds" is kept for favorites persistence (settings key).
    ListModel {
        id: tabsModel
        ListElement { tabId: "media"; icon: 0; separatorAfter: false }
        ListElement { tabId: "market"; icon: 12; separatorAfter: true }
        ListElement { tabId: "text"; icon: 1; separatorAfter: false }
        ListElement { tabId: "subtitles"; icon: 2; separatorAfter: false }
        ListElement { tabId: "stickers"; icon: 3; separatorAfter: false }
        ListElement { tabId: "shapes"; icon: 4; separatorAfter: false }
        ListElement { tabId: "masks"; icon: 11; separatorAfter: true }
        ListElement { tabId: "scenes"; icon: 10; separatorAfter: true }
        ListElement { tabId: "effects"; icon: 5; separatorAfter: false }
        ListElement { tabId: "templates"; icon: 6; separatorAfter: false }
        ListElement { tabId: "transitions"; icon: 7; separatorAfter: false }
        ListElement { tabId: "sounds"; icon: 8; separatorAfter: true }
        ListElement { tabId: "shortcuts"; icon: 9; separatorAfter: false }
    }
    property var tabIcons: [
        Theme.icons.film,
        Theme.icons.type,
        Theme.icons.captions,
        Theme.icons.smile,
        Theme.icons.shapes,
        Theme.icons.wand,
        Theme.icons.layers,
        Theme.icons.chevronsRight,
        Theme.icons.audioLines,
        Theme.icons.keyboard,
        Theme.icons.listVideo,
        Theme.icons.mask,
        Theme.icons.store
    ]
    property int activeTab: 0
    readonly property string currentTabId: tabsModel.get(activeTab).tabId

    // Per-tab view state that outlives the tab being unloaded, keyed by tabId. Plain
    // storage: tabs read it once on creation and write each change back.
    property var tabState: ({})

    function rememberTab(tabId, key, value) {
        if (!root.tabState[tabId])
            root.tabState[tabId] = {}
        root.tabState[tabId][key] = value
    }

    // Search text and category for the catalog browsers. A category that went away
    // while the tab was unloaded (addon removed) falls back to the tab's default.
    function restoreBrowserTab(tab, tabId) {
        const saved = root.tabState[tabId] || {}
        if (saved.search)
            tab.searchText = saved.search
        if (saved.category === undefined || !tab.categories)
            return
        if (saved.category === "__favorites__"
                || tab.categories.some(category => category.id === saved.category))
            tab.activeCategory = saved.category
    }

    // Fades the tab body in on a tab change instead of hard-cutting to it. Driven
    // as one property the bodies share, rather than fading the whole content
    // Column, so the panel header does not flash along with it. Fade-in only, not
    // a crossfade: the bodies are Column siblings, and overlapping two would
    // double-count height and jump the layout mid-transition.
    property real tabOpacity: 1.0

    onActiveTabChanged: {
        root.tabOpacity = 0
        tabFadeIn.restart()
    }

    NumberAnimation {
        id: tabFadeIn
        target: root
        property: "tabOpacity"
        from: 0.0
        to: 1.0
        duration: Theme.durationBase
        easing.type: Theme.easing
    }

    DropArea {
        id: assetDropArea
        anchors.fill: parent
        keys: ["text/uri-list"]
        onDropped: (drop) => {
            if (drop.hasUrls)
                root.importUrlsReporting(drop.urls, true)
        }
    }

    // Drag feedback. Dropping files onto the panel used to give no visual
    // confirmation that it was even a valid target.
    Rectangle {
        anchors.fill: parent
        z: 50
        radius: Theme.radiusMd
        color: Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.08)
        border.width: Theme.borderWidthFocus
        border.color: Theme.primary
        visible: opacity > 0
        opacity: assetDropArea.containsDrag ? 1 : 0

        Behavior on opacity {
            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }

        EmptyState {
            anchors.centerIn: parent
            glyph: Theme.icons.upload
            title: qsTr("Drop to import")
            hint: qsTr("Video, audio and image files")
        }
    }

    // Import progress. Probing and thumbnailing a large selection blocks for a
    // while; the panel used to simply appear frozen.
    Rectangle {
        anchors.fill: parent
        z: 60
        color: Theme.panelBackground
        opacity: root.importing ? 0.92 : 0
        visible: opacity > 0

        Behavior on opacity {
            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }

        EmptyState {
            anchors.centerIn: parent
            glyph: Theme.icons.spinner
            glyphSpinning: root.importing
            title: qsTr("Importing…")
            hint: qsTr("Reading media and generating thumbnails.")
        }
    }

    Row {
        anchors.fill: parent
        spacing: 0

        // Vertical tab rail. Up/Down move between tabs once it has focus.
        // Flickable so short panel heights can still reach lower icons.
        // Hidden in sheetMode — AndroidBottomRail drives showTab() instead.
        Flickable {
            id: tabRail
            width: root.sheetMode ? 0 : Theme.tabRailWidth
            height: parent.height
            visible: !root.sheetMode
            contentWidth: width
            contentHeight: tabRailColumn.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            interactive: contentHeight > height
            ScrollBar.vertical: AppScrollBar {
                policy: tabRail.contentHeight > tabRail.height
                        ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
            }

            Accessible.role: Accessible.PageTabList

            Keys.onUpPressed: function(event) {
                root.activeTab = (root.activeTab - 1 + tabsModel.count) % tabsModel.count
                event.accepted = true
            }
            Keys.onDownPressed: function(event) {
                root.activeTab = (root.activeTab + 1) % tabsModel.count
                event.accepted = true
            }

            Column {
                id: tabRailColumn
                width: parent.width
                topPadding: Theme.spacingSm
                spacing: Theme.spacingXs

                Repeater {
                    model: tabsModel
                    delegate: Column {
                        required property int index
                        required property var model

                        width: parent.width
                        spacing: 0

                        IconButton {
                            anchors.horizontalCenter: parent.horizontalCenter
                            glyph: root.tabIcons[model.icon]
                            variant: "ghost"
                            tooltip: tabLabels[model.tabId]
                            active: root.activeTab === index
                            onClicked: root.activeTab = index

                            Accessible.role: Accessible.PageTab
                            Accessible.name: tabLabels[model.tabId]
                            Accessible.checked: root.activeTab === index
                        }

                        // Group divider — sits in the rail gap so related tabs
                        // cluster and prefs stay visually apart from content.
                        Item {
                            visible: model.separatorAfter
                            width: parent.width
                            height: visible ? Theme.spacingLg + Theme.borderWidth : 0

                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.verticalCenter: parent.verticalCenter
                                width: Theme.iconSizeSm
                                height: Theme.borderWidth
                                radius: height / 2
                                color: Theme.panelBorder
                                opacity: 0.85
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            width: root.sheetMode ? 0 : Theme.borderWidth
            height: parent.height
            visible: !root.sheetMode
            color: Theme.panelBorder
        }

        Column {
            id: assetsContent
            width: parent.width - (root.sheetMode ? 0 : (Theme.tabRailWidth + Theme.borderWidth))
            height: parent.height

            Rectangle {
                width: parent.width
                height: Theme.panelHeaderHeight
                // Matches the surrounding PanelFrame; it used to paint the app
                // background, so the header read as a different surface than the
                // panel it belongs to.
                color: Theme.panelBackground

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: Theme.borderWidth
                    color: Theme.panelBorder
                }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.pagePadding
                    anchors.verticalCenter: parent.verticalCenter
                    // Sheet chrome already shows the tab title.
                    visible: !root.sheetMode
                    text: tabLabels[tabsModel.get(root.activeTab).tabId]
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm
                }

                // The sticker packs are a curated subset of the emoji set, so the rest live behind
                // this button rather than being unreachable.
                IconButton {
                    id: emojiPickerButton
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    visible: tabsModel.get(root.activeTab).tabId === "stickers"
                    glyph: Theme.icons.plus
                    variant: "ghost"
                    tooltip: qsTr("More emoji")
                    active: emojiPicker.opened
                    onClicked: emojiPicker.opened ? emojiPicker.close() : emojiPicker.open()
                }

                Row {
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6
                    visible: kindsForTab(tabsModel.get(root.activeTab).tabId).length > 0

                    ThemedButton {
                        text: qsTr("New Folder")
                        variant: "ghost"
                        glyph: Theme.icons.folder
                        tooltip: qsTr("Create a new folder here")
                        anchors.verticalCenter: parent.verticalCenter
                        onClicked: newFolderDialog.open()
                    }

                    // Split button: the left half imports files, the chevron opens the
                    // folder variant. Both halves share one bordered box so the header
                    // reads as two actions, not three competing buttons.
                    Rectangle {
                        id: importSplit
                        anchors.verticalCenter: parent.verticalCenter
                        width: importFilesHalf.width
                               + (importMenuHalf.visible ? importSplitDivider.width + importMenuHalf.width : 0)
                        height: Theme.controlHeight
                        radius: Theme.radiusSm
                        color: "transparent"
                        border.width: Theme.borderWidth
                        border.color: Theme.panelBorder
                        opacity: root.importing ? 0.6 : 1

                        Row {
                            anchors.fill: parent
                            spacing: 0

                            ThemedButton {
                                id: importFilesHalf
                                text: qsTr("Import")
                                variant: "ghost"
                                flat: true
                                radius: Theme.radiusXs
                                glyph: Theme.icons.upload
                                tooltip: qsTr("Import video, audio or image files")
                                enabled: !root.importing
                                height: parent.height - Theme.borderWidth * 2
                                anchors.verticalCenter: parent.verticalCenter
                                onClicked: root.importMedia()
                            }

                            Rectangle {
                                id: importSplitDivider
                                width: Theme.borderWidth
                                height: parent.height - Theme.spacingLg
                                anchors.verticalCenter: parent.verticalCenter
                                color: Theme.panelBorder
                                visible: importMenuHalf.visible
                            }

                            ThemedButton {
                                id: importMenuHalf
                                variant: "ghost"
                                flat: true
                                radius: Theme.radiusXs
                                glyph: Theme.icons.chevronDown
                                glyphSize: Theme.iconSizeSm
                                leftPadding: Theme.spacingLg
                                rightPadding: Theme.spacingLg
                                tooltip: qsTr("More import options")
                                enabled: !root.importing
                                visible: !Theme.touchUi
                                height: parent.height - Theme.borderWidth * 2
                                anchors.verticalCenter: parent.verticalCenter
                                onClicked: importMenu.popup(0, importSplit.height + Theme.spacingSm)
                            }
                        }

                        ThemedContextMenu {
                            id: importMenu
                            implicitWidth: 220

                            ThemedMenuItem {
                                text: qsTr("Import Files…")
                                icon.name: Theme.icons.upload
                                onTriggered: root.importMedia()
                            }

                            ThemedMenuItem {
                                text: qsTr("Import Folder…")
                                icon.name: Theme.icons.folderInput
                                onTriggered: root.importFolder()
                            }
                        }
                    }
                }
            }

            EmojiPicker {
                id: emojiPicker
                // Hangs off the button at the panel's right edge, and slides back rather than
                // running off-window when the panel is dragged narrow.
                x: Math.max(-Theme.tabRailWidth, assetsContent.width - width - 8)
                y: Theme.panelHeaderHeight + 4
                onAddonManagerRequested: root.Window.window.openAddonManager("stickers")
                onAdded: root.addCompleted()
            }

            // Only the open tab is instantiated; what should outlive a switch is kept in
            // tabState below. Scroll position is not kept.
            Loader {
                active: root.currentTabId === "text"
                visible: active
                width: parent.width
                height: parent.height - Theme.panelHeaderHeight
                opacity: root.tabOpacity
                sourceComponent: Component {
                    TextAssetsTab {
                        onAdded: root.addCompleted()
                    }
                }
            }

            Loader {
                active: root.currentTabId === "subtitles"
                visible: active
                width: parent.width
                height: parent.height - Theme.panelHeaderHeight
                opacity: root.tabOpacity
                sourceComponent: Component {
                    SubtitlesTab {
                        onAdded: root.addCompleted()
                    }
                }
            }

            Loader {
                active: root.currentTabId === "sounds"
                visible: active
                width: parent.width
                height: parent.height - Theme.panelHeaderHeight
                opacity: root.tabOpacity
                sourceComponent: Component {
                    SoundsTab {
                        Component.onCompleted: root.restoreBrowserTab(this, "sounds")
                        onSearchTextChanged: root.rememberTab("sounds", "search", searchText)
                        onActiveCategoryChanged: root.rememberTab("sounds", "category", activeCategory)
                    }
                }
            }

            Loader {
                active: root.currentTabId === "stickers"
                visible: active
                width: parent.width
                height: parent.height - Theme.panelHeaderHeight
                opacity: root.tabOpacity
                sourceComponent: Component {
                    StickersTab {
                        onAdded: root.addCompleted()
                        Component.onCompleted: root.restoreBrowserTab(this, "stickers")
                        onSearchTextChanged: root.rememberTab("stickers", "search", searchText)
                        onActiveCategoryChanged: root.rememberTab("stickers", "category", activeCategory)
                    }
                }
            }

            Loader {
                active: root.currentTabId === "shapes"
                visible: active
                width: parent.width
                height: parent.height - Theme.panelHeaderHeight
                opacity: root.tabOpacity
                sourceComponent: Component {
                    ShapesTab {
                        onAdded: root.addCompleted()
                        Component.onCompleted: root.restoreBrowserTab(this, "shapes")
                        onSearchTextChanged: root.rememberTab("shapes", "search", searchText)
                        onActiveCategoryChanged: root.rememberTab("shapes", "category", activeCategory)
                    }
                }
            }

            Loader {
                active: root.currentTabId === "scenes"
                visible: active
                width: parent.width
                height: parent.height - Theme.panelHeaderHeight
                opacity: root.tabOpacity
                sourceComponent: Component {
                    ScenesTab {
                        Component.onCompleted: {
                            const saved = root.tabState["scenes"] || {}
                            if (saved.sortByScore !== undefined)
                                sortByScore = saved.sortByScore
                            if (saved.labelFilter !== undefined)
                                labelFilter = saved.labelFilter
                        }
                        onSortByScoreChanged: root.rememberTab("scenes", "sortByScore", sortByScore)
                        onLabelFilterChanged: root.rememberTab("scenes", "labelFilter", labelFilter)
                    }
                }
            }

            Loader {
                active: root.currentTabId === "shortcuts"
                visible: active
                width: parent.width
                height: parent.height - Theme.panelHeaderHeight
                opacity: root.tabOpacity
                sourceComponent: Component {
                    ShortcutsTab {
                        Component.onCompleted: root.restoreBrowserTab(this, "shortcuts")
                        onSearchTextChanged: root.rememberTab("shortcuts", "search", searchText)
                    }
                }
            }

            Loader {
                active: root.currentTabId === "masks"
                visible: active
                width: parent.width
                height: parent.height - Theme.panelHeaderHeight
                opacity: root.tabOpacity
                sourceComponent: Component {
                    MasksTab {
                        onAdded: root.addCompleted()
                    }
                }
            }

            Loader {
                active: root.currentTabId === "effects"
                visible: active
                width: parent.width
                height: parent.height - Theme.panelHeaderHeight
                opacity: root.tabOpacity
                sourceComponent: Component {
                    EffectBrowser {
                        Component.onCompleted: root.restoreBrowserTab(this, "effects")
                        onSearchTextChanged: root.rememberTab("effects", "search", searchText)
                        onActiveCategoryChanged: root.rememberTab("effects", "category", activeCategory)
                    }
                }
            }

            Loader {
                active: root.currentTabId === "templates"
                visible: active
                width: parent.width
                height: parent.height - Theme.panelHeaderHeight
                opacity: root.tabOpacity
                sourceComponent: Component {
                    EffectTemplateBrowser {
                        Component.onCompleted: root.restoreBrowserTab(this, "templates")
                        onSearchTextChanged: root.rememberTab("templates", "search", searchText)
                        onActiveCategoryChanged: root.rememberTab("templates", "category", activeCategory)
                    }
                }
            }

            Loader {
                active: root.currentTabId === "transitions"
                visible: active
                width: parent.width
                height: parent.height - Theme.panelHeaderHeight
                opacity: root.tabOpacity
                sourceComponent: Component {
                    TransitionsTab {
                        Component.onCompleted: root.restoreBrowserTab(this, "transitions")
                        onSearchTextChanged: root.rememberTab("transitions", "search", searchText)
                        onActiveCategoryChanged: root.rememberTab("transitions", "category", activeCategory)
                    }
                }
            }

            // Shared media browser used by the Media tab.
            Loader {
                active: root.kindsForTab(root.currentTabId).length > 0
                visible: active
                width: parent.width
                height: parent.height - Theme.panelHeaderHeight
                opacity: root.tabOpacity
                sourceComponent: Component {
                    MediaAssetsTab {
                        importing: root.importing
                        assetVisibleFn: function(kind) { return root.assetVisible(kind) }
                        onPreviewRequested: (assetIndex) => {
                            if (typeof Window !== "undefined" && Window.window && Window.window.openMediaPreview)
                                Window.window.openMediaPreview(assetIndex)
                        }
                        onAddToTimelineRequested: (assetIds) => root.requestAddToTimeline(assetIds)
                        onRemoveRequested: (assetIds) => root.requestRemoveAsset(assetIds)
                        onReplaceRequested: (assetIndex) => root.requestReplaceAsset(assetIndex)
                        onRenameRequested: (assetIndex) => root.requestRenameAsset(assetIndex)
                        onExportRequested: (assetIndex) => root.requestExportAsset(assetIndex)
                        onImportRequested: root.importMedia()
                        onImportFolderRequested: root.importFolder()
                        onMoveToFolderRequested: (assetIds) => root.requestMoveAssetToFolder(assetIds)
                        onFolderRenameRequested: (folderId, folderName) => root.requestRenameFolder(folderId, folderName)
                        onFolderMoveRequested: (folderId) => root.requestMoveFolder(folderId)

                        Component.onCompleted: {
                            const saved = root.tabState["media"] || {}
                            if (saved.search)
                                searchText = saved.search
                            if (saved.sortByKind !== undefined)
                                sortByKind = saved.sortByKind
                            if (saved.expandedFolderIds)
                                expandedFolderIds = saved.expandedFolderIds
                            if (saved.selectedAssetIds)
                                selectedAssetIds = saved.selectedAssetIds
                            if (saved.selectionAnchorId)
                                selectionAnchorId = saved.selectionAnchorId
                            // Assets removed while the tab was unloaded must not come back selected.
                            pruneSelection()
                        }
                        onSearchTextChanged: root.rememberTab("media", "search", searchText)
                        onSortByKindChanged: root.rememberTab("media", "sortByKind", sortByKind)
                        onExpandedFolderIdsChanged: root.rememberTab("media", "expandedFolderIds", expandedFolderIds)
                        onSelectedAssetIdsChanged: root.rememberTab("media", "selectedAssetIds", selectedAssetIds)
                        onSelectionAnchorIdChanged: root.rememberTab("media", "selectionAnchorId", selectionAnchorId)
                    }
                }
            }

            Loader {
                active: root.currentTabId === "market"
                visible: active
                width: parent.width
                height: parent.height - Theme.panelHeaderHeight
                opacity: root.tabOpacity
                sourceComponent: Component {
                    MarketTab {
                        // Filter values are not restored: the filter fields and dropdowns
                        // don't read them back, so restored filters would be invisible.
                        Component.onCompleted: {
                            const saved = root.tabState["market"] || {}
                            if (saved.search)
                                searchText = saved.search
                            if (saved.submittedQuery)
                                submittedQuery = saved.submittedQuery
                            if (saved.lastSaveDir)
                                lastSaveDir = saved.lastSaveDir
                        }
                        onSearchTextChanged: root.rememberTab("market", "search", searchText)
                        onSubmittedQueryChanged: root.rememberTab("market", "submittedQuery", submittedQuery)
                        onLastSaveDirChanged: root.rememberTab("market", "lastSaveDir", lastSaveDir)
                    }
                }
            }
        }
    }
}
