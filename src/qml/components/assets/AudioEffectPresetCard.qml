import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
// One audio effect preset in the audio FX browser.
Column {
    id: root
    required property var modelData
    required property real cardSize
    signal applyRequested(string effectId)
    width: cardSize
    spacing: 4
    opacity: presetDrag.active ? Theme.c.audioEffectPresetCard.dragOpacity : 1
    scale: presetDrag.active ? 1.04 : 1.0

    Behavior on opacity {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }
    Behavior on scale {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }

    readonly property string thumb: root.modelData.thumbnailPath || ""
    readonly property string iconGlyph: root.modelData.icon || "audio-lines"

    Drag.active: presetDrag.active
    Drag.dragType: Drag.Automatic
    Drag.supportedActions: Qt.CopyAction
    Drag.keys: ["application/x-drift-audio-effect"]
    Drag.mimeData: ({ "application/x-drift-audio-effect": root.modelData.id })
    Drag.hotSpot.x: width / 2
    Drag.hotSpot.y: cardSize / 2

    Rectangle {
        width: cardSize
        height: cardSize
        radius: Theme.c.audioEffectPresetCard.radius
        color: cardHover.hovered ? Theme.c.audioEffectPresetCard.backgroundHover : Theme.c.audioEffectPresetCard.background
        border.width: presetDrag.active ? Theme.c.audioEffectPresetCard.borderWidth : 0
        border.color: Theme.c.audioEffectPresetCard.border
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

        ThemedToolTip {
            text: EditorState.selectedClip >= 0
                  ? qsTr("%1 — click to apply to the selection, or drag onto a clip").arg(root.modelData.label)
                  : qsTr("%1 — drag onto a clip in the timeline").arg(root.modelData.label)
            visible: cardHover.hovered && !presetDrag.active
        }

        Image {
            id: presetThumb
            anchors.fill: parent
            visible: root.thumb.length > 0 && status === Image.Ready
            source: root.thumb.length > 0
                    ? EditorState.imageUrl(root.thumb) : ""
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            sourceSize: Qt.size(Math.ceil(cardSize * Screen.devicePixelRatio), Math.ceil(cardSize * Screen.devicePixelRatio))
            smooth: true
        }

        IconGlyph {
            anchors.centerIn: parent
            visible: root.thumb.length === 0
                     || presetThumb.status === Image.Error
            glyph: root.iconGlyph
            iconSize: 28
            iconColor: Theme.c.audioEffectPresetCard.icon
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
            dragKind: "audioEffect"
            payload: root.modelData.id
            label: root.modelData.label
            thumbnail: root.thumb
            glyph: root.iconGlyph
            onLiftTapped: cardMenu.popup()
        }

        ThemedContextMenu {
            id: cardMenu

            property bool favorited: false

            onAboutToShow: favorited = EditorState.isAssetFavorite("sounds", root.modelData.id)

            ThemedMenuItem {
                text: qsTr("Apply to selected clip")
                icon.name: Theme.icons.plus
                enabled: EditorState.selectedClip >= 0
                onTriggered: applyRequested(root.modelData.id)
            }
            ThemedMenuItem {
                text: cardMenu.favorited ? qsTr("Remove from favorites") : qsTr("Add to favorites")
                icon.name: Theme.icons.star
                onTriggered: EditorState.toggleAssetFavorite("sounds", root.modelData.id)
            }
        }

        AssetFavoriteButton {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: 3
            tabId: "sounds"
            itemId: root.modelData.id
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
    }

    Text {
        width: parent.width
        text: root.modelData.label
        color: Theme.c.audioEffectPresetCard.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeCard
        font.weight: Font.Medium
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
        maximumLineCount: 2
        wrapMode: Text.WordWrap
    }
}
