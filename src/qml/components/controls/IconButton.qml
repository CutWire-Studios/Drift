pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Drift

// Toolbar button, variants "text" / "ghost". Icon-only by default; set `text`
// to show a label beside the glyph (header actions).
//
// Built on AbstractButton (not a bare Rectangle) so it joins the tab chain, is
// activatable with Space/Enter, and reports a Button role to accessibility.
// The icon name goes in `glyph` — AbstractButton.icon is FINAL.
AbstractButton {
    id: root

    property string glyph: ""
    property string tooltip: ""
    property string tooltipActionId: ""
    property string tooltipBody: ""
    property string tooltipShortcut: ""
    property string tooltipState: ""
    property url tooltipPreview: ""
    property Component tooltipIllustration: null
    property real buttonSize: Theme.iconButtonSize
    property real iconSize: Theme.iconSizeBase
    property bool active: false
    // "text": transparent, hover = subtle tint (toolbar buttons)
    // "ghost": transparent, hover = accent background (tab rail, view toggles)
    property string variant: "text"
    // "auto" | "press" | "confirm" | "select" | "none". auto is a light press.
    property string haptic: "auto"

    // Touch has no hover, so every tooltip on an icon-only button was text nobody on a
    // phone could reach. A long press surfaces it; the latch clears when the press ends.
    property bool pressAndHeld: false

    // Small buttons keep their drawn size on a touchscreen; only the area that accepts a
    // finger grows, up to Theme.iconButtonSize. Zero with a pointer.
    property real touchHitPadding: Theme.touchInput
                                   ? Math.max(0, (Theme.iconButtonSize - Math.min(width, height)) / 2)
                                   : 0
    containmentMask: touchHitPadding > 0 ? touchHitArea : null

    Item {
        id: touchHitArea
        anchors.fill: parent
        anchors.margins: -root.touchHitPadding
    }

    readonly property bool hasLabel: text.length > 0
    readonly property color _fg: active ? Theme.c.iconButton.foregroundActive : Theme.c.iconButton.foreground
    readonly property color _labelFg: active ? Theme.c.iconButton.labelForegroundActive : Theme.c.iconButton.labelForeground

    implicitWidth: hasLabel ? Math.ceil(labeledRow.implicitWidth + Theme.spacingMd * 2)
                            : buttonSize
    implicitHeight: buttonSize
    width: implicitWidth
    height: buttonSize
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    // Press feedback — see the note in ThemedButton.qml for why this sits on the
    // root rather than on `background`.
    scale: root.down ? Theme.pressScale : 1.0

    Behavior on scale {
        NumberAnimation { duration: Theme.durationPress; easing.type: Theme.easing }
    }

    Accessible.role: Accessible.Button
    Accessible.name: text.length > 0 ? text : (tooltip.length > 0 ? tooltip : glyph)
    Accessible.onPressAction: root.clicked()

    background: ThemeSlot {
        slot: "IconButtonBackground"
        control: root
        fallback: Component {
            Rectangle {
                radius: Theme.c.iconButton.radius
                color: {
                    if (root.active)
                        return Theme.c.iconButton.backgroundActive
                    if (!root.enabled)
                        return "transparent"
                    // Both variants now tint on hover. The old "text" variant dimmed to
                    // 0.75 opacity, which read as disabled rather than as hover.
                    if (root.down)
                        return Theme.c.iconButton.backgroundPressed
                    if (root.hovered)
                        return root.variant === "ghost" ? Theme.c.iconButton.ghostBackgroundHover
                                                       : Theme.c.iconButton.textBackgroundHover
                    return "transparent"
                }
                border.width: root.active ? Theme.c.iconButton.borderWidth : 0
                border.color: Theme.c.iconButton.border
                opacity: root.enabled ? 1 : 0.5

                Behavior on color {
                    ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                }
                Behavior on opacity {
                    NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                }

                // Keyboard focus ring. Drawn outside the fill so it stays visible on the
                // active/selected state too.
                Rectangle {
                    anchors.fill: parent
                    radius: parent.radius
                    color: "transparent"
                    border.width: Theme.c.iconButton.focusBorderWidth
                    border.color: Theme.c.iconButton.focusBorder
                    visible: root.visualFocus
                }
            }
        }
    }

    contentItem: Item {
        implicitWidth: root.hasLabel ? labeledRow.implicitWidth : glyphOnly.implicitWidth
        implicitHeight: root.buttonSize

        IconGlyph {
            id: glyphOnly
            visible: !root.hasLabel
            anchors.centerIn: parent
            glyph: root.glyph
            iconSize: root.iconSize
            iconColor: root._fg
            opacity: root.enabled ? 1 : 0.5

            Behavior on opacity {
                NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
            }
        }

        Row {
            id: labeledRow
            visible: root.hasLabel
            anchors.centerIn: parent
            spacing: Theme.spacingSm

            IconGlyph {
                glyph: root.glyph
                iconSize: root.iconSize
                iconColor: root._fg
                anchors.verticalCenter: parent.verticalCenter
                opacity: root.enabled ? 1 : 0.5
            }

            Text {
                text: root.text
                color: root._labelFg
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                font.weight: Font.Medium
                anchors.verticalCenter: parent.verticalCenter
                opacity: root.enabled ? 1 : 0.5
            }
        }
    }

    RichToolTip {
        text: root.tooltip
        actionId: root.tooltipActionId
        shortcut: root.tooltipShortcut
        body: root.tooltipBody
        state: root.tooltipState
        preview: root.tooltipPreview
        illustration: root.tooltipIllustration
        // Shown on hover, and on keyboard focus so Tab users get the same labels.
        // Skip when the tooltip is just the visible label again.
        visible: root.tooltip.length > 0 && root.tooltip !== root.text
                 && (root.hovered || root.visualFocus
                     || (touchPress.pressed && root.pressAndHeld))
    }

    // AbstractButton's pressAndHold carries no device; this passive handler tells a
    // finger's hold (shows the tooltip) from a mouse's.
    TapHandler {
        id: touchPress
        acceptedDevices: PointerDevice.TouchScreen
        gesturePolicy: TapHandler.DragThreshold
    }

    onPressAndHold: root.pressAndHeld = true
    onReleased: root.pressAndHeld = false
    onCanceled: root.pressAndHeld = false

    onClicked: {
        if (haptic === "none")
            return
        if (haptic === "confirm")
            Haptics.confirm()
        else if (haptic === "select")
            Haptics.select()
        else if (haptic !== "auto" && haptic !== "press")
            return
        else
            Haptics.press()
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        cursorShape: root.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onWheel: (wheel) => { wheel.accepted = false }
    }
}
