import QtQuick
import Drift

// A small label on media in the bin and on timeline clips ("Proxy", "Edit-friendly"), shared so
// the two surfaces read as the same thing. `waiting` is the queued/building look: outlined rather
// than filled.
Rectangle {
    id: root

    property string text
    property color accent: Theme.clipProxy
    property color foreground: Theme.clipProxyForeground
    property bool waiting: false
    property real fontSize: Theme.fontSizeXs
    property string tooltip: ""

    implicitWidth: label.implicitWidth + Theme.spacingMd
    implicitHeight: label.implicitHeight + 2
    radius: Theme.radiusXs
    color: waiting ? "transparent" : accent
    border.width: waiting ? 1 : 0
    border.color: accent

    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        color: root.waiting ? root.accent : root.foreground
        font.pixelSize: root.fontSize
        font.family: Theme.fontFamily
        font.weight: Font.DemiBold
    }

    HoverHandler { id: hover }
    // A tooltip is a Popup; built only while hovered, not once per pill on every clip and card.
    Loader {
        active: hover.hovered && root.tooltip.length > 0
        sourceComponent: ThemedToolTip {
            visible: true
            text: root.tooltip
        }
    }
}
