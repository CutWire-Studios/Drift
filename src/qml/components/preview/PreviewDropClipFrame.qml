import QtQuick
import Drift

// Outline of the clip an effect or mask would land on. The parent places it.
Rectangle {
    color: Qt.alpha(Theme.c.previewDropClipFrame.background, Theme.c.previewDropClipFrame.backgroundOpacity)
    border.width: Theme.c.previewDropClipFrame.borderWidth
    border.color: Theme.c.previewDropClipFrame.border
}
