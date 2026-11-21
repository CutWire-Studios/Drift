import QtQuick
import Drift

// Shown when the project has no tracks yet.
EmptyState {
    id: empty

    property var view
    property var panel

    x: view.contentX + (view.width - width) / 2
    y: view.contentY + view.headerHeight
       + Math.max(0, (view.height - view.headerHeight - height) / 2)
    width: Math.min(view.width - Theme.spacing3xl, 320)
    visible: panel.tracks.length === 0
    z: 4
    glyph: Theme.icons.layers
    title: qsTr("Your timeline is empty")
    hint: qsTr("Drag media here from the library, or import a file to start.")
    actionText: qsTr("Import media")
    actionVariant: "primary"
    secondaryText: qsTr("New track")
    onActionTriggered: panel.importMediaDirectlyToTimeline()
    onSecondaryTriggered: newTrackMenuFromEmpty.open()

    NewTrackMenu {
        id: newTrackMenuFromEmpty
        y: parent.height
    }
}
