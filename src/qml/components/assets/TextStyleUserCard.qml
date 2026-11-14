import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
// One user-saved text style card.
Column {
    id: root
    required property var modelData
    required property real cardSize
    signal added()
    signal exportRequested(var preset)
    signal renameRequested(var preset)
    signal deleteRequested(var preset)
    width: cardSize
    spacing: Theme.spacingSm

    opacity: userDrag.active ? Theme.c.textStyleUserCard.dragOpacity : 1
    scale: userDrag.active ? 1.04 : userDrag.pressed ? 0.97 : (userDrag.hovered ? 1.02 : 1.0)
    Behavior on scale {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }

    TextStylePackThumb {
        width: parent.width
        height: Math.round(width * 0.55)
        presetId: root.modelData.id
        hovered: userDrag.hovered

        AssetDragSource {
            id: userDrag
            anchors.fill: parent
            kind: "textStyle"
            payload: root.modelData.id
            label: root.modelData.label
            glyph: Theme.icons.type
            hasContextMenu: true
            onContextMenuRequested: cardMenu.popup()
            onTapped: {
                EditorState.addTextClip("", -1, root.modelData.id)
                added()
            }
        }

        ThemedToolTip {
            text: qsTr("%1 — click to add at the playhead, or drag to the timeline. Double-click it on the preview to edit.").arg(root.modelData.label)
            visible: userDrag.hovered && !userDrag.active
        }

        IconButton {
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 2
            visible: userDrag.hovered || cardMenu.visible
            glyph: Theme.icons.ellipsis
            variant: "ghost"
            buttonSize: 20
            iconSize: 12
            tooltip: qsTr("Style options")
            onClicked: cardMenu.popup()
        }

        AssetQuickAddButton {
            z: 5
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 4
            revealed: userDrag.hovered
            tooltip: qsTr("Add at playhead")
            onClicked: userDrag.tapped(1)
        }

        ThemedContextMenu {
            id: cardMenu

            ThemedMenuItem {
                text: qsTr("Rename…")
                icon.name: Theme.icons.pencil
                onTriggered: renameRequested(root.modelData)
            }
            ThemedMenuItem {
                text: qsTr("Export…")
                icon.name: Theme.icons.folderOutput
                onTriggered: exportRequested(root.modelData)
            }
            ThemedMenuSeparator { }
            ThemedMenuItem {
                text: qsTr("Delete")
                icon.name: Theme.icons.trash
                onTriggered: deleteRequested(root.modelData)
            }
        }
    }

    Text {
        width: parent.width
        text: root.modelData.label
        elide: Text.ElideRight
        horizontalAlignment: Text.AlignHCenter
        color: userDrag.hovered ? Theme.c.textStyleUserCard.foregroundHover : Theme.c.textStyleUserCard.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs

        Behavior on color {
            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }
    }
}
