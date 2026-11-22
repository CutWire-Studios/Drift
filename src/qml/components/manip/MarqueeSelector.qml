import QtQuick
import Drift

// Rubber-band selection in the owner's coordinates. The owner feeds it the press and the pointer
// from its own drag handler; the modifiers come from that press. Shift asks for an additive
// selection and Ctrl for a toggle, and the owner decides what each means.
Item {
    id: root

    property bool active: false
    property point origin: Qt.point(0, 0)
    property point current: Qt.point(0, 0)
    readonly property rect band: Qt.rect(Math.min(origin.x, current.x), Math.min(origin.y, current.y),
                                         Math.abs(current.x - origin.x), Math.abs(current.y - origin.y))
    property bool additive: false
    property bool toggle: false

    signal rectChanged(rect rect, bool additive, bool toggle)
    signal finished(rect rect, bool additive, bool toggle)

    function begin(p, modifiers) {
        origin = p
        current = p
        additive = (modifiers & Qt.ShiftModifier) !== 0
        toggle = (modifiers & Qt.ControlModifier) !== 0
        active = true
    }

    function update(p) {
        if (!active)
            return
        current = p
        rectChanged(band, additive, toggle)
    }

    function end() {
        if (!active)
            return
        active = false
        finished(band, additive, toggle)
    }

    function cancel() {
        active = false
    }

    Rectangle {
        visible: root.active
        x: root.band.x
        y: root.band.y
        width: root.band.width
        height: root.band.height
        color: Qt.rgba(Theme.c.keyframesMarquee.background.r, Theme.c.keyframesMarquee.background.g,
                       Theme.c.keyframesMarquee.background.b, Theme.c.keyframesMarquee.backgroundOpacity)
        border.width: Theme.c.keyframesMarquee.borderWidth
        border.color: Theme.c.keyframesMarquee.border
    }
}
