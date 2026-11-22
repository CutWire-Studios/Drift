import QtQuick
import Drift

// A transform layer's row carries a faint wash of its colour.
Rectangle {
    id: wash

    property bool shown: false

    anchors.fill: parent
    visible: wash.shown
    color: Qt.alpha(Theme.c.androidTimelineTrackWash.background, Theme.c.androidTimelineTrackWash.backgroundOpacity)
}
