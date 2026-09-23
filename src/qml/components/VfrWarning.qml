import QtQuick
import Drift

// The "!" on media with a variable frame rate, which can drift out of sync with its audio until
// it is converted to an edit-friendly format.
Rectangle {
    id: root

    property real size: Theme.iconSizeBase

    width: size
    height: size
    radius: size / 2
    color: Theme.warning

    Text {
        anchors.centerIn: parent
        text: "!"
        color: Theme.onMedia
        font.pixelSize: root.size * 0.75
        font.family: Theme.fontFamily
        font.weight: Font.Bold
    }

    HoverHandler { id: hover }
    ThemedToolTip {
        visible: hover.hovered
        text: qsTr("Variable frame rate. This clip can drift out of sync with its audio. Right-click it and choose Convert to edit-friendly format.")
    }
}
