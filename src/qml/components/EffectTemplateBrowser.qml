import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
// Beat-synced multi-effect presets: category chips + card grid.
Column {
    id: root
    spacing: 0

    readonly property string mineId: "__mine__"
    readonly property var categories: EditorState.effectTemplateCategories()
    readonly property var catalog: EditorState.effectTemplateCatalog()
    // Saved stacks are a different kind of card with their own actions, so My presets swaps the
    // grid for EffectStacksSection rather than being a section in it.
    property bool showStacks: catalog.length === 0
    readonly property string activeCategory: showStacks ? mineId : presetGrid.currentSectionId
    function showCategory(id) {
        showStacks = id === mineId
        if (!showStacks)
            presetGrid.scrollToSection(id)
    }
    property alias searchText: search.text
    readonly property string query: search.text.trim().toLowerCase()
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
        sections: [{ id: root.mineId, label: qsTr("My presets") }].concat(presetGrid.visibleSections)
        activeCategory: root.activeCategory
        searching: root.query.length > 0
        onCategoryActivated: (categoryId) => root.showCategory(categoryId)
    }

    Item {
        width: parent.width
        height: Math.max(0, parent.height - search.height - Theme.spacingSm
                         - categoryChips.height)

        EffectStacksSection {
            id: stacks
            anchors.fill: parent
            visible: root.showStacks && root.query.length === 0
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
            visible: root.catalog.length > 0 && !stacks.visible && presetGrid.itemCount === 0
            text: qsTr("No templates match “%1”.").arg(search.text.trim())
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

        // Rows are a fixed two label lines tall.
        SectionedAssetGrid {
            id: presetGrid
            anchors.fill: parent
            sidePadding: 12
            visible: !stacks.visible && itemCount > 0
            items: root.catalog
            categories: root.categories
            favoritesKind: "templates"
            query: root.query
            cellHeight: cardSize + 4 + Math.ceil(labelMetrics.height) * 2 + Theme.assetCardGap
            cardDelegate: EffectTemplateCard {
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
