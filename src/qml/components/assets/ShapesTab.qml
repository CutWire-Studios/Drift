import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."
import "."

// Shapes tab. Shapes used to be a page inside the Stickers tab, where they came
// and went with the sticker addon and had no room for categories of their own.
Item {
    id: root

    // A clip landed on the timeline. The phone shell closes the sheet on this —
    // the thing you came for is behind it.
    signal added()

    readonly property string favoritesId: "__favorites__"
    readonly property var categories: EditorState.builtinShapeCategories()
    readonly property var allShapes: EditorState.builtinShapes()
    property string activeCategory: categories.length > 0 ? categories[0].id : ""
    readonly property string query: search.text.trim().toLowerCase()
    property int favoritesTick: 0

    Connections {
        target: EditorState
        function onAssetFavoritesChanged() {
            root.favoritesTick++
        }
    }

    readonly property var currentShapes: {
        void root.favoritesTick
        const q = root.query
        if (q.length > 0) {
            return root.allShapes.filter(function(s) {
                const label = (s.label || "").toLowerCase()
                const id = (s.id || "").toLowerCase()
                return label.indexOf(q) >= 0 || id.indexOf(q) >= 0
            })
        }
        if (root.activeCategory === root.favoritesId) {
            return root.allShapes.filter(function(s) {
                return EditorState.isAssetFavorite("shapes", s.id)
            })
        }
        return root.allShapes.filter(function(s) { return s.category === root.activeCategory })
    }

    Column {
        anchors.fill: parent
        spacing: 0

        Item {
            width: 1
            height: Theme.pagePadding
        }

        ThemedTextField {
            id: search
            width: parent.width - Theme.pagePadding * 2
            x: Theme.pagePadding
            placeholderText: qsTr("Search shapes")
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
            height: Math.max(0, parent.height - Theme.pagePadding - search.height
                             - Theme.spacingMd - categoryChips.height)
            contentHeight: Math.max(emptySearchHint.height, shapeGrid.height) + Theme.pagePadding * 2
            clip: true
            ScrollBar.vertical: AppScrollBar { }

            Text {
                id: emptySearchHint
                x: Theme.pagePadding
                y: Theme.pagePadding
                width: parent.width - Theme.pagePadding * 2
                visible: root.currentShapes.length === 0
                text: root.query.length > 0
                      ? qsTr("No shapes match “%1”.").arg(search.text.trim())
                      : (root.activeCategory === root.favoritesId
                         ? qsTr("No favorites yet. Star shapes to save them here.")
                         : qsTr("Nothing in this category."))
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                wrapMode: Text.WordWrap
            }

            Grid {
                id: shapeGrid
                x: Theme.pagePadding
                y: Theme.pagePadding
                width: parent.width - Theme.pagePadding * 2
                visible: root.currentShapes.length > 0
                columns: Math.max(1, Math.floor((width + Theme.assetCardGap) / (Theme.assetCardWidth + Theme.assetCardGap)))
                columnSpacing: Theme.assetCardGap
                rowSpacing: Theme.assetCardGap

                Repeater {
                    model: root.currentShapes
                    delegate: Column {
                        id: shapeCard
                        required property var modelData
                        width: Theme.assetCardWidth
                        spacing: Theme.spacingSm

                        opacity: shapeDrag.active ? 0.85 : 1
                        scale: shapeDrag.active ? 1.04 : 1.0

                        Behavior on opacity {
                            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                        }
                        Behavior on scale {
                            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                        }

                        Drag.active: shapeDrag.active
                        Drag.dragType: Drag.Automatic
                        Drag.supportedActions: Qt.CopyAction
                        Drag.keys: ["application/x-drift-shape"]
                        Drag.mimeData: { "application/x-drift-shape": shapeCard.modelData.id }

                        Rectangle {
                            width: Theme.assetCardWidth
                            height: Theme.assetCardWidth
                            radius: Theme.radiusSm
                            color: shapeHover.hovered ? Theme.popoverHover : Theme.panelAccent

                            Behavior on color {
                                ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                            }

                            ShapePreview {
                                anchors.fill: parent
                                anchors.margins: Theme.pagePadding
                                shapeKind: shapeCard.modelData.id
                            }

                            HoverHandler {
                                id: shapeHover
                                cursorShape: Qt.PointingHandCursor
                            }

                            ThemedToolTip {
                                text: qsTr("%1 — click to add, or drag to the timeline").arg(shapeCard.modelData.label)
                                visible: shapeHover.hovered
                            }

                            TapHandler {
                                onTapped: {
                                    EditorState.addShapeClip(shapeCard.modelData.id, -1)
                                    root.added()
                                }
                            }
                            DragHandler {
                                id: shapeDrag
                                target: null
                                // Touch adds at the playhead and closes the sheet; a
                                // platform drag has no gesture there and only competes
                                // with the tap for the grab.
                                enabled: !Theme.touchUi
                                acceptedButtons: Qt.LeftButton
                            }

                            AssetFavoriteButton {
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: 3
                                tabId: "shapes"
                                itemId: shapeCard.modelData.id
                            }
                        }

                        Text {
                            width: parent.width
                            text: modelData.label
                            color: Theme.mutedForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeCard
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
        
    }
}
