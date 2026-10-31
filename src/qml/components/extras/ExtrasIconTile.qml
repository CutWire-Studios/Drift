import QtQuick
import Drift

Rectangle {
    id: root

    property string icon: ""
    property string accent: ""
    property string label: ""
    property real tileSize: 44

    readonly property bool _hasAccent: /^#[0-9a-fA-F]{6}([0-9a-fA-F]{2})?$/.test(root.accent)
    readonly property color _fg: root._hasAccent ? Theme.c.extrasStore.tileForegroundOnAccent
                                                 : Theme.c.extrasStore.tileForeground

    width: tileSize
    height: tileSize
    radius: Theme.c.extrasStore.tileRadius
    color: root._hasAccent ? root.accent : Theme.c.extrasStore.tileBackground

    ExtrasIcon {
        anchors.centerIn: parent
        visible: root.icon.length > 0
        name: root.icon
        iconSize: Math.round(root.tileSize * 0.5)
        iconColor: root._fg
    }

    Text {
        anchors.centerIn: parent
        visible: root.icon.length === 0
        text: root.label.length > 0 ? root.label.charAt(0).toUpperCase() : ""
        color: root._fg
        font.family: Theme.fontFamily
        font.pixelSize: Math.round(root.tileSize * 0.45)
        font.weight: Font.DemiBold
    }
}
