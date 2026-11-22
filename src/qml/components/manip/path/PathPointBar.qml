import QtQuick
import Drift

// What a long press on a point offers on a touch screen, in place of right-click: delete, corner or
// smooth, break the handles, and whether delete heals the gap. Sits above the pressed point.
TouchContextBar {
    id: bar

    required property var session
    // The point, in the parent's coordinates.
    property point anchorPoint
    property bool open: false

    chips: []
    visible: open
    x: Math.max(0, Math.min(parent ? parent.width - width : 0, anchorPoint.x - width / 2))
    y: Math.max(0, anchorPoint.y - Theme.androidMinTouchTarget * 2.2)
    z: 120

    ThemedChip {
        chipHeight: Theme.androidMinTouchTarget
        text: qsTr("Delete")
        onClicked: {
            bar.session.deleteSelected(bar.session.healOnDelete)
            bar.open = false
        }
    }

    ThemedChip {
        chipHeight: Theme.androidMinTouchTarget
        text: bar.session.selectedMode === 0 ? qsTr("Smooth") : qsTr("Corner")
        onClicked: {
            bar.session.toggleSmoothSelected()
            bar.open = false
        }
    }

    ThemedChip {
        chipHeight: Theme.androidMinTouchTarget
        text: qsTr("Break handles")
        selected: bar.session.selectedMode === 3
        onClicked: {
            bar.session.setSelectedMode(3)
            bar.open = false
        }
    }

    ThemedChip {
        chipHeight: Theme.androidMinTouchTarget
        text: qsTr("Heal")
        selected: bar.session.healOnDelete
        onClicked: bar.session.healOnDelete = !bar.session.healOnDelete
    }
}
