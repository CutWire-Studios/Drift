import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
// One effect preset in the effects browser.
Column {
    id: root
    required property var modelData
    required property real cardSize
    signal applyRequested(string effectId)
    signal deleteRequested(var preset)
    width: cardSize
    spacing: 4
    // Lift on grab: the card dims and grows slightly, so it reads
    // as picked up rather than merely faded.
    opacity: presetDrag.active ? Theme.c.effectPresetCard.dragOpacity : 1
    scale: presetDrag.active ? 1.04 : 1.0

    Behavior on opacity {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }
    Behavior on scale {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }

    readonly property string thumb: root.modelData.thumbnailPath || ""

    Drag.active: presetDrag.active
    Drag.dragType: Drag.Automatic
    Drag.supportedActions: Qt.CopyAction
    Drag.keys: ["application/x-drift-effect"]
    Drag.mimeData: ({ "application/x-drift-effect": root.modelData.id })
    Drag.hotSpot.x: width / 2
    Drag.hotSpot.y: cardSize / 2

    Rectangle {
        width: cardSize
        height: cardSize
        // The slot paints the card surface. This rectangle only clips and scales.
        color: "transparent"
        radius: Theme.c.effectPresetCard.radius
        clip: true
        // Matches the media cards in MediaAssetsTab, which already
        // grow and animate on hover; these snapped.
        scale: cardHover.hovered ? 1.03 : 1.0

        Behavior on scale {
            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }

        ThemeSlot {
            anchors.fill: parent
            z: -1
            slot: "EffectPresetCardBackground"
            control: root
            fallback: Component {
                Rectangle {
                    anchors.fill: parent
                    radius: Theme.c.effectPresetCard.radius
                    color: cardHover.hovered ? Theme.c.effectPresetCard.backgroundHover : Theme.c.effectPresetCard.background
                    border.width: presetDrag.active ? Theme.c.effectPresetCard.borderWidth : 0
                    border.color: Theme.c.effectPresetCard.border

                    Behavior on color {
                        ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                    }
                    Behavior on border.width {
                        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                    }
                }
            }
        }

        HoverHandler { id: cardHover }

        ThemedToolTip {
            text: EditorState.selectedClip >= 0
                  ? qsTr("%1 — click to apply to the selection, or drag onto a clip").arg(root.modelData.label)
                  : qsTr("%1 — click to add as an adjustment layer, or drag onto a clip").arg(root.modelData.label)
            visible: cardHover.hovered && !presetDrag.active
        }

        Image {
            anchors.fill: parent
            visible: root.thumb.length > 0
            source: root.thumb.length > 0
                    ? EditorState.imageUrl(root.thumb) : ""
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            sourceSize: Qt.size(Math.ceil(cardSize * Screen.devicePixelRatio), Math.ceil(cardSize * Screen.devicePixelRatio))
            smooth: true
        }

        // Fallback when no thumbnail is present yet.
        Text {
            anchors.centerIn: parent
            visible: root.thumb.length === 0
            width: parent.width - 12
            text: root.modelData.label
            color: Theme.c.effectPresetCard.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeCard
            font.weight: Font.Medium
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            maximumLineCount: 3
            elide: Text.ElideRight
        }

        TapHandler {
            // Finger taps arrive through TouchLiftArea below, which
            // holds the grab this one would need.
            enabled: !presetDrag.active
            acceptedButtons: Qt.LeftButton
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
            onTapped: if (tapCount === 1) applyRequested(root.modelData.id)
        }

        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: cardMenu.popup()
        }

        DragHandler {
            id: presetDrag
            target: null
            // A finger lifts through TouchDrag instead: a platform
            // drag has no touch gesture and cannot leave the sheet.
            acceptedButtons: Qt.LeftButton
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
        }

        // Hold to carry the preset onto a specific clip; tap still
        // applies it to the selection.
        TouchLiftArea {
            dragKind: "effect"
            payload: root.modelData.id
            label: root.modelData.label
            thumbnail: root.thumb
            glyph: Theme.icons.wand
            onLiftTapped: cardMenu.popup()
        }

        ThemedContextMenu {
            id: cardMenu

            property bool favorited: false

            onAboutToShow: favorited = EditorState.isAssetFavorite("effects", root.modelData.id)

            ThemedMenuItem {
                text: qsTr("Apply to selected clip")
                icon.name: Theme.icons.plus
                enabled: EditorState.selectedClip >= 0
                onTriggered: applyRequested(root.modelData.id)
            }
            ThemedMenuItem {
                text: cardMenu.favorited ? qsTr("Remove from favorites") : qsTr("Add to favorites")
                icon.name: Theme.icons.star
                onTriggered: EditorState.toggleAssetFavorite("effects", root.modelData.id)
            }
            ThemedMenuSeparator { visible: root.modelData.user === true }
            ThemedMenuItem {
                visible: root.modelData.user === true
                text: qsTr("Delete from My Effects")
                icon.name: Theme.icons.trash
                onTriggered: deleteRequested(root.modelData)
            }
        }

        AssetFavoriteButton {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: 3
            tabId: "effects"
            itemId: root.modelData.id
        }

        AssetQuickAddButton {
            z: 5
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 4
            revealed: cardHover.hovered
            tooltip: EditorState.selectedClip >= 0 ? qsTr("Apply to selected clip")
                                                   : qsTr("Add as adjustment layer")
            onClicked: applyRequested(root.modelData.id)
        }

        // Imported effects only: built-in and add-on ones aren't the user's to delete.
        // Top-right, since the quick-add button owns the bottom-right corner.
        IconButton {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 3
            visible: root.modelData.user === true
                     && (cardHover.hovered || Theme.touchUi)
            glyph: Theme.icons.trash
            variant: "ghost"
            buttonSize: 18
            iconSize: 12
            tooltip: qsTr("Delete from My Effects")
            onClicked: deleteRequested(root.modelData)
        }
    }

    Text {
        width: parent.width
        text: root.modelData.label
        color: Theme.c.effectPresetCard.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeCard
        font.weight: Font.Medium
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
        maximumLineCount: 2
        wrapMode: Text.WordWrap
    }

    Text {
        width: parent.width
        visible: root.modelData.compositorOnly === true
        text: qsTr("Built-in")
        color: Theme.c.effectPresetCard.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs - 1
        horizontalAlignment: Text.AlignHCenter
    }
}
