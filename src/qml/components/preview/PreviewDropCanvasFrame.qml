import QtQuick
import Drift

// Frame around the whole canvas when a drop would land as a new overlay.
Rectangle {
    anchors.fill: parent
    color: Qt.alpha(Theme.c.previewDropCanvasFrame.background, Theme.c.previewDropCanvasFrame.backgroundOpacity)
    border.width: Theme.c.previewDropCanvasFrame.borderWidth
    border.color: Theme.c.previewDropCanvasFrame.border
}
