pragma ComponentBehavior: Bound
import QtQuick
import Drift

// Diamond switch for one property's animation. Its colour says whether the keyframes are driving
// the property; clicking parks them (they are kept) or brings them back:
//
//   grey outline    — no keyframes yet, so there is nothing to switch
//   ghosted colour  — keyframes exist but are switched off
//   solid colour    — animating
//
// Adding and removing individual keys is the +/- button down on the value row, not this.
Item {
    id: root

    // The property's curve colour, so the switch and its curve on the strip read as one series.
    property color accentColor: Theme.c.keyframeDiamond.accent
    property bool animated: false
    property bool keysEnabled: true
    property string tooltip: ""

    readonly property bool live: animated && keysEnabled

    signal toggled()

    width: 12
    height: 12

    ThemeSlot {
        anchors.centerIn: parent
        slot: "KeyframeDiamond"
        control: root
        fallback: Component {
            Rectangle {
                width: 9
                height: 9
                rotation: 45
                radius: Theme.c.keyframeDiamond.radius
                color: root.live ? root.accentColor : "transparent"
                border.width: Theme.c.keyframeDiamond.borderWidth
                border.color: root.live
                              ? root.accentColor
                              : (root.animated
                                 // Switched off: a ghost of the property's own colour, so it reads as
                                 // parked rather than as never keyed.
                                 ? Qt.rgba(root.accentColor.r, root.accentColor.g, root.accentColor.b,
                                           Theme.c.keyframeDiamond.opacityParked)
                                 : Theme.c.keyframeDiamond.foregroundMuted)

                Behavior on color {
                    ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                }
                Behavior on border.color {
                    ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                }
            }
        }
    }

    ThemedToolTip {
        text: root.tooltip
        visible: root.tooltip.length > 0 && hover.hovered
    }

    // TapHandler's own cursorShape only applies while it is active, so hovering needs its own
    // handler for the click target to advertise itself.
    HoverHandler {
        id: hover
        cursorShape: root.animated ? Qt.PointingHandCursor : Qt.ArrowCursor
    }

    TapHandler {
        margin: Theme.touchInput ? 18 : 0
        enabled: root.animated
        onTapped: root.toggled()
    }
}
