import QtQuick
import Drift

Rectangle {
    property var shading
    readonly property bool phone: Theme.touchInput && Theme.compact
    width: parent.width
    height: phone ? Theme.androidRowHeight : Math.max(Theme.controlHeightSm, header.implicitHeight + 8)
    radius: Theme.c.shadingLayerHeader.radius
    color: Theme.c.shadingLayerHeader.background

    ThemedContextMenu {
        id: layerMenu

        ThemedMenuItem {
            text: qsTr("Bring forward")
            icon.name: Theme.icons.chevronUp
            enabled: shading.position > 0
            onTriggered: EditorState.moveStyleLayer(EditorState.selectedTrack, EditorState.selectedClip,
                                                   shading.layerId, shading.storedIndex + 1)
        }
        ThemedMenuItem {
            text: qsTr("Send backward")
            icon.name: Theme.icons.chevronDown
            enabled: shading.position < shading.count - 1
            onTriggered: EditorState.moveStyleLayer(EditorState.selectedTrack, EditorState.selectedClip,
                                                   shading.layerId, shading.storedIndex - 1)
        }
        ThemedMenuItem {
            text: qsTr("Duplicate layer")
            icon.name: Theme.icons.copy
            onTriggered: EditorState.duplicateStyleLayer(EditorState.selectedTrack, EditorState.selectedClip,
                                                        shading.layerId)
        }
        ThemedMenuSeparator { }
        ThemedMenuItem {
            text: qsTr("Remove layer")
            icon.name: Theme.icons.trash
            onTriggered: EditorState.removeStyleLayer(EditorState.selectedTrack, EditorState.selectedClip,
                                                     shading.layerId)
        }
    }

    Row {
        id: header
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 2
        anchors.rightMargin: 4
        spacing: 2

        IconButton {
            glyph: shading.expanded ? Theme.icons.chevronDown : Theme.icons.chevronRight
            variant: "ghost"
            buttonSize: phone ? Theme.androidMinTouchTarget : 22
            iconSize: phone ? Theme.iconSizeMd : 12
            anchors.verticalCenter: parent.verticalCenter
            tooltip: shading.expanded ? qsTr("Collapse layer") : qsTr("Expand layer")
            onClicked: shading.toggleRequested()
        }
        IconGlyph {
            glyph: shading.kindGlyphs[shading.kind] || Theme.icons.layers
            iconSize: phone ? Theme.iconSizeMd : 12
            iconColor: shading.layerEnabled ? Theme.c.shadingLayerHeader.icon : Theme.c.shadingLayerHeader.iconDisabled
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: (shading.kindLabels[shading.kind] || shading.kind) + " · " + (shading.paintLabels[shading.paintKind] || shading.paintKind)
            color: shading.layerEnabled ? Theme.c.shadingLayerHeader.foreground : Theme.c.shadingLayerHeader.foregroundDisabled
            font.family: Theme.fontFamily
            font.pixelSize: phone ? Theme.fontSizeBase : Theme.fontSizeSm
            font.weight: Font.Medium
            width: phone ? Math.max(20, parent.width - Theme.androidMinTouchTarget * 3 - Theme.iconSizeMd - parent.spacing * 4)
                         : Math.max(20, parent.width - 22 * 6 - 12 - 4 - parent.spacing * 7)
            elide: Text.ElideRight
            anchors.verticalCenter: parent.verticalCenter

            MouseArea {
                anchors.fill: parent
                onClicked: shading.toggleRequested()
            }
        }
        IconButton {
            glyph: Theme.icons.chevronUp
            variant: "ghost"
            buttonSize: 22
            iconSize: 12
            visible: !phone
            enabled: shading.position > 0
            tooltip: qsTr("Bring forward")
            onClicked: EditorState.moveStyleLayer(EditorState.selectedTrack, EditorState.selectedClip,
                                                 shading.layerId, shading.storedIndex + 1)
        }
        IconButton {
            glyph: Theme.icons.chevronDown
            variant: "ghost"
            buttonSize: 22
            iconSize: 12
            visible: !phone
            enabled: shading.position < shading.count - 1
            tooltip: qsTr("Send backward")
            onClicked: EditorState.moveStyleLayer(EditorState.selectedTrack, EditorState.selectedClip,
                                                 shading.layerId, shading.storedIndex - 1)
        }
        IconButton {
            glyph: shading.layerEnabled ? Theme.icons.eye : Theme.icons.eyeOff
            variant: "ghost"
            buttonSize: phone ? Theme.androidMinTouchTarget : 22
            iconSize: phone ? Theme.iconSizeMd : 12
            tooltip: shading.layerEnabled ? qsTr("Hide layer") : qsTr("Show layer")
            onClicked: shading.setLayer({ "enabled": !shading.layerEnabled })
        }
        IconButton {
            glyph: Theme.icons.copy
            variant: "ghost"
            buttonSize: 22
            iconSize: 12
            visible: !phone
            tooltip: qsTr("Duplicate layer")
            onClicked: EditorState.duplicateStyleLayer(EditorState.selectedTrack, EditorState.selectedClip,
                                                      shading.layerId)
        }
        IconButton {
            glyph: Theme.icons.x
            variant: "ghost"
            buttonSize: 22
            iconSize: 12
            visible: !phone
            tooltip: qsTr("Remove layer")
            onClicked: EditorState.removeStyleLayer(EditorState.selectedTrack, EditorState.selectedClip,
                                                   shading.layerId)
        }
        IconButton {
            id: moreButton
            visible: phone
            glyph: Theme.icons.ellipsis
            variant: "ghost"
            buttonSize: Theme.androidMinTouchTarget
            iconSize: Theme.iconSizeMd
            anchors.verticalCenter: parent.verticalCenter
            tooltip: qsTr("More")
            onClicked: layerMenu.popup(moreButton, 0, moreButton.height)
        }
    }
}
