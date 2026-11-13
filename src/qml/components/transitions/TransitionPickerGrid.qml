import QtQuick
import QtQuick.Controls.Basic
import Drift
// Searchable, categorised grid of transition kinds with a Recent tab. Shared by the assets tab
// (draggable cards) and the picker popup / sheet (click-to-choose cards). Fills its parent.
Item {
    id: root

    // -1 lists every kind; a track index limits the list to kinds that track can play.
    property int trackIndex: -1
    property string currentKind: ""
    // Left clip of the cut being edited; >= 0 enables the hover preview on desktop.
    property int previewLeftClip: -1
    property bool draggable: false
    property bool showApplyAll: trackIndex >= 0

    // Bumped when an addon changes the transitions on disk (an imported .driftfx, a pack install).
    property int catalogTick: 0
    property int selectedCuts: 0

    readonly property var catalog: {
        void catalogTick
        return trackIndex >= 0 ? EditorState.transitionKindsForTrack(trackIndex)
                               : EditorState.transitionKinds()
    }
    readonly property var categories: {
        const all = EditorState.transitionCategories()
        if (trackIndex < 0)
            return all
        return all.filter(function(c) {
            return root.catalog.some(function(k) { return k.category === c.id })
        })
    }
    // The section at the top of the grid; showCategory() scrolls to one.
    readonly property string activeCategory: transitionGrid.currentSectionId
    function showCategory(id) { transitionGrid.scrollToSection(id) }
    property alias searchText: searchBar.text
    readonly property string query: searchBar.text.trim().toLowerCase()
    default property alias searchActions: searchBar.actions

    readonly property var recentKinds: {
        const recent = EditorState.preferences.recentTransitions
        const byKind = {}
        for (const item of root.catalog)
            byKind[item.kind] = item
        const out = []
        for (const kind of recent) {
            if (byKind[kind])
                out.push(byKind[kind])
        }
        return out
    }

    signal picked(string kind)
    signal applyAllRequested(string kind, bool selectionOnly)

    // Ends any hover preview, then emits picked.
    function commit(kind) {
        previewTimer.stop()
        EditorState.endTransitionPreview()
        root.picked(kind)
    }

    function commitApplyAll() {
        if (root.currentKind === "")
            return
        previewTimer.stop()
        EditorState.endTransitionPreview()
        root.applyAllRequested(root.currentKind, root.selectedCuts >= 1)
    }

    function refreshSelectedCuts() {
        selectedCuts = trackIndex >= 0 ? EditorState.selectedCutCount(trackIndex) : 0
    }

    // An empty kind means the pointer left every card.
    property string pendingPreviewKind: ""

    function cardHover(kind, hovered) {
        if (Theme.touchUi || root.previewLeftClip < 0 || root.trackIndex < 0)
            return
        if (hovered)
            pendingPreviewKind = kind
        else if (pendingPreviewKind === kind)
            pendingPreviewKind = ""
        previewTimer.restart()
    }

    Timer {
        id: previewTimer
        interval: 120
        onTriggered: {
            if (root.pendingPreviewKind === "")
                EditorState.endTransitionPreview()
            else
                EditorState.beginTransitionPreview(root.trackIndex, root.previewLeftClip,
                                                   root.pendingPreviewKind)
        }
    }

    HoverHandler {
        id: rootHover
        onHoveredChanged: {
            if (!hovered) {
                previewTimer.stop()
                root.pendingPreviewKind = ""
                EditorState.endTransitionPreview()
            }
        }
    }

    onVisibleChanged: {
        if (visible) {
            refreshSelectedCuts()
        } else {
            previewTimer.stop()
            pendingPreviewKind = ""
            EditorState.endTransitionPreview()
        }
    }
    onTrackIndexChanged: refreshSelectedCuts()
    Component.onCompleted: refreshSelectedCuts()
    Component.onDestruction: {
        if (previewLeftClip >= 0)
            EditorState.endTransitionPreview()
    }

    Connections {
        target: Addons
        function onKindChanged(kind) {
            if (kind === "transitions")
                root.catalogTick++
        }
    }

    FontMetrics {
        id: labelMetrics
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeCard
        font.weight: Font.Medium
    }

    Column {
        id: header
        anchors.top: parent.top
        width: parent.width
        spacing: 0

        AssetSearchBar {
            id: searchBar
            placeholderText: qsTr("Search transitions")
            historyKey: "transitions"
        }

        Item { width: 1; height: Theme.spacingSm }

        AssetCategoryChips {
            id: chips
            width: parent.width
            sections: transitionGrid.visibleSections
            activeCategory: root.activeCategory
            searching: root.query.length > 0
            onCategoryActivated: (categoryId) => transitionGrid.scrollToSection(categoryId)
        }
    }

    Item {
        id: footer
        anchors.bottom: parent.bottom
        width: parent.width
        visible: root.showApplyAll
        height: visible ? applyAll.height + Theme.spacingMd * 2 + 1 : 0

        Rectangle {
            width: parent.width
            height: 1
            color: Theme.c.transitionPicker.divider
        }

        ThemedButton {
            id: applyAll
            anchors.right: parent.right
            anchors.rightMargin: Theme.pagePadding
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.spacingMd
            enabled: root.currentKind !== ""
            text: root.selectedCuts >= 1
                  ? qsTr("Apply to %n selected cuts", "", root.selectedCuts)
                  : qsTr("Apply to all cuts on track")
            onClicked: root.commitApplyAll()
        }
    }

    Item {
        id: body
        anchors.top: header.bottom
        anchors.bottom: footer.top
        width: parent.width

        EmptyState {
            anchors.centerIn: parent
            width: Math.min(parent.width - Theme.spacing3xl, 260)
            visible: root.categories.length === 0
            glyph: Theme.icons.chevronsRight
            title: qsTr("No transitions available")
            hint: qsTr("Install a transitions pack to add more.")
            actionText: qsTr("Get extras")
            onActionTriggered: ShellBridge.main.openAddonManager()
        }

        EmptyState {
            anchors.centerIn: parent
            width: Math.min(parent.width - Theme.spacing3xl, 260)
            visible: root.categories.length > 0 && transitionGrid.itemCount === 0
            compact: true
            glyph: Theme.icons.search
            title: qsTr("No transitions match “%1”").arg(searchBar.text.trim())
            hint: qsTr("Try a different name.")
        }

        SectionedAssetGrid {
            id: transitionGrid
            anchors.fill: parent
            bottomMargin: Theme.spacing3xl
            visible: root.categories.length > 0 && itemCount > 0
            items: root.catalog
            categories: root.categories
            isMine: function(item) { return item.user === true }
            mineLabel: qsTr("My Transitions")
            recentItems: root.recentKinds
            favoritesKind: "transitions"
            idKey: "kind"
            query: root.query
            searchFields: ["label", "kind"]
            cellHeight: cardSize + 4 + Math.ceil(labelMetrics.height) * 2 + Theme.assetCardGap
            cardDelegate: TransitionPresetCard {
                cardSize: transitionGrid.cardSize
                draggable: root.draggable
                selected: modelData.kind === root.currentKind
                onChosen: (kind) => root.commit(kind)
                onHoveredChanged: root.cardHover(modelData.kind, hovered)
            }
        }
    }
}
