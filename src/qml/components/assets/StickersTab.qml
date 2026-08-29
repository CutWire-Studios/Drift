import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
import ".."
import "."

// Stickers tab: category bar over a grid of the built-in sticker set.
Item {
    id: root

    // A clip landed on the timeline. The phone shell closes the sheet on this —
    // the thing you came for is behind it.
    signal added()

    readonly property string favoritesId: "__favorites__"
    property var categories: EditorState.builtinStickerCategories()
    property var allStickers: EditorState.builtinStickers()
    readonly property bool hasStickers: allStickers.length > 0
    property string activeCategory: categories.length > 0 ? categories[0].id : ""
    readonly property string query: search.text.trim().toLowerCase()
    property int favoritesTick: 0

    Connections {
        target: EditorState
        function onAssetFavoritesChanged() {
            root.favoritesTick++
        }
    }

    readonly property var currentStickers: {
        void root.favoritesTick
        const q = root.query
        if (q.length > 0) {
            return root.allStickers.filter(function(s) {
                const label = (s.label || "").toLowerCase()
                const id = (s.id || "").toLowerCase()
                return label.indexOf(q) >= 0 || id.indexOf(q) >= 0
            })
        }
        if (root.activeCategory === root.favoritesId) {
            return root.allStickers.filter(function(s) {
                return EditorState.isAssetFavorite("stickers", s.id)
            })
        }
        return root.allStickers.filter(function(s) { return s.category === root.activeCategory })
    }

    Connections {
        target: Addons
        function onKindChanged(kind) {
            if (kind !== "stickers")
                return
            root.categories = EditorState.builtinStickerCategories()
            root.allStickers = EditorState.builtinStickers()
            root.activeCategory = root.categories.length > 0 ? root.categories[0].id : ""
        }
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
            visible: root.hasStickers
            height: visible ? implicitHeight : 0
            placeholderText: qsTr("Search stickers")
            font.family: Theme.fontFamily
        }

        Item {
            width: 1
            height: root.hasStickers ? Theme.spacingMd : 0
        }

        AssetCategoryChips {
            id: categoryChips
            width: parent.width
            categories: root.hasStickers ? root.categories : []
            activeCategory: root.activeCategory
            searching: root.query.length > 0
            onCategoryActivated: (categoryId) => root.activeCategory = categoryId
        }

        Flickable {
            width: parent.width
            height: Math.max(0, parent.height - Theme.pagePadding - search.height
                             - (root.hasStickers ? Theme.spacingMd : 0) - categoryChips.height)
            contentHeight: stickerPageContent.height + Theme.pagePadding * 2
            clip: true
            ScrollBar.vertical: AppScrollBar { }

            Column {
                id: stickerPageContent
                x: Theme.pagePadding
                y: Theme.pagePadding
                width: parent.width - Theme.pagePadding * 2
                spacing: Theme.spacingXl

                EmptyState {
                    visible: !root.hasStickers
                    width: parent.width
                    compact: true
                    glyph: Theme.icons.smile
                    title: qsTr("No sticker packs installed")
                    hint: qsTr("Install the emoji pack to add stickers.")
                    actionText: qsTr("Get extras")
                    actionVariant: "primary"
                    onActionTriggered: root.Window.window.openAddonManager("stickers")
                }

                EmptyState {
                    visible: root.hasStickers && root.currentStickers.length === 0
                    width: parent.width
                    compact: true
                    glyph: Theme.icons.smile
                    title: root.query.length > 0
                           ? qsTr("No stickers match “%1”").arg(search.text.trim())
                           : (root.activeCategory === root.favoritesId
                              ? qsTr("No favorites yet")
                              : qsTr("Nothing in this category"))
                    hint: root.query.length > 0
                          ? qsTr("Try a different name.")
                          : (root.activeCategory === root.favoritesId
                             ? qsTr("Star stickers to save them here.")
                             : qsTr("Pick another category."))
                }

                Grid {
                    width: parent.width
                    visible: root.currentStickers.length > 0
                    columns: Math.max(1, Math.floor((width + Theme.assetCardGap) / (Theme.assetCardWidth + Theme.assetCardGap)))
                    columnSpacing: Theme.assetCardGap
                    rowSpacing: Theme.assetCardGap

                    Repeater {
                        model: root.currentStickers
                        delegate: Column {
                            required property var modelData
                            width: Theme.assetCardWidth
                            spacing: Theme.spacingSm

                            Rectangle {
                                width: Theme.assetCardWidth
                                height: Theme.assetCardWidth
                                radius: Theme.radiusSm
                                color: stickerMouse.containsMouse ? Theme.popoverHover : Theme.panelAccent
                                clip: true

                                Behavior on color {
                                    ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                                }

                                SkeletonBox {
                                    anchors.fill: parent
                                    anchors.margins: Theme.pagePadding
                                    visible: stickerImage.status === Image.Loading
                                }

                                Image {
                                    id: stickerImage
                                    anchors.fill: parent
                                    anchors.margins: Theme.pagePadding
                                    source: EditorState.imageUrl(modelData.path)
                                    fillMode: Image.PreserveAspectFit
                                    asynchronous: true
                                    opacity: status === Image.Ready ? 1 : 0

                                    Behavior on opacity {
                                        NumberAnimation { duration: Theme.durationBase; easing.type: Theme.easing }
                                    }
                                }

                                IconGlyph {
                                    anchors.centerIn: parent
                                    visible: stickerImage.status === Image.Error
                                    glyph: Theme.icons.error
                                    iconSize: Theme.iconSizeLg
                                    iconColor: Theme.mutedForeground
                                }

                                ThemedToolTip {
                                    text: modelData.label
                                    visible: stickerMouse.containsMouse
                                }

                                MouseArea {
                                    id: stickerMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        EditorState.addStickerClip(modelData.id, -1)
                                        root.added()
                                    }
                                }

                                AssetFavoriteButton {
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.margins: 3
                                    tabId: "stickers"
                                    itemId: modelData.id
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
}
