import QtQuick
import Drift
// Navigating the camera output: wheel zoom about the cursor with Ctrl, middle-drag pan, and the
// composition guides on the canvas. Takes only the middle button, so left-drags still reach the
// clip handles; in crop mode CropOverlay has the same gestures and takes them first.
Item {
    id: mode

    // The PreviewViewport this view navigates.
    property var viewport

    ViewNavigation {
        anchors.fill: parent
        viewport: mode.viewport
    }

    // Drawn on the canvas itself, so it is clipped to the frame and sits under the status text.
    GuideLayer {
        parent: mode.viewport ? mode.viewport.canvas : mode
        anchors.fill: parent
    }
}
