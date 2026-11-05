import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
// Browsable audio-effect preset picker: category chips + card grid.
// Drag a card onto a timeline clip, or click / tap + to apply to the selection.
Column {
    id: root
    spacing: 0

    readonly property string favoritesId: "__favorites__"
    readonly property string mineId: "__mine__"
    // Bumped when an addon changes the audio effects on disk (an imported .driftfx, a pack install).
    property int catalogTick: 0
    readonly property var categories: { void catalogTick; return EditorState.audioEffectCategories() }
    readonly property var catalog: { void catalogTick; return EditorState.audioEffectCatalog() }
    property string activeCategory: categories.length > 0 ? categories[0].id : ""
    property alias searchText: search.text
    readonly property string query: search.text.trim().toLowerCase()
    property int favoritesTick: 0

    Connections {
        target: Addons
        function onKindChanged(kind) {
            if (kind === "audio-effects")
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
                const label = (preset.label || "").toLowerCase()
                const id = (preset.id || "").toLowerCase()
                return label.indexOf(q) >= 0 || id.indexOf(q) >= 0
            })
        }
        if (root.activeCategory === root.favoritesId) {
            return root.catalog.filter(function(preset) {
                return EditorState.isAssetFavorite("sounds", preset.id)
            })
        }
        if (root.activeCategory === root.mineId)
            return root.catalog.filter(function(preset) { return preset.user === true })
        return root.catalog.filter(function(preset) {
            return preset.category === root.activeCategory
        })
    }

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
            categories: root.categories
            activeCategory: root.activeCategory
            showMine: true
            mineLabel: qsTr("My Audio Effects")
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
                      ? qsTr("No audio effects match “%1”.").arg(search.text.trim())
                      : (root.activeCategory === root.mineId
                         ? qsTr("Nothing here yet. Import a .driftfx or a Drift Forge zip to add your own.")
                         : root.activeCategory === root.favoritesId
                         ? qsTr("No favorites yet. Star presets to save them here.")
                         : qsTr("Nothing in this category."))
                color: Theme.c.audioEffectBrowser.foregroundMuted
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
                title: qsTr("%1 is empty").arg(qsTr("My Audio Effects"))
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
                visible: root.visiblePresets.length > 0
                clip: true
                reuseItems: true
                boundsBehavior: Flickable.StopAtBounds
                acceptedButtons: Theme.touchUi ? Qt.LeftButton : Qt.NoButton
                ScrollBar.vertical: AppScrollBar { }
                cellWidth: Math.floor(width / columnCount)
                cellHeight: presetGrid.cardSize + 4 + Math.ceil(labelMetrics.height) * 2 + Theme.assetCardGap
                model: root.visiblePresets

                delegate: AudioEffectPresetCard {
                    cardSize: presetGrid.cardSize
                    onApplyRequested: (effectId) => root.applyPreset(effectId)
                }
            }
        }
        
    }
}
