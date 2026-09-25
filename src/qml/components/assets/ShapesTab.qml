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
    property alias searchText: search.text
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

        Item {
            width: parent.width
            height: Math.max(0, parent.height - Theme.pagePadding - search.height
                             - Theme.spacingMd - categoryChips.height)

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

            FontMetrics {
                id: labelMetrics
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeCard
            }

            // One trailing gap wider than the padded area, so the cards pack from the left exactly
            // as the old Grid did.
            GridView {
                id: shapeGrid
                // As many columns as fit at the nominal card size, rounded, with the cards stretched or
                // squeezed a little to fill the row — a fixed card left a column's worth of empty space.
                readonly property int columnCount: Math.max(1, Math.round(width / (Theme.assetCardWidth + Theme.assetCardGap)))
                readonly property real cardSize: Math.floor(width / columnCount) - Theme.assetCardGap
                x: Theme.pagePadding
                width: parent.width - Theme.pagePadding * 2 + Theme.assetCardGap
                height: parent.height
                topMargin: Theme.pagePadding
                bottomMargin: Theme.pagePadding
                visible: root.currentShapes.length > 0
                clip: true
                reuseItems: true
                boundsBehavior: Flickable.StopAtBounds
                acceptedButtons: Theme.touchUi ? Qt.LeftButton : Qt.NoButton
                ScrollBar.vertical: AppScrollBar { }
                cellWidth: Math.floor(width / columnCount)
                cellHeight: shapeGrid.cardSize + Theme.spacingSm + Math.ceil(labelMetrics.height) + Theme.assetCardGap
                model: root.currentShapes

                delegate: Column {
                    id: shapeCard
                    required property var modelData
                    width: shapeGrid.cardSize
                    spacing: Theme.spacingSm

                    opacity: shapeDrag.active ? 0.85 : 1
                    scale: shapeDrag.active ? 1.04 : (shapeDrag.pressed ? 0.97 : 1.0)

                    Behavior on opacity {
                        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                    }
                    Behavior on scale {
                        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                    }

                    Rectangle {
                        width: shapeGrid.cardSize
                        height: shapeGrid.cardSize
                        radius: Theme.radiusSm
                        color: shapeDrag.hovered ? Theme.popoverHover : Theme.panelAccent
                        border.width: 1
                        border.color: shapeDrag.hovered ? Theme.accent : "transparent"

                        Behavior on color {
                            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                        }
                        Behavior on border.color {
                            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                        }

                        // The catalog entry drawn with its real default style, so the card
                        // shows what lands on the timeline.
                        Image {
                            anchors.fill: parent
                            anchors.margins: Theme.pagePadding
                            source: "image://shape/" + shapeCard.modelData.id
                            sourceSize: Qt.size(shapeGrid.cardSize * 2, shapeGrid.cardSize * 2)
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                            cache: true
                            scale: shapeDrag.pressed ? 0.94 : shapeDrag.hovered ? 1.06 : 1.0
                            Behavior on scale {
                                NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                            }
                        }

                        ThemedToolTip {
                            text: qsTr("%1 — click to add, or drag to the timeline or preview").arg(shapeCard.modelData.label)
                            visible: shapeDrag.hovered && !shapeDrag.active
                        }

                        AssetDragSource {
                            id: shapeDrag
                            anchors.fill: parent
                            kind: "shape"
                            payload: shapeCard.modelData.id
                            label: shapeCard.modelData.label
                            glyph: Theme.icons.shapes
                            onTapped: {
                                EditorState.addShapeClip(shapeCard.modelData.id, -1)
                                root.added()
                            }
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
