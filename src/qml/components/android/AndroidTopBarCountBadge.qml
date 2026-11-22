import QtQuick
import Drift

// Count sitting on a top-bar icon. Notifications grow into a pill past one digit;
// a download count stays a circle.
Rectangle {
    id: badge

    property int count: 0
    // 0 shows the number as-is. A positive cap prints "99+" past it.
    property int cap: 0
    property bool grow: false

    width: badge.grow ? Math.max(Theme.spacingXl, label.implicitWidth + Theme.spacingSm)
                      : Theme.spacingXl
    height: Theme.spacingXl
    radius: Theme.radiusPill
    color: Theme.c.androidTopBarCountBadge.background

    Text {
        id: label
        anchors.centerIn: parent
        text: badge.cap > 0 && badge.count > badge.cap
              ? String(badge.cap) + "+"
              : String(badge.count)
        color: Theme.c.androidTopBarCountBadge.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }
}
