import QtQuick
import Drift

// Hairline between the horizontal and vertical alignment toggles.
Rectangle {
    width: Theme.c.textAlignDivider.borderWidth
    height: Theme.touchInput && Theme.compact ? Theme.androidMinTouchTarget : Theme.controlHeightSm
    color: Theme.c.textAlignDivider.border
}
