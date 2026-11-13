import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
// Browsable effect preset picker: category chips + card grid.
// Drag a card onto a timeline clip, or click / tap + to apply to the selection.
Column {
    id: root
    spacing: 0

    // Bumped when an addon changes the effects on disk (an imported .driftfx, a pack install).
    property int catalogTick: 0
    readonly property var categories: { void catalogTick; return EditorState.effectCategories() }
    readonly property var catalog: { void catalogTick; return EditorState.effectCatalog() }
    // The section at the top of the grid; showCategory() scrolls to one.
    readonly property string activeCategory: presetGrid.currentSectionId
    function showCategory(id) { presetGrid.scrollToSection(id) }
    property alias searchText: search.text
    readonly property string query: search.text.trim().toLowerCase()
    function applyPreset(effectId) {
        if (EditorState.selectedClip < 0) {
            EditorState.addAdjustmentClipWithEffect(effectId, -1, -1)
            return
        }
        EditorState.addEffect(EditorState.selectedTrack, EditorState.selectedClip, effectId)
    }

    // With the effects pack uninstalled the catalog is empty, activeCategory falls
    // back to "" and the grid rendered nothing — a blank panel with a stray tip line,
    // while the sibling audio and transitions tabs both offered an install CTA.
    // Startup now prompts for these packs and users can decline, so uninstalled is a
    // normal first-run state rather than an edge case.
    EmptyState {
        width: parent.width
        height: visible ? root.height : 0
        visible: root.catalog.length === 0
        glyph: Theme.icons.wand
        title: qsTr("No effects")
        hint: qsTr("Install the Effects pack from Extras to browse presets here.")
        actionText: qsTr("Get extras")
        onActionTriggered: ShellBridge.main.openAddonManager()
    }

    Column {
        visible: root.catalog.length > 0
        width: parent.width
        height: parent.height
        spacing: 0

        AssetSearchBar {
            id: search
            placeholderText: qsTr("Search effects")
            historyKey: "effects"

            IconButton {
                glyph: Theme.icons.wand
                variant: "ghost"
                buttonSize: Theme.controlHeight
                tooltip: qsTr("Add adjustment layer — click, or drag it onto a track")
                down: adjustmentDrag.pressed

                // Tap adds at the playhead as before; drag places it on a track.
                AssetDragSource {
                    id: adjustmentDrag
                    anchors.fill: parent
                    kind: "adjustment"
                    payload: "videoEffects"
                    label: qsTr("Adjustment layer")
                    glyph: Theme.icons.wand
                    onTapped: EditorState.addAdjustmentClip(-1, -1)
                }
            }

            IconButton {
                glyph: Theme.icons.download
                variant: "ghost"
                buttonSize: Theme.controlHeight
                tooltip: qsTr("Install a custom effect or transition from a .driftfx or zip made in Drift Forge")
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
                text: qsTr("No effects match “%1”.").arg(search.text.trim())
                color: Theme.c.effectBrowser.foregroundMuted
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

            FontMetrics {
                id: badgeMetrics
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs - 1
            }

            // Rows are a fixed height (two label lines plus the "Built-in" line) since a row
            // can't size itself to its tallest card.
            SectionedAssetGrid {
                id: presetGrid
                anchors.fill: parent
                sidePadding: 12
                visible: itemCount > 0
                items: root.catalog
                categories: root.categories
                isMine: function(preset) { return preset.user === true }
                mineLabel: qsTr("My Effects")
                favoritesKind: "effects"
                query: root.query
                searchFields: ["label", "displayName", "id"]
                cellHeight: cardSize + 4 + Math.ceil(labelMetrics.height) * 2
                            + 4 + Math.ceil(badgeMetrics.height) + Theme.assetCardGap
                cardDelegate: EffectPresetCard {
                    cardSize: presetGrid.cardSize
                    onApplyRequested: (effectId) => {
                        search.commitHistory()
                        root.applyPreset(effectId)
                    }
                    onDeleteRequested: (preset) => deleteEffectDialog.openFor(preset)
                    onSearchClearRequested: search.text = ""
                }
            }
        }
        }

    ThemedDialog {
        id: deleteEffectDialog

        property string effectId: ""
        property string effectLabel: ""
        property int uses: 0

        title: qsTr("Delete effect")
        acceptText: qsTr("Delete")
        acceptVariant: "destructive"
        preferredWidth: Theme.dialogWidthSm
        acceptOnReturn: false

        function openFor(preset) {
            effectId = preset.id
            effectLabel = preset.label
            uses = EditorState.userEffectUses(preset.id)
            open()
        }

        onAccepted: EditorState.deleteUserEffect(deleteEffectDialog.effectId)

        contentItem: Text {
            width: parent ? parent.width : 320
            wrapMode: Text.WordWrap
            text: deleteEffectDialog.uses > 0
                  ? qsTr("Delete “%1” from My Effects? It is used %n time(s) in this project, and those clips will play without it.", "", deleteEffectDialog.uses)
                        .arg(deleteEffectDialog.effectLabel)
                  : qsTr("Delete “%1” from My Effects? You can import its .driftfx file again later.")
                        .arg(deleteEffectDialog.effectLabel)
            color: Theme.c.effectBrowser.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
        }
    }
}
