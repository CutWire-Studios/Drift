import QtQuick
import QtQuick.Controls.Basic
import Drift

// Square mute / solo / record toggle. `tone` is "destructive" or "active".
AbstractButton {
    id: toggle

    property string glyph: ""
    property string tooltip: ""
    property string tone: "destructive"
    property bool blinking: false

    readonly property color _checkedFg: tone === "active" ? Theme.c.mixerStripToggle.activeForeground : Theme.c.mixerStripToggle.destructive

    implicitWidth: 28
    implicitHeight: 28
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    scale: down ? Theme.pressScale : 1.0

    Behavior on scale {
        NumberAnimation { duration: Theme.durationPress; easing.type: Theme.easing }
    }

    Accessible.role: Accessible.CheckBox
    Accessible.name: tooltip
    Accessible.checked: checked

    background: Rectangle {
        radius: Theme.c.mixerStripToggle.radius
        color: toggle.checked
               ? (toggle.tone === "active" ? Theme.c.mixerStripToggle.activeBackground
                                           : Qt.alpha(Theme.c.mixerStripToggle.destructive, Theme.c.mixerStripToggle.destructiveOpacity))
               : (toggle.down ? Theme.c.mixerStripToggle.backgroundPressed : (toggle.hovered ? Theme.c.mixerStripToggle.backgroundHover : "transparent"))
        border.width: toggle.checked || toggle.visualFocus ? Theme.c.mixerStripToggle.borderWidth : 0
        border.color: toggle.visualFocus ? Theme.c.mixerStripToggle.borderFocus
                    : toggle.tone === "active" ? Theme.c.mixerStripToggle.activeBorder
                    : Qt.alpha(Theme.c.mixerStripToggle.destructive, Theme.c.mixerStripToggle.destructiveOpacityBorder)

        Behavior on color {
            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }
    }

    contentItem: Item {
        IconGlyph {
            id: toggleGlyph
            anchors.centerIn: parent
            glyph: toggle.glyph
            iconSize: Theme.iconSizeBase
            iconColor: toggle.checked ? toggle._checkedFg
                     : (toggle.hovered ? Theme.c.mixerStripToggle.foreground : Theme.c.mixerStripToggle.foregroundMuted)

            SequentialAnimation on opacity {
                running: toggle.blinking
                loops: Animation.Infinite
                onStopped: toggleGlyph.opacity = 1
                NumberAnimation { to: 0.3; duration: 450 }
                NumberAnimation { to: 1.0; duration: 450 }
            }
        }
    }

    ThemedToolTip {
        visible: toggle.tooltip.length > 0 && (toggle.hovered || toggle.visualFocus)
        text: toggle.tooltip
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        cursorShape: Qt.PointingHandCursor
    }
}
