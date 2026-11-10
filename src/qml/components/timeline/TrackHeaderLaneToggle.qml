import QtQuick
import Drift

Item {
    id: toggle

    required property var row
    readonly property bool collapsed: row.lanesCollapsed

    width: 14 + (collapsed ? badge.width + 4 : 0)

    IconGlyph {
        x: 0
        anchors.verticalCenter: parent.verticalCenter
        glyph: toggle.collapsed ? Theme.icons.chevronRight : Theme.icons.chevronDown
        iconSize: 12
        iconColor: toggleMouse.containsMouse ? Theme.c.trackHeaderLaneToggle.foregroundHover
                                             : Theme.c.trackHeaderLaneToggle.foreground
    }

    Rectangle {
        id: badge
        visible: toggle.collapsed
        x: 14
        anchors.verticalCenter: parent.verticalCenter
        width: Math.max(height, badgeText.implicitWidth + 6)
        height: 12
        radius: height / 2
        color: Theme.c.trackHeaderLaneToggle.badgeBackground

        Text {
            id: badgeText
            anchors.centerIn: parent
            text: toggle.row.laneCount
            color: Theme.c.trackHeaderLaneToggle.badgeForeground
            font.family: Theme.fontFamily
            font.pixelSize: 9
        }
    }

    Accessible.role: Accessible.Button
    Accessible.name: toggle.collapsed ? qsTr("Expand child lanes") : qsTr("Collapse child lanes")
    Accessible.onPressAction: toggleMouse.clicked(null)

    MouseArea {
        id: toggleMouse
        anchors.fill: parent
        anchors.margins: -2
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: EditorState.setTrackLanesCollapsed(toggle.row.index, !toggle.collapsed)

        ThemedToolTip {
            visible: toggleMouse.containsMouse
            text: toggle.collapsed ? qsTr("Expand child lanes") : qsTr("Collapse child lanes")
        }
    }
}
