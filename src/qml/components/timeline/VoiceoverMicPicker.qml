import QtQuick
import Drift

// Microphone name, opening the input-device menu.
Item {
    id: micPickerBtn
    width: Math.min(130, Math.max(70, micPickerRow.implicitWidth + 12))
    height: 24
    anchors.verticalCenter: parent.verticalCenter

    Rectangle {
        anchors.fill: parent
        radius: Theme.c.voiceoverMicPicker.radius
        color: micPickerMouse.containsMouse || micMenu.opened
               ? Theme.c.voiceoverMicPicker.backgroundHover
               : Qt.alpha(Theme.c.voiceoverMicPicker.background, Theme.c.voiceoverMicPicker.backgroundOpacity)
        border.color: Theme.c.voiceoverMicPicker.border
        border.width: Theme.c.voiceoverMicPicker.borderWidth
    }

    Row {
        id: micPickerRow
        anchors.centerIn: parent
        spacing: 4

        IconGlyph {
            glyph: Theme.icons.mic
            iconSize: 12
            iconColor: Theme.c.voiceoverMicPicker.icon
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            text: EditorState.currentMicrophoneName || qsTr("Default Mic")
            font.pixelSize: 11
            color: Theme.c.voiceoverMicPicker.foreground
            elide: Text.ElideRight
            width: Math.min(85, implicitWidth)
            anchors.verticalCenter: parent.verticalCenter
        }

        IconGlyph {
            glyph: Theme.icons.chevronDown
            iconSize: 10
            iconColor: Theme.c.voiceoverMicPicker.iconMuted
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    MouseArea {
        id: micPickerMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: micMenu.popup(0, micPickerBtn.height + 2)
    }

    ThemedContextMenu {
        id: micMenu
        implicitWidth: 220

        Instantiator {
            model: EditorState.availableMicrophones
            delegate: VoiceoverMicMenuItem { }
            onObjectAdded: (index, object) => micMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => micMenu.removeItem(object)
        }
    }

    ThemedToolTip {
        visible: micPickerMouse.containsMouse && !micMenu.opened
        text: qsTr("Input microphone: %1 (click to switch)").arg(EditorState.currentMicrophoneName)
    }
}
