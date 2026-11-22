import QtQuick
import Drift

// Hover, selection and lock tints behind a header's content.
Item {
    property bool hovered: false
    property bool selected: false
    property bool locked: false
    property real rowHeight: 0

    width: parent.width
    height: rowHeight

    Rectangle {
        anchors.fill: parent
        color: Theme.c.trackHeaderStateTint.locked
        opacity: parent.locked ? Theme.c.trackHeaderStateTint.lockedOpacity : 0
    }
    Rectangle {
        anchors.fill: parent
        color: Theme.c.trackHeaderStateTint.hover
        opacity: parent.hovered ? Theme.c.trackHeaderStateTint.hoverOpacity : 0
    }
    Rectangle {
        anchors.fill: parent
        color: Theme.c.trackHeaderStateTint.selected
        opacity: parent.selected ? Theme.c.trackHeaderStateTint.selectedOpacity : 0
    }
}
