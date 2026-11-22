pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Drift

// Themed on/off switch. Replaces the raw QtQuick.Controls.Basic `Switch`
// instances, which shipped completely unstyled and looked foreign next to every
// other control in the inspector.
Switch {
    id: root

    property string tooltip: ""

    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSizeXs
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    spacing: Theme.spacingLg
    implicitHeight: Math.max(implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding,
                             Theme.touchInput && Theme.compact ? Theme.androidMinTouchTarget : 0)

    Accessible.role: Accessible.CheckBox
    Accessible.name: text.length > 0 ? text : tooltip

    indicator: ThemeSlot {
        implicitWidth: 36
        implicitHeight: 20
        x: root.leftPadding
        y: parent.height / 2 - height / 2
        slot: "ThemedSwitchIndicator"
        control: root
        fallback: Component {
            Rectangle {
                radius: Theme.c.themedSwitch.trackRadius
                color: root.checked ? Theme.c.themedSwitch.trackBackgroundChecked
                                    : Theme.c.themedSwitch.trackBackground
                border.width: Theme.c.themedSwitch.trackBorderWidth
                border.color: root.checked ? Theme.c.themedSwitch.trackBorderChecked
                                           : Theme.c.themedSwitch.trackBorder
                opacity: root.enabled ? 1 : 0.5

                Behavior on color {
                    ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                }

                Rectangle {
                    id: knob
                    x: root.checked ? parent.width - width - 2 : 2
                    y: 2
                    width: parent.height - 4
                    height: width
                    radius: Theme.c.themedSwitch.handleRadius
                    color: Theme.c.themedSwitch.handleBackground
                    scale: root.down ? 0.9 : 1.0

                    // Travel across the track is on-screen movement, not an entrance:
                    // ease-in-out, so it neither jerks off the mark nor overshoots.
                    Behavior on x {
                        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easingInOut }
                    }
                    Behavior on scale {
                        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                    }
                }

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -Theme.c.themedSwitch.focusBorderWidth
                    radius: Theme.c.themedSwitch.trackRadius
                    color: "transparent"
                    border.width: Theme.c.themedSwitch.focusBorderWidth
                    border.color: Theme.c.themedSwitch.focusBorder
                    visible: root.visualFocus
                }
            }
        }
    }

    contentItem: Text {
        text: root.text
        font: root.font
        color: Theme.c.themedSwitch.foreground
        opacity: root.enabled ? 1 : 0.5
        verticalAlignment: Text.AlignVCenter
        leftPadding: root.indicator.width + root.spacing
        elide: Text.ElideRight
    }

    ThemedToolTip {
        text: root.tooltip
        visible: root.tooltip.length > 0 && (root.hovered || root.visualFocus)
    }

    onClicked: Haptics.toggle(root.checked)

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        cursorShape: root.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onWheel: (wheel) => { wheel.accepted = false }
    }
}
