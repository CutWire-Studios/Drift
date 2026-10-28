import QtQuick
import Drift

Rectangle {
    id: root

    property bool selected: false
    property bool hovered: false
    property bool hold: false
    property bool disabled: false
    property bool collides: false

    width: Theme.c.keyframesKeyDot.size
    height: Theme.c.keyframesKeyDot.size
    rotation: hold ? 0 : 45
    radius: Theme.c.keyframesKeyDot.radius
    border.width: selected || collides ? Theme.c.keyframesKeyDot.borderWidthSelected : Theme.c.keyframesKeyDot.borderWidth
    border.color: collides ? Theme.c.keyframesKeyDot.borderCollision
                              : (selected ? Theme.c.keyframesKeyDot.borderSelected : Theme.c.keyframesKeyDot.border)
    scale: selected ? Theme.c.keyframesKeyDot.scaleSelected : (hovered ? Theme.c.keyframesKeyDot.scaleHover : 1)
    opacity: disabled ? Theme.c.keyframesKeyDot.opacityDisabled : 1
    antialiasing: true

    Behavior on scale {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }
}
