import QtQuick
import QtQuick.Controls.Basic
import Drift

// Checkable button for segmented groups (text align, valign) and standalone
// on/off boxes (Italic, Word wrap, Shadow, Background).
//
// Replaces six hand-built `Rectangle + MouseArea` toggles that had a checked
// state and nothing else: no hover, no pressed, no focus, no disabled visual,
// no keyboard activation, and no accessible role.
AbstractButton {
    id: root

    property string glyph: ""
    property real glyphSize: Theme.iconSizeMd
    property string tooltip: ""
    property real toggleHeight: Theme.controlHeightSm

    checkable: true
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    // Control.horizontalPadding is per-side and FINAL — used directly, not shadowed.
    horizontalPadding: Theme.spacingMd
    // Never narrower than it is tall, so icon-only toggles stay square.
    implicitWidth: Math.max(toggleHeight, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: toggleHeight

    // Press feedback — see the note in ThemedButton.qml.
    scale: root.down ? Theme.pressScale : 1.0

    Behavior on scale {
        NumberAnimation { duration: Theme.durationPress; easing.type: Theme.easing }
    }

    Accessible.role: Accessible.CheckBox
    Accessible.name: text.length > 0 ? text : tooltip
    Accessible.checked: root.checked
    Accessible.onToggleAction: root.toggle()

    readonly property color _fg: checked ? Theme.panelSecondaryForeground : Theme.panelForeground

    background: Rectangle {
        radius: Theme.radiusSm
        color: {
            if (!root.enabled)
                return Theme.panelAccent
            if (root.checked)
                return root.down ? Qt.darker(Theme.panelSecondaryBg, 1.2) : Theme.panelSecondaryBg
            if (root.down)
                return Theme.panelMuted
            if (root.hovered)
                return Theme.popoverHover
            return Theme.panelAccent
        }
        border.width: Theme.borderWidth
        border.color: root.checked ? Theme.panelSecondaryBorder : Theme.panelBorder
        opacity: root.enabled ? 1 : 0.5

        Behavior on color {
            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }
        Behavior on border.color {
            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }

        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: "transparent"
            border.width: Theme.borderWidthFocus
            border.color: Theme.focusRing
            visible: root.visualFocus
        }
    }

    contentItem: Item {
        implicitWidth: root.glyph.length > 0 ? root.glyphSize : label.implicitWidth
        implicitHeight: root.glyph.length > 0 ? root.glyphSize : label.implicitHeight

        IconGlyph {
            anchors.centerIn: parent
            visible: root.glyph.length > 0
            glyph: root.glyph
            iconSize: root.glyphSize
            iconColor: root._fg
            opacity: root.enabled ? 1 : 0.5
        }

        Text {
            id: label
            anchors.centerIn: parent
            visible: root.glyph.length === 0
            text: root.text
            color: root._fg
            opacity: root.enabled ? 1 : 0.5
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            font.weight: root.checked ? Font.Medium : Font.Normal
            elide: Text.ElideRight
            width: Math.min(implicitWidth, root.availableWidth)
            horizontalAlignment: Text.AlignHCenter
        }
    }

    ThemedToolTip {
        text: root.tooltip
        visible: root.tooltip.length > 0 && (root.hovered || root.visualFocus)
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        cursorShape: root.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onWheel: (wheel) => { wheel.accepted = false }
    }
}
