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
    // Comparing the row count before and after tells us how many were rejected.
    // `fromDrop` is the Flatpak case: a drag hands us a host path the sandbox
    // cannot open, which used to be reported as an unsupported format.
    function importUrlsReporting(urls, fromDrop) {
        if (!urls || urls.length === 0)
            return
        // Async, because on Android reading a picked file means copying it out of the
        // SAF stream first. Run inline, that copy blocked the GUI thread for the whole
        // transfer — which also meant the "Importing…" overlay below was set and cleared
        // inside one JS turn and never painted at all.
        const before = AssetLibrary.count
        if (!AssetLibrary.importUrlsAsync(urls)) {
            Toasts.warning(qsTr("An import is already running."))
            return
        }
        root._importRequested = urls.length
        root._countBefore = before
        root._importFromDrop = !!fromDrop
    }

    function importOpenFailedMessage(requested) {
        if (root._importFromDrop && AssetLibrary.sandboxed) {
            return requested === 1
                ? qsTr("Could not open that file. This package cannot read files dropped from other apps — use Import to pick them instead.")
                : qsTr("Could not open those files. This package cannot read files dropped from other apps — use Import to pick them instead.")
        }
        return requested === 1
            ? qsTr("Could not open that file. It may have been moved, or you may not have permission to read it.")
            : qsTr("Could not open any of the selected files.")
    }

    property int _importRequested: 0
    property int _countBefore: 0
    property bool _importFromDrop: false

    Connections {
        target: AssetLibrary
        function onImportFinished(materialized, failed) {
            const requested = root._importRequested
            if (requested <= 0)
                return
            root._importRequested = 0
            const added = AssetLibrary.count - root._countBefore
            const skipped = requested - added
            if (added > 0 && skipped > 0) {
                if (root._importFromDrop && AssetLibrary.sandboxed)
                    Toasts.warning(qsTr("Imported %1 of %2 files. The rest could not be opened — this package cannot read files dropped from other apps. Use Import instead.")
                                   .arg(added).arg(requested))
                else
                    Toasts.warning(qsTr("Imported %1 of %2 files. %3 could not be read.")
                                   .arg(added).arg(requested).arg(skipped))
            } else if (added > 0) {
                Toasts.success(qsTr("Imported %n files.", "", added))
            } else if (failed > 0) {
                Toasts.error(root.importOpenFailedMessage(requested))
            } else if (materialized > 0) {
                Toasts.success(qsTr("Imported %n files.", "", requested))
            } else if (requested === 1) {
                Toasts.error(qsTr("Could not import that file — the format may be unsupported."))
            } else {
                Toasts.error(qsTr("Could not import any of the %n selected files.", "", requested))
            }
        }
    }

    // True while an import is running, so the panel can show progress.
    readonly property bool importing: AssetLibrary.importing

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
    // destination from a flat list.
    property var pendingMoveAssetIds: []
    // The folder every selected asset is in right now, so the picker can omit it — moving them
    // "into" the folder they're already in isn't a real destination. A single common value is
    // safe here (not a per-asset lookup): MediaAssetsTab's grid only ever shows one folder's
    // contents at a time, so anything selectable there already shares this folder.
    property string pendingMoveAssetCurrentFolderId: ""

    function requestMoveAssetToFolder(assetIds) {
        root.pendingMoveAssetIds = assetIds
        root.pendingMoveAssetCurrentFolderId = EditorState.currentBinFolderId
        folderPickerDialog.open()
    }

    ThemedDialog {
        id: folderPickerDialog
        title: qsTr("Move to folder")
        showFooter: false
        preferredWidth: Theme.dialogWidthSm

        // Flat list, root first, minus the folder the asset is already in — nesting depth is
        // not shown, matching the breadcrumb's "where you are" rather than "the whole tree"
        // framing.
        //
        // folderAt() is a plain invokable call, not a property read, so it isn't by itself
        // enough to make this binding re-evaluate after a rename (BinFolderModel.count doesn't
        // change either). Reading undoAvailable is a cheap way to add that dependency: its
        // NOTIFY is undoStackChanged, which fires after every project edit including a rename.
        readonly property var folderOptions: {
            void EditorState.undoAvailable
            const currentFolderId = root.pendingMoveAssetCurrentFolderId
            const out = []
            if (currentFolderId !== "")
                out.push({ id: "", name: qsTr("Media") })
            for (let i = 0; i < BinFolderModel.count; ++i) {
                const folder = BinFolderModel.folderAt(i)
                if (folder.id !== currentFolderId)
                    out.push(folder)
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
                            text: optionRow.modelData.name
                            elide: Text.ElideRight
                            color: Theme.panelForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeSm
                        }

                        onClicked: {
                            if (root.pendingMoveAssetIds.length > 0)
                                EditorState.moveAssetsToFolder(root.pendingMoveAssetIds, optionRow.modelData.id)
                            root.pendingMoveAssetIds = []
                            folderPickerDialog.close()
                        }
                    }
                }
            }
        }
    }

    // Points a bin row at a different file while every clip using it stays put, so a project set
    // up once — music, outro, CTA — can be re-pointed at the next video instead of rebuilt.
    function requestReplaceAsset(assetIndex) {
        var url = FileDialogs.openFile(qsTr("Replace Media"), [
            qsTr("Media files (*.mp4 *.mov *.mkv *.avi *.webm *.m4v *.mp3 *.wav *.aac *.flac *.ogg *.m4a *.png *.jpg *.jpeg *.gif *.webp *.bmp)")
        ])
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
            } else {
                Toasts.success(qsTr("Saved “%1”. Drag it onto the timeline.").arg(message))
            }
        }
    }

    function importMedia() {
        var urls = FileDialogs.openFiles(qsTr("Import Media"), [
            qsTr("Media files (*.mp4 *.mov *.mkv *.avi *.webm *.m4v *.mp3 *.wav *.aac *.flac *.ogg *.m4a *.png *.jpg *.jpeg *.gif *.webp *.bmp)")
        ])
        root.importUrlsReporting(urls)
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
        if (tabId === "media") return ["video", "image", "audio"]
        return []
    }

    function assetVisible(kind) {
        const tabId = tabsModel.get(activeTab).tabId
        if (tabId === "text" || tabId === "subtitles" || tabId === "stickers" || tabId === "shapes"
                || tabId === "effects" || tabId === "templates" || tabId === "adjustment"
                || tabId === "settings" || tabId === "sounds" || tabId === "transitions"
                || tabId === "shortcuts" || tabId === "scenes")
            return false
        const kinds = kindsForTab(tabId)
        return kinds.length === 0 || kinds.indexOf(kind) >= 0
    }

    // ListElement only accepts literal values; qsTr() calls are not
    // evaluated. Labels are translated via tabLabels below.
    property var tabLabels: ({
        "media": qsTr("Media"),
        "text": qsTr("Text"),
        "subtitles": qsTr("Subtitles"),
        "stickers": qsTr("Stickers"),
        "shapes": qsTr("Shapes"),
        "scenes": qsTr("Scenes"),
        "effects": qsTr("Effects"),
        "templates": qsTr("Templates"),
        "transitions": qsTr("Transitions"),
        "sounds": qsTr("Audio FX"),
        "settings": qsTr("Settings"),
        "shortcuts": qsTr("Shortcuts")
    })

    // Rail order: project media → on-canvas graphics → processing → prefs.
    // `separatorAfter` draws a hairline under the tab so groups read as sections.
    // tabId "sounds" is kept for favorites persistence (settings key).
    ListModel {
        id: tabsModel
        ListElement { tabId: "media"; icon: 0; separatorAfter: true }
        ListElement { tabId: "text"; icon: 1; separatorAfter: false }
        ListElement { tabId: "subtitles"; icon: 2; separatorAfter: false }
        ListElement { tabId: "stickers"; icon: 3; separatorAfter: false }
        ListElement { tabId: "shapes"; icon: 4; separatorAfter: true }
        ListElement { tabId: "scenes"; icon: 11; separatorAfter: true }
        ListElement { tabId: "effects"; icon: 5; separatorAfter: false }
        ListElement { tabId: "templates"; icon: 6; separatorAfter: false }
        ListElement { tabId: "transitions"; icon: 7; separatorAfter: false }
        ListElement { tabId: "sounds"; icon: 8; separatorAfter: true }
        ListElement { tabId: "settings"; icon: 9; separatorAfter: false }
        ListElement { tabId: "shortcuts"; icon: 10; separatorAfter: false }
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
        Theme.icons.settings,
        Theme.icons.keyboard,
        Theme.icons.listVideo
    ]
    property int activeTab: 0
    property bool sortByKind: false

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
            property bool gridMode: EditorState.mediaGridMode

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

                    IconButton {
                        glyph: Theme.icons.grid
                        variant: "ghost"
                        tooltip: qsTr("Grid view")
                        active: assetsContent.gridMode
                        onClicked: EditorState.mediaGridMode = true
                    }
                    IconButton {
                        glyph: Theme.icons.list
                        variant: "ghost"
                        tooltip: qsTr("List view")
                        active: !assetsContent.gridMode
                        onClicked: EditorState.mediaGridMode = false
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

                    ThemedButton {
                        text: qsTr("New Folder")
                        variant: "ghost"
                        glyph: Theme.icons.folder
                        tooltip: qsTr("Create a new folder here")
                        anchors.verticalCenter: parent.verticalCenter
                        onClicked: newFolderDialog.open()
                    }

                    ThemedButton {
                        text: qsTr("Import")
                        variant: "ghost"
                        glyph: Theme.icons.upload
                        tooltip: qsTr("Import video, audio or image files")
                        enabled: !root.importing
                        anchors.verticalCenter: parent.verticalCenter
                        onClicked: root.importMedia()
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

            TextAssetsTab {
                visible: tabsModel.get(activeTab).tabId === "text"
                width: parent.width
                opacity: root.tabOpacity
                height: parent.height - Theme.panelHeaderHeight
                onAdded: root.addCompleted()
            }

            SubtitlesTab {
                visible: tabsModel.get(activeTab).tabId === "subtitles"
                width: parent.width
                opacity: root.tabOpacity
                height: parent.height - Theme.panelHeaderHeight
                onAdded: root.addCompleted()
            }

            SoundsTab {
                visible: tabsModel.get(activeTab).tabId === "sounds"
                width: parent.width
                opacity: root.tabOpacity
                height: parent.height - Theme.panelHeaderHeight
            }

            StickersTab {
                visible: tabsModel.get(activeTab).tabId === "stickers"
                width: parent.width
                opacity: root.tabOpacity
                height: parent.height - Theme.panelHeaderHeight
                onAdded: root.addCompleted()
            }

            ShapesTab {
                visible: tabsModel.get(activeTab).tabId === "shapes"
                width: parent.width
                opacity: root.tabOpacity
                height: parent.height - Theme.panelHeaderHeight
                onAdded: root.addCompleted()
            }

            ScenesTab {
                visible: tabsModel.get(activeTab).tabId === "scenes"
                width: parent.width
                opacity: root.tabOpacity
                height: parent.height - Theme.panelHeaderHeight
            }

            SettingsTab {
                visible: tabsModel.get(activeTab).tabId === "settings"
                width: parent.width
                opacity: root.tabOpacity
                height: parent.height - Theme.panelHeaderHeight
            }

            ShortcutsTab {
                visible: tabsModel.get(activeTab).tabId === "shortcuts"
                width: parent.width
                opacity: root.tabOpacity
                height: parent.height - Theme.panelHeaderHeight
            }

            // Effects browser
            EffectBrowser {
                visible: tabsModel.get(activeTab).tabId === "effects"
                width: parent.width
                opacity: root.tabOpacity
                height: parent.height - Theme.panelHeaderHeight
            }

            EffectTemplateBrowser {
                visible: tabsModel.get(activeTab).tabId === "templates"
                width: parent.width
                opacity: root.tabOpacity
                height: parent.height - Theme.panelHeaderHeight
            }

            // Transitions browser
            Item {
                id: transitionsBrowser
                visible: tabsModel.get(activeTab).tabId === "transitions"
                width: parent.width
                opacity: root.tabOpacity
                height: parent.height - Theme.panelHeaderHeight

                readonly property var categories: EditorState.transitionCategories()
                readonly property var catalog: EditorState.transitionKinds()
                readonly property string favoritesId: "__favorites__"
                property string activeCategory: categories.length > 0 ? categories[0].id : ""
                readonly property string query: transitionSearch.text.trim().toLowerCase()
                property int favoritesTick: 0

                Connections {
                    target: EditorState
                    function onAssetFavoritesChanged() {
                        transitionsBrowser.favoritesTick++
                    }
                }

                // Search spans every category — once you have a name, the sectors are in the way.
                readonly property var visibleTransitions: {
                    void transitionsBrowser.favoritesTick
                    const q = transitionsBrowser.query
                    if (q.length > 0) {
                        return transitionsBrowser.catalog.filter(function(item) {
                            const label = (item.label || "").toLowerCase()
                            const kind = (item.kind || "").toLowerCase()
                            return label.indexOf(q) >= 0 || kind.indexOf(q) >= 0
                        })
                    }
                    if (transitionsBrowser.activeCategory === transitionsBrowser.favoritesId) {
                        return transitionsBrowser.catalog.filter(function(item) {
                            return EditorState.isAssetFavorite("transitions", item.kind)
                        })
                    }
                    return transitionsBrowser.catalog.filter(function(item) {
                        return item.category === transitionsBrowser.activeCategory
                    })
                }

                Column {
                    anchors.fill: parent
                    spacing: 0
                    Text {
                        id: transitionTip
                        width: parent.width
                        leftPadding: Theme.pagePadding
                        rightPadding: Theme.pagePadding
                        topPadding: Theme.spacingLg
                        bottomPadding: Theme.spacingSm
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignHCenter
                        maximumLineCount: 3
                        elide: Text.ElideRight
                        text: Theme.touchUi
                              ? qsTr("Touch and hold a transition, then drag it onto where two clips meet.")
                              : qsTr("Drag onto where two clips overlap. They fade into each other by default.")
                        color: Theme.mutedForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                    }

                    ThemedTextField {
                        id: transitionSearch
                        width: parent.width - Theme.pagePadding * 2
                        x: Theme.pagePadding
                        placeholderText: qsTr("Search transitions")
                        font.family: Theme.fontFamily
                    }

                    Item { width: 1; height: Theme.spacingMd }

                    AssetCategoryChips {
                        id: transitionCategoryChips
                        width: parent.width
                        categories: transitionsBrowser.categories
                        activeCategory: transitionsBrowser.activeCategory
                        searching: transitionsBrowser.query.length > 0
                        onCategoryActivated: (categoryId) => transitionsBrowser.activeCategory = categoryId
                    }

                    Item {
                        width: parent.width
                        height: Math.max(0, parent.height - transitionTip.height - transitionSearch.height
                                         - Theme.spacingMd - transitionCategoryChips.height)

                    // A category whose filter matches nothing used to leave a
                    // blank scroll area with no explanation.
                    EmptyState {
                        anchors.centerIn: parent
                        width: Math.min(parent.width - Theme.spacing3xl, 260)
                        visible: transitionsBrowser.categories.length === 0
                        glyph: Theme.icons.chevronsRight
                        title: qsTr("No transitions available")
                        hint: qsTr("Install a transitions pack to add more.")
                        actionText: qsTr("Get extras")
                        onActionTriggered: root.Window.window.openAddonManager()
                    }

                    EmptyState {
                        anchors.centerIn: parent
                        width: Math.min(parent.width - Theme.spacing3xl, 260)
                        visible: transitionsBrowser.categories.length > 0
                                 && transitionsBrowser.visibleTransitions.length === 0
                        compact: true
                        glyph: Theme.icons.search
                        title: transitionsBrowser.query.length > 0
                               ? qsTr("No transitions match “%1”").arg(transitionSearch.text.trim())
                               : (transitionsBrowser.activeCategory === transitionsBrowser.favoritesId
                                  ? qsTr("No favorites yet")
                                  : qsTr("Nothing in this category"))
                        hint: transitionsBrowser.query.length > 0
                              ? qsTr("Try a different name.")
                              : (transitionsBrowser.activeCategory === transitionsBrowser.favoritesId
                                 ? qsTr("Star transitions to save them here.")
                                 : qsTr("Pick another category."))
                    }

                    Flickable {
                    anchors.fill: parent
                    visible: transitionsBrowser.categories.length > 0
                             && transitionsBrowser.visibleTransitions.length > 0
                    contentHeight: transitionGrid.height + Theme.spacing3xl
                    clip: true
                    ScrollBar.vertical: AppScrollBar { }

                    Grid {
                        id: transitionGrid
                        x: Theme.pagePadding
                        y: Theme.pagePadding
                        width: parent.width - Theme.pagePadding * 2
                        columns: Math.max(1, Math.floor((width + Theme.assetCardGap) / (Theme.assetCardWidth + Theme.assetCardGap)))
                        columnSpacing: Theme.assetCardGap
                        rowSpacing: Theme.assetCardGap

                        Repeater {
                            model: transitionsBrowser.visibleTransitions
                            delegate: Column {
                                id: transitionCard
                                required property var modelData
                                width: Theme.assetCardWidth
                                spacing: 4
                                // Lift on grab — matches the media and effect cards.
                                opacity: transitionDrag.active ? 0.85 : 1
                                scale: transitionDrag.active ? 1.04 : 1.0

                                Behavior on opacity {
                                    NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                                }
                                Behavior on scale {
                                    NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                                }

                                readonly property string strip: transitionCard.modelData.previewStripPath || ""
                                readonly property int frameCount: Math.max(1, transitionCard.modelData.previewFrames || 1)

                                // Cards rest on a frame partway through the transition; hovering
                                // scrubs the whole strip, which is the only way to tell many of
                                // these apart (a crossfade and a dip look the same at p = 0.5).
                                property real scrub: 0.45
                                readonly property int frameIndex:
                                    Math.max(0, Math.min(frameCount - 1, Math.round(scrub * (frameCount - 1))))

                                // Without hover there is no way to tell a crossfade from a dip
                                // to black — both rest on the same middle frame — so on touch
                                // every card scrubs continuously instead.
                                NumberAnimation on scrub {
                                    running: transitionCard.frameCount > 1
                                             && (Theme.touchUi || transitionHover.hovered)
                                    from: 0
                                    to: 1
                                    duration: 1400
                                    loops: Animation.Infinite
                                }

                                Connections {
                                    target: transitionHover
                                    function onHoveredChanged() {
                                        if (!transitionHover.hovered)
                                            transitionCard.scrub = 0.45
                                    }
                                }

                                Drag.active: transitionDrag.active
                                Drag.dragType: Drag.Automatic
                                Drag.supportedActions: Qt.CopyAction
                                Drag.keys: ["application/x-drift-transition"]
                                Drag.mimeData: ({ "application/x-drift-transition": transitionCard.modelData.kind })
                                Drag.hotSpot.x: width / 2
                                Drag.hotSpot.y: Theme.assetCardWidth / 2

                                Rectangle {
                                    width: Theme.assetCardWidth
                                    height: Theme.assetCardWidth
                                    radius: Theme.radiusSm
                                    color: transitionHover.hovered ? Theme.panelSecondaryBg : Theme.panelAccent
                                    border.width: transitionDrag.active ? Theme.borderWidth : 0
                                    border.color: Theme.transitionOverlap
                                    clip: true

                                    // The card already had a considered hover
                                    // scrub animation but no transition on its
                                    // own colours.
                                    Behavior on color {
                                        ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                                    }
                                    Behavior on border.width {
                                        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                                    }

                                    HoverHandler {
                                        id: transitionHover
                                        cursorShape: Qt.PointingHandCursor
                                    }

                                    ThemedToolTip {
                                        text: qsTr("%1 — drag onto an overlap between two clips").arg(transitionCard.modelData.label)
                                        visible: transitionHover.hovered
                                    }

                                    DragHandler {
                                        id: transitionDrag
                                        target: null
                                        // Touch lifts through TouchDrag instead: a platform
                                        // drag has no touch gesture and cannot leave the sheet.
                                        enabled: !Theme.touchUi
                                        acceptedButtons: Qt.LeftButton
                                    }

                                    // Hold to carry the transition onto the join between two
                                    // clips. This used to be a tap that applied to whatever was
                                    // selected, which gave no say over which boundary it landed on.
                                    TouchLiftArea {
                                        dragKind: "transition"
                                        payload: transitionCard.modelData.kind
                                        label: transitionCard.modelData.label
                                        glyph: Theme.icons.chevronsRight
                                    }

                                    AssetFavoriteButton {
                                        anchors.right: parent.right
                                        anchors.top: parent.top
                                        anchors.margins: 3
                                        tabId: "transitions"
                                        itemId: transitionCard.modelData.kind
                                    }

                                    SkeletonBox {
                                        anchors.fill: parent
                                        visible: transitionCard.strip.length > 0
                                                 && transitionStrip.status === Image.Loading
                                    }

                                    // The strip is one row of square cells; slide it rather than
                                    // re-decoding a sourceClipRect per frame.
                                    Image {
                                        id: transitionStrip
                                        visible: transitionCard.strip.length > 0
                                                 && status === Image.Ready
                                        source: transitionCard.strip.length > 0
                                                ? EditorState.imageUrl(transitionCard.strip) : ""
                                        height: parent.height
                                        width: parent.height * transitionCard.frameCount
                                        x: -transitionCard.frameIndex * parent.height
                                        fillMode: Image.Stretch
                                        asynchronous: true
                                        smooth: true
                                    }

                                    IconGlyph {
                                        anchors.centerIn: parent
                                        visible: transitionCard.strip.length === 0
                                                 || transitionStrip.status === Image.Error
                                        glyph: Theme.icons.chevronsRight
                                        iconSize: Theme.iconSizeXl
                                        iconColor: Theme.transitionOverlap
                                    }
                                }

                                Text {
                                    width: parent.width
                                    text: transitionCard.modelData.label
                                    color: Theme.panelForeground
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeCard
                                    font.weight: Font.Medium
                                    horizontalAlignment: Text.AlignHCenter
                                    wrapMode: Text.WordWrap
                                    maximumLineCount: 2
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }
                    }
                }
            }
            }

            // Shared media browser used by the Media tab.
            MediaAssetsTab {
                id: mediaAssetsTab
                visible: kindsForTab(tabsModel.get(activeTab).tabId).length > 0
                width: parent.width
                opacity: root.tabOpacity
                height: parent.height - Theme.panelHeaderHeight
                gridMode: assetsContent.gridMode
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
                onMoveToFolderRequested: (assetIds) => root.requestMoveAssetToFolder(assetIds)
                onFolderRenameRequested: (folderId, folderName) => root.requestRenameFolder(folderId, folderName)
            }
        }
    }
}
