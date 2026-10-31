import QtQuick
import Drift
// One listing as a card. The presentation follows mediaKind: stock video and photos use the
// stock tile, Drift Assets their own shape-aware card, and everything else (community add-ons,
// audio) a plain thumbnail card. Tapping opens the detail; an install-state badge sits on the
// card, except on Drift Assets which draw their own.
Item {
    id: root

    required property var item
    property real cardWidth: Theme.assetCardWidth
    property real previewHeight: 88
    property bool compact: Theme.compact
    property bool autoplay: false
    property bool justAdded: false

    signal openRequested()
    signal actionRequested()

    readonly property string kind: item.mediaKind || ""
    readonly property bool isAsset: kind === "lottie" || kind === "model3d" || kind === "face-prop"
    readonly property bool isStock: kind === "video" || kind === "image"
    readonly property string installState: {
        void MarketInstall.revision
        return MarketInstall.installState(item)
    }
    readonly property bool badgeShown: !isAsset
                                       && (installState === "installed" || installState === "update"
                                           || installState === "failed"
                                           || (installState === "installing" && !isStock))

    implicitWidth: loader.item ? loader.item.width : cardWidth
    implicitHeight: loader.item ? loader.item.implicitHeight : 0
    width: implicitWidth
    height: implicitHeight
    Accessible.role: Accessible.Button
    Accessible.name: item.title || ""

    function glyphFor(mediaKind) {
        switch (mediaKind) {
        case "audio":
        case "music": return Theme.icons.music
        case "effect": return Theme.icons.wand
        case "transition": return Theme.icons.chevronsRight
        case "audio-effect": return Theme.icons.audioLines
        case "look": return Theme.icons.sparkles
        case "theme": return Theme.icons.sun
        case "effect-template": return Theme.icons.layers
        case "image": return Theme.icons.image
        case "video": return Theme.icons.film
        }
        return Theme.icons.package
    }

    Loader {
        id: loader
        sourceComponent: root.isAsset ? assetCard : (root.isStock ? stockCard : plainCard)
    }

    Component {
        id: assetCard
        DriftAssetCard {
            asset: root.item
            previewHeight: root.previewHeight
            compact: root.compact
            autoplay: root.autoplay
            justAdded: root.justAdded
            actionLabel: root.kind === "face-prop" ? qsTr("Install") : qsTr("Add")
            onOpenRequested: root.openRequested()
            onActionRequested: root.actionRequested()
        }
    }

    Component {
        id: stockCard
        Column {
            readonly property real aspect: {
                const w = Number(root.item.width || 0), h = Number(root.item.height || 0)
                return root.kind === "image" && w > 0 && h > 0 ? Math.min(1.6, Math.max(0.6, h / w)) : 9 / 16
            }
            width: root.cardWidth
            spacing: Theme.spacingXs

            StockThumb {
                width: parent.width
                height: width * parent.aspect
                item: root.item
                compact: root.compact
                onOpenRequested: root.openRequested()
            }
            Text {
                width: parent.width
                text: root.item.title || ""
                color: Theme.c.marketplace.cardForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                elide: Text.ElideRight
                maximumLineCount: 2
                wrapMode: Text.WordWrap
            }
        }
    }

    Component {
        id: plainCard
        Column {
            width: root.cardWidth
            spacing: Theme.spacingXs

            Rectangle {
                width: parent.width
                height: width * 9 / 16
                radius: Theme.c.marketplace.cardRadius
                color: hover.hovered ? Theme.c.marketplace.cardBackgroundHover : Theme.c.marketplace.cardBackground
                clip: true

                IconGlyph {
                    anchors.centerIn: parent
                    visible: thumb.status !== Image.Ready
                    glyph: root.glyphFor(root.kind)
                    iconSize: Theme.iconSizeXl
                    iconColor: Theme.c.marketplace.cardIcon
                }
                Image {
                    id: thumb
                    anchors.fill: parent
                    source: root.item.thumbUrl || ""
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    sourceSize.width: Math.max(1, Math.round(parent.width))
                    opacity: status === Image.Ready ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: Theme.durationBase; easing.type: Theme.easing } }
                }
                HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: root.openRequested() }
            }
            Text {
                width: parent.width
                text: root.item.title || ""
                color: Theme.c.marketplace.cardForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                font.weight: Font.Medium
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                visible: text.length > 0
                text: root.item.subtitle || ""
                color: Theme.c.marketplace.cardForegroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                elide: Text.ElideRight
            }
        }
    }

    Rectangle {
        visible: root.badgeShown
        x: Theme.spacingSm
        y: Theme.spacingSm
        width: Theme.iconSizeMd + Theme.spacingSm
        height: width
        radius: Theme.c.marketplace.stateBadgeRadius
        color: Theme.c.marketplace.stateBadgeBackground

        IconGlyph {
            anchors.centerIn: parent
            glyph: root.installState === "failed" ? Theme.icons.error
                   : root.installState === "installing" ? Theme.icons.spinner
                   : root.installState === "update" ? Theme.icons.download : Theme.icons.check
            iconSize: Theme.iconSizeMd
            iconColor: root.installState === "failed" ? Theme.c.marketplace.stateFailed
                       : root.installState === "update" ? Theme.c.marketplace.stateUpdate
                       : Theme.c.marketplace.stateInstalled
        }
    }
}
