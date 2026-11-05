import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
// Transitions tab: category bar over a grid of transition kinds, dragged onto clip overlaps.
Item {
    id: root

    // Bumped when an addon changes the transitions on disk (an imported .driftfx, a pack install).
    property int catalogTick: 0
    readonly property var categories: { void catalogTick; return EditorState.transitionCategories() }
    readonly property var catalog: { void catalogTick; return EditorState.transitionKinds() }
    readonly property string favoritesId: "__favorites__"
    readonly property string mineId: "__mine__"
    property string activeCategory: categories.length > 0 ? categories[0].id : ""
    property alias searchText: transitionSearch.text
    readonly property string query: transitionSearch.text.trim().toLowerCase()
    property int favoritesTick: 0

    Connections {
        target: Addons
        function onKindChanged(kind) {
            if (kind === "transitions")
                root.catalogTick++
        }
    }

    Connections {
        target: EditorState
        function onAssetFavoritesChanged() {
            root.favoritesTick++
        }
    }

    // Search spans every category — once you have a name, the sectors are in the way.
    readonly property var visibleTransitions: {
        void root.favoritesTick
        const q = root.query
        if (q.length > 0) {
            return root.catalog.filter(function(item) {
                const label = (item.label || "").toLowerCase()
                const kind = (item.kind || "").toLowerCase()
                return label.indexOf(q) >= 0 || kind.indexOf(q) >= 0
            })
        }
        if (root.activeCategory === root.favoritesId) {
            return root.catalog.filter(function(item) {
                return EditorState.isAssetFavorite("transitions", item.kind)
            })
        }
        if (root.activeCategory === root.mineId)
            return root.catalog.filter(function(item) { return item.user === true })
        return root.catalog.filter(function(item) {
            return item.category === root.activeCategory
        })
    }

    Column {
        anchors.fill: parent
        spacing: 0
        AssetSearchBar {
            id: transitionSearch
            placeholderText: qsTr("Search transitions")

            IconButton {
                glyph: Theme.icons.download
                variant: "ghost"
                buttonSize: Theme.controlHeight
                tooltip: qsTr("Install a custom transition or effect from a .driftfx or zip made in Drift Forge")
                onClicked: ShellBridge.main.importUserPackage("")
            }
        }

        Item { width: 1; height: Theme.spacingSm }

        AssetCategoryChips {
            id: transitionCategoryChips
            width: parent.width
            categories: root.categories
            activeCategory: root.activeCategory
            showMine: true
            mineLabel: qsTr("My Transitions")
            searching: root.query.length > 0
            onCategoryActivated: (categoryId) => root.activeCategory = categoryId
        }

        Item {
            width: parent.width
            height: Math.max(0, parent.height - transitionSearch.height
                             - Theme.spacingSm - transitionCategoryChips.height)

            // A category whose filter matches nothing used to leave a
            // blank scroll area with no explanation.
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
                visible: root.categories.length > 0
                         && root.visibleTransitions.length === 0
                compact: true
                glyph: Theme.icons.search
                title: root.query.length > 0
                       ? qsTr("No transitions match “%1”").arg(transitionSearch.text.trim())
                       : (root.activeCategory === root.mineId
                          ? qsTr("No custom transitions yet")
                          : root.activeCategory === root.favoritesId
                          ? qsTr("No favorites yet")
                          : qsTr("Nothing in this category"))
                hint: root.query.length > 0
                      ? qsTr("Try a different name.")
                      : (root.activeCategory === root.mineId
                         ? qsTr("Import a .driftfx or zip made in Drift Forge to add your own.")
                         : root.activeCategory === root.favoritesId
                         ? qsTr("Star transitions to save them here.")
                         : qsTr("Pick another category."))
                actionText: root.activeCategory === root.mineId && root.query.length === 0
                            ? qsTr("Import") : ""
                onActionTriggered: ShellBridge.main.importUserPackage("")
            }

            FontMetrics {
                id: labelMetrics
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeCard
                font.weight: Font.Medium
            }

            GridView {
                id: transitionGrid
                // As many columns as fit at the nominal card size, rounded, with the cards stretched or
                // squeezed a little to fill the row — a fixed card left a column's worth of empty space.
                readonly property int columnCount: Math.max(1, Math.round(width / (Theme.assetCardWidth + Theme.assetCardGap)))
                readonly property real cardSize: Math.floor(width / columnCount) - Theme.assetCardGap
                // One trailing gap wider than the padded area, so the last column's gap
                // fits and the cards pack from the left exactly as the old Grid did.
                x: Theme.pagePadding
                width: parent.width - Theme.pagePadding * 2 + Theme.assetCardGap
                height: parent.height
                topMargin: Theme.pagePadding
                bottomMargin: Theme.spacing3xl
                visible: root.categories.length > 0
                         && root.visibleTransitions.length > 0
                clip: true
                reuseItems: true
                boundsBehavior: Flickable.StopAtBounds
                acceptedButtons: Theme.touchUi ? Qt.LeftButton : Qt.NoButton
                ScrollBar.vertical: AppScrollBar { }
                cellWidth: Math.floor(width / columnCount)
                cellHeight: transitionGrid.cardSize + 4 + Math.ceil(labelMetrics.height) * 2 + Theme.assetCardGap
                model: root.visibleTransitions

                delegate: TransitionPresetCard {
                    cardSize: transitionGrid.cardSize
                }
            }
        }
    }
}
