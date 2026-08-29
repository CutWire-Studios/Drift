import QtQuick
import QtQuick.Controls.Basic
import Drift

// Themed entry for ThemedContextMenu.
//
// A Menu's `delegate` is only applied to Action-based items, not to MenuItems
// declared inline as menu children — those fall back to the Basic style, whose
// default text colour is near-invisible against the dark menu background. Using
// this component gives inline entries the themed contentItem/background directly.
MenuItem {
    id: root

    // A Menu lays entries out in a ListView, which still reserves a row for a
    // hidden item, so conditional entries left blank gaps behind.
    implicitHeight: visible ? Theme.controlHeightSm + Theme.spacingSm : 0
    height: implicitHeight
    hoverEnabled: true

    contentItem: Row {
        spacing: Theme.spacingLg
        leftPadding: Theme.spacingLg
        rightPadding: Theme.spacingLg

        IconGlyph {
            anchors.verticalCenter: parent.verticalCenter
            visible: root.icon.name.length > 0
            width: visible ? implicitWidth : 0
            glyph: root.icon.name
            iconSize: Theme.iconSizeMd
            iconColor: root.enabled ? Theme.panelForeground : Theme.mutedForeground
            opacity: root.enabled ? 1 : 0.5
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            color: Theme.panelForeground
            opacity: root.enabled ? 1 : 0.5
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            elide: Text.ElideRight
        }
    }

    background: Rectangle {
        radius: Theme.radiusXs
        color: root.highlighted && root.enabled ? Theme.panelAccent : "transparent"

        Behavior on color {
            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        cursorShape: root.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onWheel: (wheel) => { wheel.accepted = false }
    }
}
