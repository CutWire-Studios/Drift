import QtQuick
import Drift

// The icon square of a bottom-sheet action grid cell.
Rectangle {
    id: face

    property string glyph: ""
    property bool toggled: false
    property bool pressed: false

    width: Theme.phone ? Theme.androidControlHeight + 4 : Theme.androidMinTouchTarget
    height: width
    radius: Theme.c.androidSheetActionTileFace.radius
    color: face.toggled
           ? Qt.alpha(Theme.c.androidSheetActionTileFace.backgroundToggled, Theme.c.androidSheetActionTileFace.backgroundToggledOpacity)
           : (face.pressed ? Theme.c.androidSheetActionTileFace.backgroundPressed
                           : Theme.c.androidSheetActionTileFace.background)
    border.width: face.toggled ? Theme.c.androidSheetActionTileFace.borderWidth : 0
    border.color: Theme.c.androidSheetActionTileFace.border

    Behavior on color {
        ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }

    IconGlyph {
        anchors.centerIn: parent
        glyph: face.glyph
        iconSize: Theme.iconSizeLg
        iconColor: face.toggled ? Theme.c.androidSheetActionTileFace.iconToggled : Theme.c.androidSheetActionTileFace.icon
    }
}
