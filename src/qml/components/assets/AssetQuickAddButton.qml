import QtQuick
import QtQuick.Controls.Basic
import Drift

// Round "+" that performs an asset item's primary add/apply action in one click.
// Visible size stays small; on touch only the area that accepts a finger grows.
Button {
    id: root

    property string tooltip: qsTr("Add to timeline")
    // Cards pass their hover state; a touch UI has no hover so it is always shown.
    // Faded rather than hidden so the button stays reachable by Tab.
    property bool revealed: false
    // Swaps the cross for a check to confirm the action just ran.
    property bool done: false

    readonly property real _visualSize: Theme.touchInput ? 30 : 24
    readonly property real _touchPad: Theme.touchInput
                                      ? Math.max(0, (Theme.androidMinTouchTarget - _visualSize) / 2)
                                      : 0

    implicitWidth: _visualSize
    implicitHeight: _visualSize
    width: implicitWidth
    height: implicitHeight
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    containmentMask: _touchPad > 0 ? touchHitArea : null

    Accessible.role: Accessible.Button
    Accessible.name: root.tooltip
    Accessible.onPressAction: root.clicked()

    opacity: revealed || Theme.touchUi || visualFocus ? 1.0 : 0.0
    Behavior on opacity {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }
    scale: !enabled ? 1.0 : down ? 0.92 : hovered ? 1.1 : 1.0
    Behavior on scale {
        NumberAnimation { duration: Theme.durationPress; easing.type: Theme.easing }
    }

    Item {
        id: touchHitArea
        anchors.fill: parent
        anchors.margins: -root._touchPad
    }

    background: Rectangle {
        radius: Theme.radiusPill
        color: !root.enabled ? Theme.c.assetQuickAddButton.backgroundDisabled
               : root.down ? Theme.c.assetQuickAddButton.backgroundPressed
               : root.hovered ? Theme.c.assetQuickAddButton.backgroundHover
                              : Theme.c.assetQuickAddButton.background
        border.width: root.visualFocus ? 2 : 0
        border.color: Theme.c.assetQuickAddButton.focusRing
    }

    // Drawn geometrically: a font glyph has uneven side bearings and looks
    // off-centre inside a small circle.
    contentItem: Item {
        readonly property color crossColor: root.enabled ? Theme.c.assetQuickAddButton.foreground
                                                         : Theme.c.assetQuickAddButton.foregroundDisabled
        Rectangle {
            anchors.centerIn: parent
            visible: !root.done
            width: root._visualSize / 2
            height: 2
            color: parent.crossColor
        }
        Rectangle {
            anchors.centerIn: parent
            visible: !root.done
            width: 2
            height: root._visualSize / 2
            color: parent.crossColor
        }
        IconGlyph {
            anchors.centerIn: parent
            visible: root.done
            glyph: Theme.icons.check
            iconSize: root._visualSize / 2 + 2
            iconColor: parent.crossColor
        }
    }

    ThemedToolTip {
        text: root.tooltip
        visible: root.hovered || root.visualFocus
    }
}
