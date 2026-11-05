import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
// One transition card. Hover (or touch) scrubs its preview strip.
Column {
    id: root
    required property var modelData
    required property real cardSize
    width: cardSize
    spacing: 4
    // Lift on grab — matches the media and effect cards.
    opacity: transitionDrag.active ? Theme.c.transitionPresetCard.dragOpacity : 1
    scale: transitionDrag.active ? 1.04 : 1.0

    GridView.onPooled: root.scrub = 0.45

    Behavior on opacity {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }
    Behavior on scale {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }

    readonly property string strip: root.modelData.previewStripPath || ""
    readonly property int frameCount: Math.max(1, root.modelData.previewFrames || 1)

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
        running: root.frameCount > 1
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
                root.scrub = 0.45
        }
    }

    Drag.active: transitionDrag.active
    Drag.dragType: Drag.Automatic
    Drag.supportedActions: Qt.CopyAction
    Drag.keys: ["application/x-drift-transition"]
    Drag.mimeData: ({ "application/x-drift-transition": root.modelData.kind })
    Drag.hotSpot.x: width / 2
    Drag.hotSpot.y: cardSize / 2

    Rectangle {
        width: cardSize
        height: cardSize
        radius: Theme.c.transitionPresetCard.radius
        color: transitionHover.hovered ? Theme.c.transitionPresetCard.backgroundHover : Theme.c.transitionPresetCard.background
        border.width: transitionDrag.active ? Theme.c.transitionPresetCard.borderWidth : 0
        border.color: Theme.c.transitionPresetCard.border
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
            text: qsTr("%1 — drag onto where two clips meet, or on touch hold then drag").arg(root.modelData.label)
            visible: transitionHover.hovered
        }

        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: cardMenu.popup()
        }

        DragHandler {
            id: transitionDrag
            target: null
            // A finger lifts through TouchDrag instead: a platform
            // drag has no touch gesture and cannot leave the sheet.
            acceptedButtons: Qt.LeftButton
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
            onActiveChanged: {
                if (active)
                    AssetDrag.begin("transition", root.modelData.kind, root.modelData.label)
                else
                    AssetDrag.end()
            }
        }

        // Hold to carry the transition onto the join between two
        // clips. This used to be a tap that applied to whatever was
        // selected, which gave no say over which boundary it landed on.
        TouchLiftArea {
            dragKind: "transition"
            payload: root.modelData.kind
            label: root.modelData.label
            glyph: Theme.icons.chevronsRight
            onLiftTapped: cardMenu.popup()
        }

        ThemedContextMenu {
            id: cardMenu

            property bool favorited: false

            onAboutToShow: favorited = EditorState.isAssetFavorite("transitions", root.modelData.kind)

            ThemedMenuItem {
                text: cardMenu.favorited ? qsTr("Remove from favorites") : qsTr("Add to favorites")
                icon.name: Theme.icons.star
                onTriggered: EditorState.toggleAssetFavorite("transitions", root.modelData.kind)
            }
        }

        AssetFavoriteButton {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 3
            tabId: "transitions"
            itemId: root.modelData.kind
        }

        SkeletonBox {
            anchors.fill: parent
            visible: root.strip.length > 0
                     && transitionStrip.status === Image.Loading
        }

        // The strip is one row of square cells; slide it rather than
        // re-decoding a sourceClipRect per frame.
        Image {
            id: transitionStrip
            visible: root.strip.length > 0
                     && status === Image.Ready
            source: root.strip.length > 0
                    ? EditorState.imageUrl(root.strip) : ""
            height: parent.height
            width: parent.height * root.frameCount
            x: -root.frameIndex * parent.height
            // Height only: the width follows the strip's aspect, so each
            // frame stays a whole cell.
            sourceSize.height: Math.ceil(parent.height * Screen.devicePixelRatio)
            fillMode: Image.Stretch
            asynchronous: true
            smooth: true
        }

        IconGlyph {
            anchors.centerIn: parent
            visible: root.strip.length === 0
                     || transitionStrip.status === Image.Error
            glyph: Theme.icons.chevronsRight
            iconSize: Theme.iconSizeXl
            iconColor: Theme.c.transitionPresetCard.icon
        }
    }

    Text {
        width: parent.width
        text: root.modelData.label
        color: Theme.c.transitionPresetCard.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeCard
        font.weight: Font.Medium
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        maximumLineCount: 2
        elide: Text.ElideRight
    }
}
