import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
// One saved effect-stack preset.
Column {
    id: root
    required property var modelData
    required property real cardSize
    signal applyRequested(var preset)
    signal exportRequested(var preset)
    signal renameRequested(var preset)
    signal deleteRequested(var preset)
    width: cardSize
    spacing: Theme.spacingSm

    opacity: cardDrag.active ? Theme.c.effectStackCard.dragOpacity : 1
    scale: cardDrag.active ? 1.04 : cardDrag.pressed ? 0.97 : (cardDrag.hovered ? 1.02 : 1.0)
    Behavior on scale {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }

    Rectangle {
        width: parent.width
        height: Math.round(width * 0.55)
        radius: Theme.c.effectStackCard.radius
        color: cardDrag.hovered ? Theme.c.effectStackCard.backgroundHover : Theme.c.effectStackCard.background
        border.width: Theme.c.effectStackCard.borderWidth
        border.color: Theme.c.effectStackCard.border

        Behavior on color {
            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }

        AssetDragSource {
            id: cardDrag
            anchors.fill: parent
            kind: "effectStack"
            payload: root.modelData.id
            label: root.modelData.label || ""
            glyph: Theme.icons.layers
            hasContextMenu: true
            onContextMenuRequested: stackMenu.popup()
            onTapped: if (tapCount === 1) applyRequested(root.modelData)
        }

        // There is no rendered preview for a stack, so the card says what is in it.
        Column {
            anchors.fill: parent
            anchors.margins: Theme.spacingSm
            spacing: 2

            Repeater {
                model: Math.min(3, (root.modelData.labels || []).length)
                delegate: Text {
                    required property int index
                    width: parent.width
                    text: root.modelData.labels[index]
                    elide: Text.ElideRight
                    color: Theme.c.effectStackCard.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
            }

            Text {
                width: parent.width
                visible: (root.modelData.labels || []).length > 3
                text: qsTr("+%1 more").arg(
                          (root.modelData.labels || []).length - 3)
                color: Theme.c.effectStackCard.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
        }

        AssetQuickAddButton {
            z: 5
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 4
            revealed: cardDrag.hovered
            tooltip: qsTr("Apply to selected clip")
            enabled: EditorState.selectedClip >= 0
            onClicked: applyRequested(root.modelData)
        }

        IconButton {
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 2
            visible: cardDrag.hovered || stackMenu.visible
            glyph: Theme.icons.ellipsis
            variant: "ghost"
            buttonSize: 20
            iconSize: 12
            tooltip: qsTr("Preset options")
            onClicked: stackMenu.popup()
        }

        ThemedContextMenu {
            id: stackMenu

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
        color: cardDrag.hovered ? Theme.c.effectStackCard.foregroundHover : Theme.c.effectStackCard.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs

        Behavior on color {
            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }
    }
}
