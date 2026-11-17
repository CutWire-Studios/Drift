import QtQuick
import Drift

// Bakes the work area, or cancels the bake that is running.
Rectangle {
    id: chip

    readonly property bool busy: EditorState.bakes.busy
    readonly property bool inComposite: EditorState.activeSequenceId !== ""
    // Cancelling a running bake stays possible from any tab.
    readonly property bool available: busy || !inComposite

    width: chipLabel.implicitWidth + Theme.spacingLg * 2
    height: chipLabel.implicitHeight + Theme.spacingXs * 2
    radius: Theme.c.timelineBakeChip.radius
    color: Theme.c.timelineBakeChip.background
    opacity: !chip.available ? 0.45 : chipMouse.containsMouse ? 1 : 0.85

    Text {
        id: chipLabel
        anchors.centerIn: parent
        text: chip.busy ? qsTr("Baking %1%").arg(Math.round(EditorState.bakes.progress * 100))
                        : qsTr("Bake")
        color: Theme.c.timelineBakeChip.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeTiny
    }

    MouseArea {
        id: chipMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: chip.available ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: {
            if (!chip.available)
                return
            chip.busy ? EditorState.bakes.cancelBake() : EditorState.bakes.bakeWorkArea()
        }
    }

    ThemedToolTip {
        visible: chipMouse.containsMouse
        text: chip.busy ? qsTr("Cancel baking")
              : chip.inComposite ? qsTr("Bakes are of the main timeline. Switch to it to bake.")
              : qsTr("Pre-render the work area for smooth playback and export")
    }
}
