pragma ComponentBehavior: Bound
import QtQuick
import Drift

// One-axis scroller for tab rows, chip rows and icon rails. Drag or wheel to
// scroll; with a mouse over it, < > (or ^ v when `vertical`) arrows appear at
// any end that still has content past it. Touch never hovers, so phones only
// get the swipe.
//
// Children go into an inner item inset by `padding` on the scroll axis, so
// call sites drop their own x/y offsets.
Item {
    id: root

    property bool vertical: false
    property real padding: 0
    // Colour the arrows fade out of — match the surface the strip sits on.
    property color fadeColor: Theme.c.scrollStrip.fade
    default property alias content: inner.data

    readonly property real _extent: vertical ? flick.contentHeight - flick.height
                                             : flick.contentWidth - flick.width
    readonly property real _pos: vertical ? flick.contentY : flick.contentX
    readonly property bool overflowing: _extent > 1
    readonly property bool _showArrows: overflowing && hover.hovered

    implicitWidth: vertical ? inner.childrenRect.width : 0
    implicitHeight: vertical ? 0 : inner.childrenRect.height

    function scrollTo(pos) {
        scrollAnimation.to = Math.max(0, Math.min(root._extent, pos))
        scrollAnimation.restart()
    }

    function scrollBy(delta) {
        const from = scrollAnimation.running ? scrollAnimation.to : root._pos
        root.scrollTo(from + delta)
    }

    HoverHandler {
        id: hover
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: root.vertical ? width : inner.childrenRect.width + root.padding * 2
        contentHeight: root.vertical ? inner.childrenRect.height + root.padding * 2 : height
        flickableDirection: root.vertical ? Flickable.VerticalFlick : Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        interactive: root.overflowing

        onMovementStarted: scrollAnimation.stop()

        Item {
            id: inner
            x: root.vertical ? 0 : root.padding
            y: root.vertical ? root.padding : 0
            width: root.vertical ? flick.width : childrenRect.width
            height: root.vertical ? childrenRect.height : flick.height
        }
    }

    NumberAnimation {
        id: scrollAnimation
        target: flick
        property: root.vertical ? "contentY" : "contentX"
        duration: Theme.durationBase
        easing.type: Easing.OutCubic
    }

    // Horizontal rows turn a plain wheel into sideways scroll. Buttons pass
    // through (NoButton), so chips underneath still take clicks.
    MouseArea {
        anchors.fill: parent
        enabled: !root.vertical && root.overflowing
        acceptedButtons: Qt.NoButton
        onWheel: (wheel) => {
            const dy = wheel.angleDelta.y
            const dx = wheel.angleDelta.x
            root.scrollBy(-(Math.abs(dy) > Math.abs(dx) ? dy : dx))
        }
    }

    component Arrow: Item {
        id: arrow

        property bool atEnd: false
        property string glyph: ""
        signal stepped()

        readonly property real thickness: root.vertical ? Theme.controlHeightSm * 0.75
                                                        : Math.min(root.height, Theme.controlHeightSm)
        readonly property real fadeLength: Theme.spacingXl

        width: root.vertical ? root.width : thickness + fadeLength
        height: root.vertical ? thickness + fadeLength : root.height
        visible: opacity > 0
        opacity: root._showArrows && (atEnd ? root._pos < root._extent - 1 : root._pos > 1) ? 1 : 0

        Behavior on opacity {
            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                orientation: root.vertical ? Gradient.Vertical : Gradient.Horizontal
                GradientStop {
                    position: 0
                    color: arrow.atEnd ? Qt.rgba(root.fadeColor.r, root.fadeColor.g, root.fadeColor.b, 0)
                                       : root.fadeColor
                }
                GradientStop {
                    position: arrow.atEnd ? arrow.fadeLength / (root.vertical ? arrow.height : arrow.width)
                                          : 1 - arrow.fadeLength / (root.vertical ? arrow.height : arrow.width)
                    color: root.fadeColor
                }
                GradientStop {
                    position: 1
                    color: arrow.atEnd ? root.fadeColor
                                       : Qt.rgba(root.fadeColor.r, root.fadeColor.g, root.fadeColor.b, 0)
                }
            }
        }

        IconButton {
            x: root.vertical ? (arrow.width - width) / 2 : (arrow.atEnd ? arrow.width - width : 0)
            y: root.vertical ? (arrow.atEnd ? arrow.height - height : 0) : (arrow.height - height) / 2
            width: root.vertical ? arrow.width : arrow.thickness
            buttonSize: arrow.thickness
            iconSize: 14
            glyph: arrow.glyph
            focusPolicy: Qt.NoFocus
            onClicked: arrow.stepped()
        }
    }

    Arrow {
        anchors.left: parent.left
        anchors.top: parent.top
        glyph: root.vertical ? Theme.icons.chevronUp : Theme.icons.chevronLeft
        onStepped: root.scrollBy(-(root.vertical ? flick.height : flick.width) * 0.8)
    }

    Arrow {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        atEnd: true
        glyph: root.vertical ? Theme.icons.chevronDown : Theme.icons.chevronRight
        onStepped: root.scrollBy((root.vertical ? flick.height : flick.width) * 0.8)
    }
}
