import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
// Browsable effect preset picker: category chips + card grid.
// Drag a card onto a timeline clip, or click / tap + to apply to the selection.
Column {
    id: root
    spacing: 0

    readonly property string favoritesId: "__favorites__"
    readonly property string mineId: "__mine__"
    // Bumped when an addon changes the effects on disk (an imported .driftfx, a pack install).
    property int catalogTick: 0
    readonly property var categories: { void catalogTick; return EditorState.effectCategories() }
    readonly property var catalog: { void catalogTick; return EditorState.effectCatalog() }
    property string activeCategory: categories.length > 0 ? categories[0].id : ""
    property alias searchText: search.text
    readonly property string query: search.text.trim().toLowerCase()
    property int favoritesTick: 0

    Connections {
        target: Addons
        function onKindChanged(kind) {
            if (kind === "effects")
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
    readonly property var visiblePresets: {
        void root.favoritesTick
        const q = root.query
        if (q.length > 0) {
            return root.catalog.filter(function(preset) {
                const label = (preset.label || preset.displayName || "").toLowerCase()
                const id = (preset.id || "").toLowerCase()
                return label.indexOf(q) >= 0 || id.indexOf(q) >= 0
            })
        }
        if (root.activeCategory === root.favoritesId) {
            return root.catalog.filter(function(preset) {
                return EditorState.isAssetFavorite("effects", preset.id)
            })
        }
        if (root.activeCategory === root.mineId)
            return root.catalog.filter(function(preset) { return preset.user === true })
        return root.catalog.filter(function(preset) {
            return preset.category === root.activeCategory
        })
    }

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
            categories: root.categories
            activeCategory: root.activeCategory
            showMine: true
            mineLabel: qsTr("My Effects")
            searching: root.query.length > 0
            onCategoryActivated: (categoryId) => root.activeCategory = categoryId
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
                visible: root.visiblePresets.length === 0
                         && !(root.activeCategory === root.mineId && root.query.length === 0)
                text: root.query.length > 0
                      ? qsTr("No effects match “%1”.").arg(search.text.trim())
                      : (root.activeCategory === root.mineId
                         ? qsTr("Nothing here yet. Import a .driftfx or a Drift Forge zip to add your own.")
                         : root.activeCategory === root.favoritesId
                         ? qsTr("No favorites yet. Star presets to save them here.")
                         : qsTr("Nothing in this category."))
                color: Theme.c.effectBrowser.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                wrapMode: Text.WordWrap
            }

            // Nothing imported yet: the Import call to action sits in the middle of the panel.
            EmptyState {
                anchors.centerIn: parent
                width: Math.min(parent.width - Theme.spacing3xl, 260)
                visible: root.activeCategory === root.mineId && root.visiblePresets.length === 0
                         && root.query.length === 0
                compact: true
                glyph: Theme.icons.download
                title: qsTr("%1 is empty").arg(qsTr("My Effects"))
                hint: qsTr("Import a .driftfx or zip made in Drift Forge to add your own.")
                actionText: qsTr("Import")
                onActionTriggered: ShellBridge.main.importUserPackage("")
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

            // One trailing gap wider than the padded area, so the cards pack from the left
            // exactly as the old Grid did. Rows are a fixed height (two label lines plus the
            // "Built-in" line) since a GridView can't size each row to its tallest card.
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
                visible: root.visiblePresets.length > 0
                clip: true
                reuseItems: true
                boundsBehavior: Flickable.StopAtBounds
                acceptedButtons: Theme.touchUi ? Qt.LeftButton : Qt.NoButton
                ScrollBar.vertical: AppScrollBar { }
                cellWidth: Math.floor(width / columnCount)
                cellHeight: presetGrid.cardSize + 4 + Math.ceil(labelMetrics.height) * 2
                            + 4 + Math.ceil(badgeMetrics.height) + Theme.assetCardGap
                model: root.visiblePresets

                delegate: EffectPresetCard {
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
