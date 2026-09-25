import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
import ".."
import "."

// Transitions tab: category bar over a grid of transition kinds, dragged onto clip overlaps.
Item {
    id: root

    readonly property var categories: EditorState.transitionCategories()
    readonly property var catalog: EditorState.transitionKinds()
    readonly property string favoritesId: "__favorites__"
    property string activeCategory: categories.length > 0 ? categories[0].id : ""
    property alias searchText: transitionSearch.text
    readonly property string query: transitionSearch.text.trim().toLowerCase()
    property int favoritesTick: 0

    Connections {
        target: EditorState
        function onAssetFavoritesChanged() {
            root.favoritesTick++
        }
    }

    // Search spans every category — once you have a name, the sectors are in the way.
    readonly property var visibleTransitions: {
        void root.favoritesTick
        const q = root.query
        if (q.length > 0) {
            return root.catalog.filter(function(item) {
                const label = (item.label || "").toLowerCase()
                const kind = (item.kind || "").toLowerCase()
                return label.indexOf(q) >= 0 || kind.indexOf(q) >= 0
            })
        }
        if (root.activeCategory === root.favoritesId) {
            return root.catalog.filter(function(item) {
                return EditorState.isAssetFavorite("transitions", item.kind)
            })
        }
        return root.catalog.filter(function(item) {
            return item.category === root.activeCategory
        })
    }

    Column {
        anchors.fill: parent
        spacing: 0
        Text {
            id: transitionTip
            width: parent.width
            leftPadding: Theme.pagePadding
            rightPadding: Theme.pagePadding
            topPadding: Theme.spacingLg
            bottomPadding: Theme.spacingSm
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            maximumLineCount: 3
            elide: Text.ElideRight
            text: Theme.touchUi
                  ? qsTr("Touch and hold a transition, then drag it onto where two clips meet.")
                  : qsTr("Drag onto where two clips overlap. They fade into each other by default.")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        ThemedTextField {
            id: transitionSearch
            width: parent.width - Theme.pagePadding * 2
            x: Theme.pagePadding
            placeholderText: qsTr("Search transitions")
            font.family: Theme.fontFamily
        }

        Item { width: 1; height: Theme.spacingMd }

        AssetCategoryChips {
            id: transitionCategoryChips
            width: parent.width
            categories: root.categories
            activeCategory: root.activeCategory
            searching: root.query.length > 0
            onCategoryActivated: (categoryId) => root.activeCategory = categoryId
        }

        Item {
            width: parent.width
            height: Math.max(0, parent.height - transitionTip.height - transitionSearch.height
                             - Theme.spacingMd - transitionCategoryChips.height)

            // A category whose filter matches nothing used to leave a
            // blank scroll area with no explanation.
            EmptyState {
                anchors.centerIn: parent
                width: Math.min(parent.width - Theme.spacing3xl, 260)
                visible: root.categories.length === 0
                glyph: Theme.icons.chevronsRight
                title: qsTr("No transitions available")
                hint: qsTr("Install a transitions pack to add more.")
                actionText: qsTr("Get extras")
                onActionTriggered: root.Window.window.openAddonManager()
            }

            EmptyState {
                anchors.centerIn: parent
                width: Math.min(parent.width - Theme.spacing3xl, 260)
                visible: root.categories.length > 0
                         && root.visibleTransitions.length === 0
                compact: true
                glyph: Theme.icons.search
                title: root.query.length > 0
                       ? qsTr("No transitions match “%1”").arg(transitionSearch.text.trim())
                       : (root.activeCategory === root.favoritesId
                          ? qsTr("No favorites yet")
                          : qsTr("Nothing in this category"))
                hint: root.query.length > 0
                      ? qsTr("Try a different name.")
                      : (root.activeCategory === root.favoritesId
                         ? qsTr("Star transitions to save them here.")
                         : qsTr("Pick another category."))
            }

            FontMetrics {
                id: labelMetrics
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeCard
                font.weight: Font.Medium
            }

            GridView {
                id: transitionGrid
                // As many columns as fit at the nominal card size, rounded, with the cards stretched or
                // squeezed a little to fill the row — a fixed card left a column's worth of empty space.
                readonly property int columnCount: Math.max(1, Math.round(width / (Theme.assetCardWidth + Theme.assetCardGap)))
                readonly property real cardSize: Math.floor(width / columnCount) - Theme.assetCardGap
                // One trailing gap wider than the padded area, so the last column's gap
                // fits and the cards pack from the left exactly as the old Grid did.
                x: Theme.pagePadding
                width: parent.width - Theme.pagePadding * 2 + Theme.assetCardGap
                height: parent.height
                topMargin: Theme.pagePadding
                bottomMargin: Theme.spacing3xl
                visible: root.categories.length > 0
                         && root.visibleTransitions.length > 0
                clip: true
                reuseItems: true
                boundsBehavior: Flickable.StopAtBounds
                acceptedButtons: Theme.touchUi ? Qt.LeftButton : Qt.NoButton
                ScrollBar.vertical: AppScrollBar { }
                cellWidth: Math.floor(width / columnCount)
                cellHeight: transitionGrid.cardSize + 4 + Math.ceil(labelMetrics.height) * 2 + Theme.assetCardGap
                model: root.visibleTransitions

                delegate: Column {
                    id: transitionCard
                    required property var modelData
                    width: transitionGrid.cardSize
                    spacing: 4
                    // Lift on grab — matches the media and effect cards.
                    opacity: transitionDrag.active ? 0.85 : 1
                    scale: transitionDrag.active ? 1.04 : 1.0

                    GridView.onPooled: transitionCard.scrub = 0.45

                    Behavior on opacity {
                        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                    }
                    Behavior on scale {
                        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                    }

                    readonly property string strip: transitionCard.modelData.previewStripPath || ""
                    readonly property int frameCount: Math.max(1, transitionCard.modelData.previewFrames || 1)

                    // Cards rest on a frame partway through the transition; hovering
                    // scrubs the whole strip, which is the only way to tell many of
                    // these apart (a crossfade and a dip look the same at p = 0.5).
                    property real scrub: 0.45
                    readonly property int frameIndex:
                        Math.max(0, Math.min(frameCount - 1, Math.round(scrub * (frameCount - 1))))

                    // Without hover there is no way to tell a crossfade from a dip
                    // to black — both rest on the same middle frame — so on touch
                    // every card scrubs continuously instead.
                    NumberAnimation on scrub {
                        running: transitionCard.frameCount > 1
                                 && (Theme.touchUi || transitionHover.hovered)
                        from: 0
                        to: 1
                        duration: 1400
                        loops: Animation.Infinite
                    }

                    Connections {
                        target: transitionHover
                        function onHoveredChanged() {
                            if (!transitionHover.hovered)
                                transitionCard.scrub = 0.45
                        }
                    }

                    Drag.active: transitionDrag.active
                    Drag.dragType: Drag.Automatic
                    Drag.supportedActions: Qt.CopyAction
                    Drag.keys: ["application/x-drift-transition"]
                    Drag.mimeData: ({ "application/x-drift-transition": transitionCard.modelData.kind })
                    Drag.hotSpot.x: width / 2
                    Drag.hotSpot.y: transitionGrid.cardSize / 2

                    Rectangle {
                        width: transitionGrid.cardSize
                        height: transitionGrid.cardSize
                        radius: Theme.radiusSm
                        color: transitionHover.hovered ? Theme.panelSecondaryBg : Theme.panelAccent
                        border.width: transitionDrag.active ? Theme.borderWidth : 0
                        border.color: Theme.transitionOverlap
                        clip: true

                        // The card already had a considered hover
                        // scrub animation but no transition on its
                        // own colours.
                        Behavior on color {
                            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                        }
                        Behavior on border.width {
                            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                        }

                        HoverHandler {
                            id: transitionHover
                            cursorShape: Qt.PointingHandCursor
                        }

                        ThemedToolTip {
                            text: qsTr("%1 — drag onto an overlap between two clips").arg(transitionCard.modelData.label)
                            visible: transitionHover.hovered
                        }

                        DragHandler {
                            id: transitionDrag
                            target: null
                            // Touch lifts through TouchDrag instead: a platform
                            // drag has no touch gesture and cannot leave the sheet.
                            enabled: !Theme.touchUi
                            acceptedButtons: Qt.LeftButton
                            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
                        }

                        // Hold to carry the transition onto the join between two
                        // clips. This used to be a tap that applied to whatever was
                        // selected, which gave no say over which boundary it landed on.
                        TouchLiftArea {
                            dragKind: "transition"
                            payload: transitionCard.modelData.kind
                            label: transitionCard.modelData.label
                            glyph: Theme.icons.chevronsRight
                        }

                        AssetFavoriteButton {
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 3
                            tabId: "transitions"
                            itemId: transitionCard.modelData.kind
                        }

                        SkeletonBox {
                            anchors.fill: parent
                            visible: transitionCard.strip.length > 0
                                     && transitionStrip.status === Image.Loading
                        }

                        // The strip is one row of square cells; slide it rather than
                        // re-decoding a sourceClipRect per frame.
                        Image {
                            id: transitionStrip
                            visible: transitionCard.strip.length > 0
                                     && status === Image.Ready
                            source: transitionCard.strip.length > 0
                                    ? EditorState.imageUrl(transitionCard.strip) : ""
                            height: parent.height
                            width: parent.height * transitionCard.frameCount
                            x: -transitionCard.frameIndex * parent.height
                            // Height only: the width follows the strip's aspect, so each
                            // frame stays a whole cell.
                            sourceSize.height: Math.ceil(parent.height * Screen.devicePixelRatio)
                            fillMode: Image.Stretch
                            asynchronous: true
                            smooth: true
                        }

                        IconGlyph {
                            anchors.centerIn: parent
                            visible: transitionCard.strip.length === 0
                                     || transitionStrip.status === Image.Error
                            glyph: Theme.icons.chevronsRight
                            iconSize: Theme.iconSizeXl
                            iconColor: Theme.transitionOverlap
                        }
                    }

                    Text {
                        width: parent.width
                        text: transitionCard.modelData.label
                        color: Theme.panelForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeCard
                        font.weight: Font.Medium
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }
}
