pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Drift

PanelFrame {
    id: root

    property bool shown: false
    property string filter: "all"
    property var selectedIds: []
    property string anchorId: ""
    property string editingId: ""
    property string noteId: ""
    property string menuId: ""

    readonly property var lab: EditorState.sourceLab
    readonly property var preview: EditorState.assetPreview
    readonly property bool hasAsset: root.lab.currentAssetId.length > 0
    readonly property bool useTimecode: root.lab.kind !== "audio"
    readonly property real fps: root.lab.fps > 0 ? root.lab.fps : 30
    readonly property var focusItem: Window.activeFocusItem
    readonly property bool wantsActions: root.shown && (ShellBridge.focusWithin(root, focusItem) || hover.hovered)

    readonly property var rows: {
        const out = []
        if (root.filter !== "markers") {
            for (const r of root.lab.ranges) {
                if (root.filter === "favorites" && r.rating <= 0)
                    continue
                if (root.filter === "rejected" && r.rating >= 0)
                    continue
                out.push({ type: "range", id: r.id, time: r.in, in: r.in, out: r.out,
                           rating: r.rating, name: r.name, note: r.note })
            }
        }
        if (root.filter === "all" || root.filter === "markers") {
            for (const m of root.lab.markers)
                out.push({ type: "marker", id: m.id, time: m.time, name: m.name, note: m.note })
        }
        out.sort((a, b) => a.time - b.time)
        return out
    }
    readonly property var selectedRangeIds: root.selectedIds.filter(
        id => root.lab.ranges.some(r => r.id === id))

    function entryFor(id) {
        return root.rows.find(r => r.id === id) ?? null
    }

    function select(id, modifiers) {
        list.forceActiveFocus()
        if (modifiers & Qt.ShiftModifier && root.anchorId.length > 0) {
            const a = root.rows.findIndex(r => r.id === root.anchorId)
            const b = root.rows.findIndex(r => r.id === id)
            if (a >= 0 && b >= 0) {
                root.selectedIds = root.rows.slice(Math.min(a, b), Math.max(a, b) + 1).map(r => r.id)
                return
            }
        }
        if (modifiers & Qt.ControlModifier) {
            root.selectedIds = root.selectedIds.indexOf(id) >= 0
                ? root.selectedIds.filter(s => s !== id) : root.selectedIds.concat([id])
        } else {
            root.selectedIds = [id]
        }
        root.anchorId = id
    }

    function seekTo(entry) {
        root.preview.seek(entry.time)
    }

    function useAsTrim(entry) {
        const end = entry.out >= root.lab.durationSeconds - 0.0005 ? -1 : entry.out
        root.lab.setRecipeValue("trimOut", -1)
        root.lab.setRecipeValue("trimIn", entry.in)
        root.lab.setRecipeValue("trimOut", end)
    }

    function setRating(ids, rating) {
        for (const id of ids)
            root.lab.setRangeRating(id, rating)
    }

    function toggleRating(rating) {
        const ids = root.selectedRangeIds
        if (ids.length === 0)
            return
        const all = ids.every(id => root.lab.ranges.find(r => r.id === id)?.rating === rating)
        root.setRating(ids, all ? 0 : rating)
    }

    function removeSelected() {
        const ids = root.selectedIds.slice()
        for (const id of ids) {
            if (!root.lab.removeRange(id))
                root.lab.removeMarker(id)
        }
        root.selectedIds = []
    }

    function renameSelected() {
        if (root.selectedIds.length > 0)
            root.editingId = root.selectedIds[0]
    }

    function commitRename(entry, name) {
        root.editingId = ""
        if (name === entry.name)
            return
        if (entry.type === "range")
            root.lab.renameRange(entry.id, name)
        else
            root.lab.renameMarker(entry.id, name)
    }

    function makeSubclips() {
        root.lab.makeSubclips(root.selectedRangeIds)
    }

    function runAutoSplit() {
        if (root.lab.ranges.length > 0)
            confirmSplit.open()
        else
            root.lab.autoSplit()
    }

    function handleAction(id) {
        if (!root.wantsActions || root.editingId.length > 0 || id !== "delete")
            return false
        if (root.selectedRangeIds.length === 0)
            return false
        root.toggleRating(-1)
        return true
    }

    readonly property string scope: "global"
    readonly property int priority: 6
    onWantsActionsChanged: {
        if (root.wantsActions)
            ShellBridge.registerActionHandler(root)
        else
            ShellBridge.unregisterActionHandler(root)
    }
    Component.onDestruction: ShellBridge.unregisterActionHandler(root)

    Connections {
        target: root.lab
        function onCurrentAssetIdChanged() {
            root.selectedIds = []
            root.anchorId = ""
            root.editingId = ""
            root.noteId = ""
        }
    }

    HoverHandler { id: hover }

    AssetsPanelHeader {
        id: header
        width: parent.width
        height: Theme.panelHeaderHeight
        title: qsTr("Ranges & Markers")

        ThemedButton {
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingMd
            anchors.verticalCenter: parent.verticalCenter
            visible: root.hasAsset
            variant: "ghost"
            glyph: Theme.icons.wand
            text: qsTr("Auto-split")
            tooltip: qsTr("Detect shots and replace the ranges with one per shot")
            enabled: !root.lab.busy && root.lab.kind === "video"
            onClicked: root.runAutoSplit()
        }
    }

    Flow {
        id: filterRow
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingMd
        spacing: Theme.spacingSm
        visible: root.hasAsset

        Repeater {
            model: [
                { id: "all", label: qsTr("All") },
                { id: "favorites", label: qsTr("Favorites") },
                { id: "rejected", label: qsTr("Rejected") },
                { id: "markers", label: qsTr("Markers") }
            ]
            ThemedChip {
                required property var modelData
                text: modelData.label
                selected: root.filter === modelData.id
                onClicked: root.filter = modelData.id
            }
        }
    }

    ListView {
        id: list
        anchors.top: filterRow.visible ? filterRow.bottom : header.bottom
        anchors.topMargin: Theme.spacingSm
        anchors.bottom: footer.visible ? footer.top : parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.spacingMd
        anchors.rightMargin: Theme.spacingMd
        visible: root.hasAsset && root.rows.length > 0
        clip: true
        spacing: 2
        model: root.rows
        activeFocusOnTab: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar { }

        // The scope's keys are not app-wide shortcuts (Backspace would take the key from every other
        // item), so the list claims them from the shortcut matcher while it has focus.
        Keys.onShortcutOverride: event => {
            if (root.editingId.length === 0 && ShellBridge.scopedAction("sourceLabRanges", event) !== "")
                event.accepted = true
        }
        Keys.onPressed: event => {
            if (root.editingId.length > 0)
                return
            const id = ShellBridge.scopedAction("sourceLabRanges", event)
            if (id === "sourceLabRanges.favoriteRange") {
                root.toggleRating(1)
            } else if (id === "sourceLabRanges.rejectRange") {
                root.toggleRating(-1)
            } else if (id === "sourceLabRanges.removeRange") {
                root.removeSelected()
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                       || event.key === Qt.Key_F2) {
                root.renameSelected()
            } else {
                return
            }
            event.accepted = true
        }

        delegate: SourceLabRangeRow {
            required property var modelData
            width: ListView.view.width
            entry: modelData
            fps: root.fps
            useTimecode: root.useTimecode
            selected: root.selectedIds.indexOf(modelData.id) >= 0
            editing: root.editingId === modelData.id
            noteOpen: root.noteId === modelData.id
            onPressed: modifiers => {
                root.select(modelData.id, modifiers)
                if (!(modifiers & (Qt.ControlModifier | Qt.ShiftModifier)))
                    root.seekTo(modelData)
            }
            onActivated: {
                if (modelData.type === "range")
                    root.useAsTrim(modelData)
            }
            onRenameRequested: root.editingId = modelData.id
            onRenameCommitted: name => root.commitRename(modelData, name)
            onEditCancelled: root.editingId = ""
            onNoteToggled: root.noteId = root.noteId === modelData.id ? "" : modelData.id
            onNoteCommitted: note => {
                if (modelData.type === "range")
                    root.lab.setRangeNote(modelData.id, note)
                else
                    root.lab.setMarkerNote(modelData.id, note)
            }
            onContextRequested: {
                if (root.selectedIds.indexOf(modelData.id) < 0)
                    root.select(modelData.id, 0)
                root.menuId = modelData.id
                rowMenu.popup()
            }
        }
    }

    ThemedButton {
        id: footer
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.margins: Theme.spacingMd
        visible: root.hasAsset && root.lab.ranges.length > 0
        variant: "secondary"
        glyph: Theme.icons.scissors
        text: qsTr("Make subclips (%1)").arg(root.selectedRangeIds.length)
        tooltip: qsTr("Add each selected range to the media bin as its own clip")
        enabled: root.selectedRangeIds.length > 0 && !root.lab.busy
        onClicked: root.makeSubclips()
    }

    ThemedContextMenu {
        id: rowMenu

        readonly property bool anyRange: root.selectedRangeIds.length > 0

        ThemedMenuItem {
            text: qsTr("Favorite")
            icon.name: Theme.icons.star
            visible: rowMenu.anyRange
            onTriggered: root.setRating(root.selectedRangeIds, 1)
        }
        ThemedMenuItem {
            text: qsTr("Reject")
            icon.name: Theme.icons.x
            visible: rowMenu.anyRange
            onTriggered: root.setRating(root.selectedRangeIds, -1)
        }
        ThemedMenuItem {
            text: qsTr("Clear rating")
            visible: rowMenu.anyRange
            onTriggered: root.setRating(root.selectedRangeIds, 0)
        }
        ThemedMenuItem {
            text: qsTr("Rename")
            icon.name: Theme.icons.pencil
            onTriggered: root.editingId = root.menuId
        }
        ThemedMenuItem {
            text: qsTr("Make subclip(s)")
            icon.name: Theme.icons.scissors
            visible: rowMenu.anyRange
            enabled: !root.lab.busy
            onTriggered: root.makeSubclips()
        }
        ThemedMenuItem {
            text: qsTr("Delete")
            icon.name: Theme.icons.trash
            onTriggered: root.removeSelected()
        }
    }

    ThemedDialog {
        id: confirmSplit
        title: qsTr("Replace ranges?")
        acceptText: qsTr("Auto-split")
        acceptOnReturn: false
        onAccepted: root.lab.autoSplit()

        contentItem: ThemedLabel {
            width: parent ? parent.width : 300
            size: "sm"
            wrapMode: Text.WordWrap
            text: qsTr("Auto-split replaces all %n existing ranges with one per detected shot. You can undo it.", "", root.lab.ranges.length)
        }
    }

    EmptyState {
        anchors.top: filterRow.visible ? filterRow.bottom : header.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingLg
        visible: !list.visible
        compact: true
        glyph: Theme.icons.listVideo
        title: !root.hasAsset ? qsTr("Select a clip in the media bin")
                              : root.lab.ranges.length + root.lab.markers.length === 0
                                ? qsTr("No ranges or markers yet") : qsTr("Nothing matches this filter")
        hint: root.hasAsset && root.lab.ranges.length + root.lab.markers.length === 0
              ? qsTr("Press M to drop a marker, or save the trimmed part as a range.") : ""
    }
}
