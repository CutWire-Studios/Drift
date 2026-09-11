import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
import "assets"

// Browsable audio-effect preset picker: category chips + card grid.
// Drag a card onto a timeline clip, or click / tap + to apply to the selection.
Column {
    id: root
    spacing: 0

    readonly property string favoritesId: "__favorites__"
    readonly property var categories: EditorState.audioEffectCategories()
    readonly property var catalog: EditorState.audioEffectCatalog()
    property string activeCategory: categories.length > 0 ? categories[0].id : ""
    readonly property string query: search.text.trim().toLowerCase()
    property int favoritesTick: 0

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
        onActionTriggered: root.Window.window.openAddonManager("audio-effects")
    }

    Column {
        visible: root.catalog.length > 0
        width: parent.width
        height: parent.height
        spacing: 0

        Text {
            id: browserTip
            width: parent.width - 24
            leftPadding: 12
            rightPadding: 12
            topPadding: 8
            bottomPadding: 4
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            text: EditorState.selectedClip >= 0
                  ? qsTr("Drag a preset onto a clip, or click to apply to the selection")
                  : qsTr("Drag a preset onto a clip in the timeline")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        ThemedTextField {
            id: search
            width: parent.width - 24
            x: 12
            placeholderText: qsTr("Search audio effects")
            font.family: Theme.fontFamily
        }

        Item {
            width: 1
            height: Theme.spacingMd
        }

        AssetCategoryChips {
            id: categoryChips
            width: parent.width
            categories: root.categories
            activeCategory: root.activeCategory
            searching: root.query.length > 0
            onCategoryActivated: (categoryId) => root.activeCategory = categoryId
        }

        Flickable {
            width: parent.width
            height: Math.max(0, parent.height - browserTip.height - search.height - Theme.spacingMd
                             - categoryChips.height)
            contentHeight: Math.max(emptySearchHint.height, presetGrid.height) + 24
            clip: true
            ScrollBar.vertical: AppScrollBar { }

            Text {
                id: emptySearchHint
                x: 12
                y: 12
                width: parent.width - 24
                visible: root.visiblePresets.length === 0
                text: root.query.length > 0
                      ? qsTr("No audio effects match “%1”.").arg(search.text.trim())
                      : (root.activeCategory === root.favoritesId
                         ? qsTr("No favorites yet. Star presets to save them here.")
                         : qsTr("Nothing in this category."))
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                wrapMode: Text.WordWrap
            }

            Grid {
                id: presetGrid
                x: 12
                y: 12
                width: parent.width - 24
                visible: root.visiblePresets.length > 0
                columns: Math.max(1, Math.floor((width + Theme.assetCardGap) / (Theme.assetCardWidth + Theme.assetCardGap)))
                columnSpacing: Theme.assetCardGap
                rowSpacing: Theme.assetCardGap

                Repeater {
                    model: root.visiblePresets
                    delegate: Column {
                        id: presetCard
                        required property var modelData
                        width: Theme.assetCardWidth
                        spacing: 4
                        opacity: presetDrag.active ? 0.85 : 1
                        scale: presetDrag.active ? 1.04 : 1.0

                        Behavior on opacity {
                            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                        }
                        Behavior on scale {
                            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                        }

                        readonly property string thumb: presetCard.modelData.thumbnailPath || ""
                        readonly property string iconGlyph: presetCard.modelData.icon || "audio-lines"

                        Drag.active: presetDrag.active
                        Drag.dragType: Drag.Automatic
                        Drag.supportedActions: Qt.CopyAction
                        Drag.keys: ["application/x-drift-audio-effect"]
                        Drag.mimeData: ({ "application/x-drift-audio-effect": presetCard.modelData.id })
                        Drag.hotSpot.x: width / 2
                        Drag.hotSpot.y: Theme.assetCardWidth / 2

                        Rectangle {
                            width: Theme.assetCardWidth
                            height: Theme.assetCardWidth
                            radius: Theme.radiusSm
                            color: cardHover.hovered ? Theme.panelAccent : Theme.panelBackground
                            border.width: presetDrag.active ? 1 : 0
                            border.color: Theme.primary
                            clip: true
                            scale: cardHover.hovered ? 1.03 : 1.0

                            Behavior on color {
                                ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                            }
                            Behavior on scale {
                                NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                            }
                            Behavior on border.width {
                                NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                            }

                            HoverHandler { id: cardHover }

                            Image {
                                id: presetThumb
                                anchors.fill: parent
                                visible: presetCard.thumb.length > 0 && status === Image.Ready
                                source: presetCard.thumb.length > 0
                                        ? EditorState.imageUrl(presetCard.thumb) : ""
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                smooth: true
                            }

                            IconGlyph {
                                anchors.centerIn: parent
                                visible: presetCard.thumb.length === 0
                                         || presetThumb.status === Image.Error
                                glyph: presetCard.iconGlyph
                                iconSize: 28
                                iconColor: Theme.mutedForeground
                            }

                            TapHandler {
                                // Touch taps arrive through TouchLiftArea below, which
                                // holds the grab this one would need.
                                enabled: !presetDrag.active && !Theme.touchUi
                                onTapped: root.applyPreset(presetCard.modelData.id)
                            }

                            DragHandler {
                                id: presetDrag
                                target: null
                                // Touch lifts through TouchDrag instead: a platform
                                // drag has no touch gesture and cannot leave the sheet.
                                enabled: !Theme.touchUi
                                acceptedButtons: Qt.LeftButton
                            }

                            // Hold to carry the preset onto a specific clip; tap still
                            // applies it to the selection.
                            TouchLiftArea {
                                dragKind: "audioEffect"
                                payload: presetCard.modelData.id
                                label: presetCard.modelData.label
                                thumbnail: presetCard.thumb
                                glyph: presetCard.iconGlyph
                                onLiftTapped: root.applyPreset(presetCard.modelData.id)
                            }

                            AssetFavoriteButton {
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.margins: 3
                                tabId: "sounds"
                                itemId: presetCard.modelData.id
                            }

                            IconButton {
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: 3
                                glyph: Theme.icons.plus
                                variant: "ghost"
                                buttonSize: 18
                                iconSize: 12
                                tooltip: qsTr("Apply to selected clip")
                                enabled: EditorState.selectedClip >= 0
                                onClicked: root.applyPreset(presetCard.modelData.id)
                            }
                        }

                        Text {
                            width: parent.width
                            text: presetCard.modelData.label
                            color: Theme.panelForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeCard
                            font.weight: Font.Medium
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                            maximumLineCount: 2
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
        }
        
    }
}
