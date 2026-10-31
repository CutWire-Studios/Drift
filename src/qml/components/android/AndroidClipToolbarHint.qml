import QtQuick
import Drift

// Honest rather than a row of greyed buttons — the timeline below never shifts
// because the toolbar keeps its height either way.
Text {
    color: Theme.c.androidClipToolbarHint.foregroundMuted
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSizeSm
    elide: Text.ElideRight
}
