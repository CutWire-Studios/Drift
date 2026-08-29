import QtQuick
import QtQuick.Controls.Basic
import Drift

// Selectable pill/chip used for aspect presets, speed presets, category filters, etc.
// Variants:
//   "primary"   — selected fill = Theme.primary
//   "secondary" — selected fill = Theme.panelSecondaryBg
//   "outline"   — transparent / accent fill; border always drawn
//
// Built on AbstractButton so chips are tab-reachable and Space/Enter-activatable.
// `selected` is kept as the public API (it drives `checked` underneath) so the
// ~20 existing call sites read the same as before.
AbstractButton {
    id: root

    property bool selected: false
    property string variant: "primary"
    property real chipHeight: Theme.controlHeightSm
    property string tooltip: ""
    // Selected fill/border for the "primary" variant. Override to tint a chip to
    // the series it stands for (e.g. keyframe curve colors).
    property color accentColor: Theme.primary

    checked: selected
    checkable: false
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    // Control.horizontalPadding is per-side and FINAL, so it is used directly
    // rather than shadowed. Width falls out of contentItem + padding.
    horizontalPadding: Theme.spacingLg
    implicitHeight: chipHeight
    height: chipHeight

    // Press feedback — see the note in ThemedButton.qml.
    scale: root.down ? Theme.pressScale : 1.0

    Behavior on scale {
        NumberAnimation { duration: Theme.durationPress; easing.type: Theme.easing }
    }

    Accessible.role: Accessible.RadioButton
    Accessible.name: root.text
    Accessible.checked: root.selected
    Accessible.onPressAction: root.clicked()

    readonly property color _fill: {
        if (!enabled)
            return variant === "outline" ? "transparent" : Theme.panelAccent
        if (selected) {
            switch (variant) {
            case "secondary": return down ? Qt.darker(Theme.panelSecondaryBg, 1.2) : Theme.panelSecondaryBg
            case "outline": return down ? Theme.panelMuted : Theme.panelAccent
            default: return down ? Qt.darker(accentColor, 1.15) : accentColor
            }
        }
        if (down)
            return Theme.panelMuted
        if (hovered)
            return Theme.popoverHover
        return variant === "outline" ? "transparent" : Theme.panelAccent
    }

    readonly property color _border: {
        if (selected) {
            switch (variant) {
            case "secondary": return Theme.panelSecondaryBorder
            case "outline": return Theme.panelBorder
            default: return accentColor
            }
        }
        return Theme.panelBorder
    }

    readonly property color _fg: {
        if (selected && variant === "primary")
            return Theme.primaryForeground
        if (selected && variant === "secondary")
            return Theme.panelSecondaryForeground
        return Theme.panelForeground
    }

    background: Rectangle {
        radius: Theme.radiusSm
        color: root._fill
        border.width: Theme.borderWidth
        border.color: root._border
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

    contentItem: Text {
        id: label
        text: root.text
        color: root._fg
        opacity: root.enabled ? 1 : 0.6
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
        font.weight: root.selected ? Font.Medium : Font.Normal
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
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
