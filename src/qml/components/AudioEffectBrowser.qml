import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
// Browsable audio-effect preset picker: category chips + card grid.
// Drag a card onto a timeline clip, or click / tap + to apply to the selection.
Column {
    id: root
    spacing: 0

    // Bumped when an addon changes the audio effects on disk (an imported .driftfx, a pack install).
    property int catalogTick: 0
    readonly property var categories: { void catalogTick; return EditorState.audioEffectCategories() }
    readonly property var catalog: { void catalogTick; return EditorState.audioEffectCatalog() }
    // The section at the top of the grid; showCategory() scrolls to one.
    readonly property string activeCategory: presetGrid.currentSectionId
    function showCategory(id) { presetGrid.scrollToSection(id) }
    property alias searchText: search.text
    readonly property string query: search.text.trim().toLowerCase()
    function applyPreset(effectId) {
        if (EditorState.selectedClip < 0)
            return
        EditorState.addAudioEffect(EditorState.selectedTrack, EditorState.selectedClip, effectId)
    }

    EmptyState {
        width: parent.width
        height: visible ? root.height : 0
        visible: root.catalog.length === 0
        glyph: Theme.icons.audioLines
        title: qsTr("No audio effects")
        hint: qsTr("Install the Audio Effects pack from Extras to browse presets here.")
        actionText: qsTr("Install audio effects")
        onActionTriggered: ShellBridge.main.openAddonManager("audio-effects")
    }

    Column {
        visible: root.catalog.length > 0
        width: parent.width
        height: parent.height
        spacing: 0

        AssetSearchBar {
            id: search
            placeholderText: qsTr("Search audio effects")
            historyKey: "audioEffects"

            IconButton {
                glyph: Theme.icons.download
                variant: "ghost"
                buttonSize: Theme.controlHeight
                tooltip: qsTr("Install a custom audio effect or effect from a .driftfx or zip made in Drift Forge")
                onClicked: ShellBridge.main.importUserPackage("")
            }
        }

        Item {
            width: 1
            height: Theme.spacingSm
        }

        AssetCategoryChips {
            id: categoryChips
            width: parent.width
            sections: presetGrid.visibleSections
            activeCategory: root.activeCategory
            searching: root.query.length > 0
            onCategoryActivated: (categoryId) => presetGrid.scrollToSection(categoryId)
        }

        Item {
            width: parent.width
            height: Math.max(0, parent.height - search.height - Theme.spacingSm
                             - categoryChips.height)

            Text {
                id: emptySearchHint
                x: 12
                y: 12
                width: parent.width - 24
                visible: presetGrid.itemCount === 0
                text: qsTr("No audio effects match “%1”.").arg(search.text.trim())
                color: Theme.c.audioEffectBrowser.foregroundMuted
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
                visible: itemCount > 0
                items: root.catalog
                categories: root.categories
                isMine: function(preset) { return preset.user === true }
                mineLabel: qsTr("My Audio Effects")
                favoritesKind: "sounds"
                query: root.query
                cellHeight: cardSize + 4 + Math.ceil(labelMetrics.height) * 2 + Theme.assetCardGap
                cardDelegate: AudioEffectPresetCard {
                    cardSize: presetGrid.cardSize
                    onApplyRequested: (effectId) => {
                        search.commitHistory()
                        root.applyPreset(effectId)
                    }
                    onSearchClearRequested: search.text = ""
                }
            }
        }
        
    }
}
