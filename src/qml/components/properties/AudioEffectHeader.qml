import QtQuick
import Drift

Rectangle {
    property var card
    property var inspector
    readonly property bool phone: Theme.touchInput && Theme.compact
    width: parent.width
    height: phone ? Theme.androidRowHeight : audioEffectHeader.implicitHeight + 8
    radius: Theme.c.audioEffectHeader.radius
    color: Theme.c.audioEffectHeader.background

    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: audioEffectCardMenu.popup()
    }
    TapHandler {
        acceptedDevices: PointerDevice.TouchScreen
        onLongPressed: audioEffectCardMenu.popup(parent, point.position.x, point.position.y)
    }
    ThemedContextMenu {
        id: audioEffectCardMenu

        property bool editable: false
        onAboutToShow: editable = AudioForge.canEdit("audio_effect", card.effectData.catalogId)

        ThemedMenuItem {
            text: card.effectEnabled ? qsTr("Disable effect") : qsTr("Enable effect")
            icon.name: card.effectEnabled ? Theme.icons.eyeOff : Theme.icons.eye
            onTriggered: EditorState.setAudioEffectEnabled(
                             EditorState.selectedTrack, EditorState.selectedClip,
                             card.index, !card.effectEnabled)
        }
        ThemedMenuItem {
            text: qsTr("Move up")
            icon.name: Theme.icons.chevronUp
            enabled: card.index > 0
            onTriggered: EditorState.moveAudioEffect(
                             EditorState.selectedTrack, EditorState.selectedClip,
                             card.index, card.index - 1)
        }
        ThemedMenuItem {
            text: qsTr("Move down")
            icon.name: Theme.icons.chevronDown
            enabled: card.index < inspector.selectedAudioEffects.length - 1
            onTriggered: EditorState.moveAudioEffect(
                             EditorState.selectedTrack, EditorState.selectedClip,
                             card.index, card.index + 1)
        }
        ThemedMenuItem {
            text: qsTr("Copy effect")
            icon.name: Theme.icons.copy
            onTriggered: EditorState.copyAudioEffectToClipboard(
                             EditorState.selectedTrack, EditorState.selectedClip,
                             card.index)
        }
        ThemedMenuSeparator {
            visible: audioForgeItem.visible
        }
        ThemedMenuItem {
            id: audioForgeItem
            visible: audioEffectCardMenu.editable && !ShellBridge.phoneShell
            text: qsTr("Edit in Forge")
            icon.name: Theme.icons.pencil
            onTriggered: AudioForge.openPackage("audio_effect", card.effectData.catalogId)
        }
        ThemedMenuSeparator { }
        ThemedMenuItem {
            text: qsTr("Remove effect")
            icon.name: Theme.icons.trash
            onTriggered: EditorState.removeAudioEffect(
                             EditorState.selectedTrack, EditorState.selectedClip,
                             card.index)
        }
    }

    Row {
        id: audioEffectHeader
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 8
        anchors.rightMargin: 4
        spacing: 2

        IconGlyph {
            anchors.verticalCenter: parent.verticalCenter
            glyph: card.effectData.icon || "audio-lines"
            iconSize: 14
            iconColor: Theme.c.audioEffectHeader.icon
            opacity: card.effectEnabled ? 1 : 0.5
        }
        Text {
            text: card.effectData.missing
                  ? qsTr("%1 (not installed)").arg(card.effectData.label)
                  : card.effectData.label
            color: card.effectEnabled
                   ? Theme.c.audioEffectHeader.foreground : Theme.c.audioEffectHeader.foregroundDisabled
            font.family: Theme.fontFamily
            font.pixelSize: phone ? Theme.fontSizeBase : Theme.fontSizeSm
            font.weight: Font.Medium
            width: parent.width - (phone ? Theme.androidMinTouchTarget * 2 : 22 * 5) - 20 - 8
            elide: Text.ElideRight
            anchors.verticalCenter: parent.verticalCenter
        }
        IconButton {
            glyph: Theme.icons.chevronUp
            variant: "ghost"
            buttonSize: 22
            iconSize: 12
            enabled: card.index > 0
            visible: !phone
            tooltip: qsTr("Move audio effect up")
            onClicked: EditorState.moveAudioEffect(
                           EditorState.selectedTrack, EditorState.selectedClip,
                           card.index, card.index - 1)
        }
        IconButton {
            glyph: Theme.icons.chevronDown
            variant: "ghost"
            buttonSize: 22
            iconSize: 12
            enabled: card.index < inspector.selectedAudioEffects.length - 1
            visible: !phone
            tooltip: qsTr("Move audio effect down")
            onClicked: EditorState.moveAudioEffect(
                           EditorState.selectedTrack, EditorState.selectedClip,
                           card.index, card.index + 1)
        }
        IconButton {
            glyph: card.effectEnabled ? Theme.icons.eye : Theme.icons.eyeOff
            variant: "ghost"
            buttonSize: phone ? Theme.androidMinTouchTarget : 22
            iconSize: phone ? Theme.iconSizeMd : 12
            tooltip: card.effectEnabled
                     ? qsTr("Disable audio effect") : qsTr("Enable audio effect")
            onClicked: EditorState.setAudioEffectEnabled(
                           EditorState.selectedTrack, EditorState.selectedClip,
                           card.index, !card.effectEnabled)
        }
        IconButton {
            glyph: Theme.icons.copy
            variant: "ghost"
            buttonSize: 22
            iconSize: 12
            visible: !phone
            tooltip: qsTr("Copy this audio effect")
            onClicked: EditorState.copyAudioEffectToClipboard(
                           EditorState.selectedTrack, EditorState.selectedClip,
                           card.index)
        }
        IconButton {
            glyph: Theme.icons.x
            variant: "ghost"
            buttonSize: 22
            iconSize: 12
            visible: !phone
            tooltip: qsTr("Remove audio effect")
            onClicked: EditorState.removeAudioEffect(
                           EditorState.selectedTrack, EditorState.selectedClip,
                           card.index)
        }
        IconButton {
            id: moreButton
            visible: phone
            glyph: Theme.icons.ellipsis
            variant: "ghost"
            buttonSize: Theme.androidMinTouchTarget
            iconSize: Theme.iconSizeMd
            tooltip: qsTr("More")
            onClicked: audioEffectCardMenu.popup(moreButton, 0, moreButton.height)
        }
    }
}
