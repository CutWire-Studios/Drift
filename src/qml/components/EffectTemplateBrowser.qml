import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
// Beat-synced multi-effect presets: category chips + card grid.
Column {
    id: root
    spacing: 0

    readonly property string favoritesId: "__favorites__"
    readonly property string mineId: "__mine__"
    readonly property var categories: EditorState.effectTemplateCategories()
    readonly property var catalog: EditorState.effectTemplateCatalog()
    property string activeCategory: categories.length > 0 ? categories[0].id : mineId
    property alias searchText: search.text
    readonly property string query: search.text.trim().toLowerCase()
    property int favoritesTick: 0

    Connections {
        target: EditorState
        function onAssetFavoritesChanged() {
            root.favoritesTick++
        }
    }

    // Search spans every category — once you have a name, the sectors are in the way.
    readonly property var visibleTemplates: {
        void root.favoritesTick
        const q = root.query
        if (q.length > 0) {
            return root.catalog.filter(function(preset) {
                const label = (preset.label || "").toLowerCase()
                const id = (preset.id || "").toLowerCase()
                return label.indexOf(q) >= 0 || id.indexOf(q) >= 0
            })
        }
        if (root.activeCategory === root.favoritesId) {
            return root.catalog.filter(function(preset) {
                return EditorState.isAssetFavorite("templates", preset.id)
            })
        }
        return root.catalog.filter(function(preset) {
            return preset.category === root.activeCategory
        })
    }

    function applyTemplate(templateId) {
        if (EditorState.selectedClip < 0)
            return
        EditorState.applyEffectTemplate(EditorState.selectedTrack, EditorState.selectedClip, templateId)
    }

    AssetSearchBar {
        id: search
        placeholderText: qsTr("Search templates")
        historyKey: "templates"

        IconButton {
            glyph: Theme.icons.folderInput
            variant: "ghost"
            buttonSize: Theme.controlHeight
            tooltip: qsTr("Import an effect stack…")
            onClicked: stacks.importStack()
        }
    }

    Item {
        width: 1
        height: Theme.spacingSm
    }

    AssetCategoryChips {
        id: categoryChips
        width: parent.width
        categories: root.categories
        activeCategory: root.activeCategory
        showMine: true
        mineLabel: qsTr("My presets")
        searching: root.query.length > 0
        onCategoryActivated: (categoryId) => root.activeCategory = categoryId
    }

    Item {
        width: parent.width
        height: Math.max(0, parent.height - search.height - Theme.spacingSm
                         - categoryChips.height)

        EffectStacksSection {
            id: stacks
            anchors.fill: parent
            visible: root.activeCategory === root.mineId && root.query.length === 0
        }

        // Without the pack installed the catalog is empty and the grid rendered nothing,
        // unlike the sibling audio and transitions tabs which both offer an install CTA.
        EmptyState {
            anchors.centerIn: parent
            width: Math.min(parent.width - Theme.spacing3xl, 260)
            visible: root.catalog.length === 0 && !stacks.visible
            compact: true
            glyph: Theme.icons.wand
            title: qsTr("No effect templates")
            hint: qsTr("Install the Effect Templates pack from Extras to browse presets here.")
            actionText: qsTr("Get extras")
            onActionTriggered: ShellBridge.main.openAddonManager()
        }

        Text {
            id: emptySearchHint
            x: 12
            y: 12
            width: parent.width - 24
            visible: root.catalog.length > 0 && !stacks.visible && root.visibleTemplates.length === 0
            text: root.query.length > 0
                  ? qsTr("No templates match “%1”.").arg(search.text.trim())
                  : (root.activeCategory === root.favoritesId
                     ? qsTr("No favorites yet. Star templates to save them here.")
                     : qsTr("Nothing in this category."))
            color: Theme.c.effectTemplateBrowser.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
            wrapMode: Text.WordWrap
        }

        FontMetrics {
            id: labelMetrics
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeCard
            font.weight: Font.Medium
        }

        // One trailing gap wider than the padded area, so the cards pack from the left exactly
        // as the old Grid did. Rows are a fixed two label lines tall.
        GridView {
            id: presetGrid
            // As many columns as fit at the nominal card size, rounded, with the cards stretched or
            // squeezed a little to fill the row — a fixed card left a column's worth of empty space.
            readonly property int columnCount: Math.max(1, Math.round(width / (Theme.assetCardWidth + Theme.assetCardGap)))
            readonly property real cardSize: Math.floor(width / columnCount) - Theme.assetCardGap
            x: 12
            width: parent.width - 24 + Theme.assetCardGap
            height: parent.height
            topMargin: 12
            bottomMargin: 12
            visible: !stacks.visible && root.visibleTemplates.length > 0
            clip: true
            reuseItems: true
            boundsBehavior: Flickable.StopAtBounds
            acceptedButtons: Theme.touchUi ? Qt.LeftButton : Qt.NoButton
            ScrollBar.vertical: AppScrollBar { }
            cellWidth: Math.floor(width / columnCount)
            cellHeight: presetGrid.cardSize + 4 + Math.ceil(labelMetrics.height) * 2 + Theme.assetCardGap
            model: root.visibleTemplates

            delegate: EffectTemplateCard {
                cardSize: presetGrid.cardSize
                onApplyRequested: (templateId) => {
                    search.commitHistory()
                    root.applyTemplate(templateId)
                }
                onSearchClearRequested: search.text = ""
            }
        }
    }
}
