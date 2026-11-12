pragma ComponentBehavior: Bound
import QtQuick
import Drift

// One preset in the easing picker grid: a curve thumbnail over its label.
Item {
    id: root

    // An entry from EditorState.easingPresets(): {id, label, samples, spec, user, ...}.
    required property var preset
    property bool selected: false
    property real thumbHeight: Theme.touchInput ? 56 : 40
    // Set for presets that can be starred; empty hides the star (curve styles, "+ New curve").
    property string favoriteTab: ""

    signal activated()
    // Mouse right-click or touch long-press; the caller decides which applies.
    signal contextRequested(real x, real y)

    implicitHeight: thumb.height + label.implicitHeight + Theme.spacingXs

    EasingThumb {
        id: thumb
        width: root.width
        height: root.thumbHeight
        samples: root.preset.samples
        selected: root.selected
        hovered: tileArea.containsMouse
    }

    Text {
        id: label
        anchors.top: thumb.bottom
        anchors.topMargin: Theme.spacingXs
        width: root.width
        horizontalAlignment: Text.AlignHCenter
        text: root.preset.label
        color: root.selected ? Theme.c.easingPicker.foreground : Theme.c.easingPicker.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeTiny
        font.weight: root.selected ? Font.Medium : Font.Normal
        elide: Text.ElideRight
    }

    ThemedToolTip {
        text: root.preset.label
        visible: tileArea.containsMouse
    }

    MouseArea {
        id: tileArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: Qt.PointingHandCursor

        // Latched per press: a long press that came from touch opens the menu, while the
        // same gesture from a mouse does nothing extra.
        property bool pressedByTouch: false
        property bool held: false

        onPressed: (mouse) => {
            pressedByTouch = mouse.source !== Qt.MouseEventNotSynthesized
            held = false
        }
        onPressAndHold: (mouse) => {
            if (!pressedByTouch)
                return
            held = true
            root.contextRequested(mouse.x, mouse.y)
        }
        onClicked: (mouse) => {
            if (mouse.button === Qt.RightButton) {
                root.contextRequested(mouse.x, mouse.y)
                return
            }
            if (!held)
                root.activated()
        }
    }

    AssetFavoriteButton {
        id: favoriteButton
        anchors.left: thumb.left
        anchors.top: thumb.top
        anchors.margins: 2
        visible: root.favoriteTab.length > 0
                 && (favoriteButton.favorited || tileArea.containsMouse || Theme.touchUi)
        tabId: root.favoriteTab
        itemId: root.preset.id
    }
}
