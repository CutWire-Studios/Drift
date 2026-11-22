import QtQuick
import Drift

// Landing preview outline for a library drop or a clip move.
Rectangle {
    property var panel
    property int trackIndex: -1
    visible: panel.dropTrackIndex === trackIndex
             && !panel.dropCreatesNewTrack
    x: panel.dropStartSeconds * panel.pxPerSecond
    width: panel.dropDurationSeconds * panel.pxPerSecond
    height: parent.height
    radius: Theme.c.timelineLandingOutline.radius
    color: Qt.alpha(Theme.c.timelineLandingOutline.background, Theme.c.timelineLandingOutline.backgroundOpacity)
    border.width: Theme.c.timelineLandingOutline.borderWidth
    border.color: Theme.c.timelineLandingOutline.border
    z: 5

    // The outline is held on screen after a file drop for as
    // long as the import it started is still probing, so it has
    // to say why it is sitting there empty.
    Text {
        anchors.centerIn: parent
        visible: panel.importDropPending
        text: qsTr("Importing…")
        color: Theme.c.timelineLandingOutline.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeTiny
        font.weight: Font.Medium

        SequentialAnimation on opacity {
            running: panel.importDropPending
            loops: Animation.Infinite
            NumberAnimation { to: 0.45; duration: Theme.durationSlow; easing.type: Theme.easing }
            NumberAnimation { to: 1.0; duration: Theme.durationSlow; easing.type: Theme.easing }
        }
    }
}
