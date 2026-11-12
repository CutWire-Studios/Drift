import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
// One effect-stack template card, with a thumbnail mosaic.
Column {
    id: root
    required property var modelData
    required property real cardSize
    signal applyRequested(string templateId)
    width: cardSize
    spacing: 4

    readonly property var effectThumbs: root.modelData.effectThumbnails || []
    readonly property int effectCount: root.modelData.effectCount || effectThumbs.length
    readonly property int mosaicCells: Math.min(4, Math.max(1, effectThumbs.length))
    readonly property int mosaicColumns: mosaicCells <= 1 ? 1 : 2
    readonly property int mosaicRows: mosaicCells <= 2 ? 1 : 2

    Rectangle {
        width: cardSize
        height: cardSize
        radius: Theme.c.effectTemplateCard.radius
        color: cardHover.hovered ? Theme.c.effectTemplateCard.backgroundHover : Theme.c.effectTemplateCard.background
        border.width: Theme.c.effectTemplateCard.borderWidth
        border.color: Qt.rgba(Theme.c.effectTemplateCard.accent.r, Theme.c.effectTemplateCard.accent.g, Theme.c.effectTemplateCard.accent.b, 0.35)
        clip: true
        // Matches the media cards in MediaAssetsTab; these snapped.
        scale: cardHover.hovered ? 1.03 : 1.0

        Behavior on color {
            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }
        Behavior on scale {
            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }

        HoverHandler { id: cardHover }

        ThemedToolTip {
            text: EditorState.selectedClip >= 0
                  ? qsTr("%1 — click to apply music-synced effects to the selection").arg(root.modelData.label)
                  : qsTr("%1 — select a clip, then click to apply").arg(root.modelData.label)
            visible: cardHover.hovered && !templateDrag.active
        }

        Grid {
            id: thumbMosaic
            anchors.fill: parent
            columns: root.mosaicColumns
            rows: root.mosaicRows

            Repeater {
                model: root.mosaicCells
                delegate: Item {
                    required property int index
                    width: thumbMosaic.width / root.mosaicColumns
                    height: thumbMosaic.height / root.mosaicRows

                    Image {
                        anchors.fill: parent
                        visible: index < root.effectThumbs.length
                        source: visible
                                ? EditorState.imageUrl(root.effectThumbs[index])
                                : ""
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        sourceSize: Qt.size(Math.ceil(cardSize * Screen.devicePixelRatio), Math.ceil(cardSize * Screen.devicePixelRatio))
                        smooth: true
                    }

                    Rectangle {
                        anchors.fill: parent
                        visible: index === 3 && root.effectCount > 4
                        color: Theme.c.effectTemplateCard.scrim

                        Text {
                            anchors.centerIn: parent
                            text: "+" + (root.effectCount - 3)
                            color: Theme.c.effectTemplateCard.foregroundOnMedia
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeCard
                            font.weight: Font.DemiBold
                        }
                    }

                    Rectangle {
                        anchors.right: parent.right
                        width: Theme.c.effectTemplateCard.mosaicDividerWidth
                        height: parent.height
                        color: Theme.c.effectTemplateCard.mosaicDivider
                        visible: index % root.mosaicColumns
                                  !== root.mosaicColumns - 1
                    }
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: Theme.c.effectTemplateCard.mosaicDividerWidth
                        color: Theme.c.effectTemplateCard.mosaicDivider
                        visible: index < root.mosaicCells - root.mosaicColumns
                    }
                }
            }
        }

        Text {
            anchors.centerIn: parent
            visible: root.effectThumbs.length === 0
            width: parent.width - 12
            text: root.modelData.label
            color: Theme.c.effectTemplateCard.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeCard
            font.weight: Font.Medium
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            maximumLineCount: 3
            elide: Text.ElideRight
        }

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: 4
            width: stackBadgeRow.implicitWidth + 8
            height: stackBadgeRow.implicitHeight + 4
            radius: Theme.c.effectTemplateCard.badgeRadius
            color: Theme.c.effectTemplateCard.scrim

            Row {
                id: stackBadgeRow
                anchors.centerIn: parent
                spacing: 3

                IconGlyph {
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: Theme.icons.layers
                    iconSize: 10
                    iconColor: Theme.c.effectTemplateCard.foregroundOnMedia
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.effectCount > 0 ? root.effectCount : "?"
                    color: Theme.c.effectTemplateCard.foregroundOnMedia
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    font.weight: Font.Medium
                }
            }
        }

        // Tap applies to the selected clip; drag to apply it to any clip, on the
        // timeline or in the preview.
        AssetDragSource {
            id: templateDrag
            hasContextMenu: true
            onContextMenuRequested: cardMenu.popup()
            anchors.fill: parent
            kind: "template"
            payload: root.modelData.id
            label: root.modelData.label
            glyph: Theme.icons.layers
            onTapped: if (tapCount === 1) applyRequested(root.modelData.id)
        }

        ThemedContextMenu {
            id: cardMenu

            property bool favorited: false

            onAboutToShow: favorited = EditorState.isAssetFavorite("templates", root.modelData.id)

            ThemedMenuItem {
                text: qsTr("Apply to selected clip")
                icon.name: Theme.icons.plus
                enabled: EditorState.selectedClip >= 0
                onTriggered: templateDrag.tapped()
            }
            ThemedMenuItem {
                text: cardMenu.favorited ? qsTr("Remove from favorites") : qsTr("Add to favorites")
                icon.name: Theme.icons.star
                onTriggered: EditorState.toggleAssetFavorite("templates", root.modelData.id)
            }
        }

        Rectangle {
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.margins: 4
            visible: root.modelData.requiresSegmentation === true
            width: segBadge.implicitWidth + 8
            height: segBadge.implicitHeight + 4
            radius: Theme.c.effectTemplateCard.badgeRadius
            color: Theme.c.effectTemplateCard.scrim

            Text {
                id: segBadge
                anchors.centerIn: parent
                text: qsTr("Needs cutout")
                color: Theme.c.effectTemplateCard.foregroundOnMedia
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                font.weight: Font.Medium
            }
        }

        AssetQuickAddButton {
            z: 5
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 4
            revealed: cardHover.hovered
            tooltip: qsTr("Apply to selected clip")
            enabled: EditorState.selectedClip >= 0
            onClicked: applyRequested(root.modelData.id)
        }

        AssetFavoriteButton {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 3
            tabId: "templates"
            itemId: root.modelData.id
        }
    }

    Text {
        width: parent.width
        text: root.modelData.label
        color: Theme.c.effectTemplateCard.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeCard
        font.weight: Font.Medium
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
        maximumLineCount: 2
        wrapMode: Text.WordWrap
    }
}
