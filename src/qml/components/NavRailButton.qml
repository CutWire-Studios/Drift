import QtQuick
import QtQuick.Controls.Basic
import Drift

// One slot in a bottom navigation bar: pill, glyph, label.
//
// Promoted out of AndroidBottomRail so the home nav and the editor rail are the same control
// rather than two that merely resemble each other. The editor rail is the one part of the phone
// UI that already got this right — 48dp+ targets, a visible text label on every slot, and a
// selection pill rather than a tint that vanished in light mode — so the home nav adopts it
// instead of inventing a second answer.
AbstractButton {
    id: root

    // { id, label, icon }
    required property var entry
    property bool selected: false

    hoverEnabled: true

    Accessible.role: Accessible.Button
    Accessible.name: entry.label
    // Nav state has to reach assistive tech too, not just the tint below.
    Accessible.checkable: true
    Accessible.checked: root.selected

    onClicked: Haptics.select()

    // Press feedback on the root rather than the background, so the label dips with the icon,
    // and via scale so packed slots do not reflow.
    scale: root.down ? Theme.pressScale : 1.0

    Behavior on scale {
        NumberAnimation { duration: Theme.durationPress; easing.type: Theme.easing }
    }

    background: Item { }

    contentItem: Item {
        anchors.fill: parent

        Column {
            anchors.centerIn: parent
            spacing: 3

            // The pill, not a tint, carries selection: Theme.primary sits at 1.69:1 on the
            // light panel and said nothing at all in light mode.
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: Theme.iconSizeLg + Theme.spacing2xl
                height: Theme.iconSizeLg + Theme.spacingLg
                radius: Theme.radiusPill
                color: root.selected ? Theme.panelSecondaryBg
                                     : (root.down ? Theme.panelAccent : "transparent")

                Behavior on color {
                    ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                }

                IconGlyph {
                    anchors.centerIn: parent
                    glyph: root.entry.icon
                    iconSize: Theme.iconSizeLg
                    iconColor: root.selected ? Theme.accentOnPanel : Theme.mutedForeground
                }
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: root.entry.label
                color: root.selected ? Theme.accentOnPanel : Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                font.weight: root.selected ? Font.Medium : Font.Normal
            }
        }
    }
}
