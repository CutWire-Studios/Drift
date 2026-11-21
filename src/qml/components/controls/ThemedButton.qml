pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Drift

// Text button with standard Drift variants. Prefer this over inline Button chrome.
// Variants: "primary" | "secondary" | "ghost" | "destructive"
Button {
    id: root

    property string variant: "secondary"
    // Named `glyph` — Button.icon is FINAL in Qt Quick Controls.
    property string glyph: ""
    property real glyphSize: Theme.iconSizeMd
    property string tooltip: ""
    property string tooltipActionId: ""
    property string tooltipShortcut: ""
    property string tooltipBody: ""
    property string tooltipState: ""
    // Corner rounding of the background. Segments merged into one bordered group
    // (the media bin's split Import button) tuck a smaller radius inside the box.
    property real radius: Theme.c.themedButton.radius
    // "auto" | "press" | "confirm" | "select" | "none". auto is confirm for primary
    // and destructive (those commit something) and press for everything else.
    property string haptic: "auto"

    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSizeSm
    font.weight: variant === "primary" ? Font.Medium : Font.Normal
    leftPadding: glyph.length > 0 ? Theme.spacingXl : Theme.spacing2xl - Theme.spacingXs
    rightPadding: Theme.spacing2xl - Theme.spacingXs
    topPadding: Theme.spacingMd + 1
    bottomPadding: Theme.spacingMd + 1
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    // Press feedback. Set on the root, not on `background`: background and
    // contentItem are siblings in a Control, so scaling the background alone
    // would leave the label at full size. scale does not affect layout, so
    // buttons packed in a Row do not reflow.
    scale: root.down ? Theme.pressScale : 1.0

    Behavior on scale {
        NumberAnimation { duration: Theme.durationPress; easing.type: Theme.easing }
    }

    Accessible.role: Accessible.Button
    Accessible.name: text.length > 0 ? text : tooltip

    readonly property color _fg: {
        switch (variant) {
        case "primary": return Theme.c.themedButton.primaryForeground
        case "destructive": return Theme.c.themedButton.destructiveForeground
        default: return Theme.c.themedButton.foreground
        }
    }

    // Disabled keeps each variant's own hue and dims it, rather than collapsing
    // every variant onto panelAccent — a disabled destructive button must still
    // read as destructive, and a disabled primary must not look like a ghost.
    readonly property color _bg: {
        switch (variant) {
        case "primary":
            if (!enabled)
                return Qt.darker(Theme.c.themedButton.primaryBackground, 1.5)
            return down ? Qt.darker(Theme.c.themedButton.primaryBackground, 1.12)
                        : (hovered ? Qt.lighter(Theme.c.themedButton.primaryBackground, 1.08)
                                   : Theme.c.themedButton.primaryBackground)
        case "ghost":
            if (!enabled)
                return "transparent"
            return down ? Theme.c.themedButton.ghostBackgroundPressed
                        : (hovered ? Theme.c.themedButton.ghostBackgroundHover : "transparent")
        case "destructive":
            if (!enabled)
                return "transparent"
            return down ? Theme.c.themedButton.destructiveBackgroundPressed
                        : (hovered ? Theme.c.themedButton.destructiveBackgroundHover : "transparent")
        default: // secondary
            if (!enabled)
                return Theme.c.themedButton.backgroundDisabled
            return down ? Theme.c.themedButton.backgroundPressed
                        : (hovered ? Theme.c.themedButton.backgroundHover : Theme.c.themedButton.background)
        }
    }

    // Button.flat drops the border so the button can sit inside a shared bordered
    // group without doubling the outline at the join.
    readonly property bool _drawBorder: !flat && (variant === "secondary" || variant === "ghost"
                                        || variant === "destructive")

    contentItem: Item {
        id: content

        // Space the glyph occupies, including the gap before the label.
        readonly property real glyphSpace: root.glyph.length > 0
                                           ? buttonGlyph.implicitWidth + row.spacing : 0

        // Sized from the children's *implicit* widths, never their laid-out
        // widths: the label's width is clamped against this item, so measuring
        // the laid-out width here would close a binding loop and collapse the
        // whole content item to zero (which blanks the label).
        implicitWidth: glyphSpace + label.implicitWidth
        implicitHeight: Math.max(label.implicitHeight, Theme.iconSizeBase)

        Row {
            id: row
            anchors.centerIn: parent
            width: Math.min(content.implicitWidth, content.width)
            spacing: Theme.spacingMd

            IconGlyph {
                id: buttonGlyph
                visible: root.glyph.length > 0
                width: visible ? implicitWidth : 0
                glyph: root.glyph
                iconSize: root.glyphSize
                iconColor: root._fg
                anchors.verticalCenter: parent.verticalCenter
                opacity: root.enabled ? 1 : 0.5
            }

            Text {
                id: label
                text: root.text
                font: root.font
                color: root._fg
                opacity: root.enabled ? 1 : 0.5
                // Elides only when the button is genuinely too narrow, instead of
                // painting outside the background rectangle.
                elide: Text.ElideRight
                width: Math.max(0, Math.min(implicitWidth, content.width - content.glyphSpace))
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    background: ThemeSlot {
        slot: "ThemedButtonBackground"
        control: root
        fallback: Component {
            Rectangle {
                implicitHeight: Theme.controlHeight
                radius: root.radius
                color: root._bg
                border.width: root._drawBorder ? Theme.c.themedButton.borderWidth : 0
                border.color: Theme.c.themedButton.border
                opacity: root.enabled ? 1 : 0.6

                Behavior on color {
                    ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                }

                Rectangle {
                    anchors.fill: parent
                    radius: parent.radius
                    color: "transparent"
                    border.width: Theme.c.themedButton.focusBorderWidth
                    border.color: Theme.c.themedButton.focusBorder
                    visible: root.visualFocus
                }
            }
        }
    }

    RichToolTip {
        text: root.tooltip
        actionId: root.tooltipActionId
        shortcut: root.tooltipShortcut
        body: root.tooltipBody
        state: root.tooltipState
        visible: root.tooltip.length > 0 && (root.hovered || root.visualFocus)
    }

    onClicked: {
        if (haptic === "none")
            return
        if (haptic === "confirm"
                || (haptic === "auto" && (variant === "primary" || variant === "destructive")))
            Haptics.confirm()
        else if (haptic === "select")
            Haptics.select()
        else
            Haptics.press()
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        cursorShape: root.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        // Cursor-only overlays accept wheel by default and block parent Flickables.
        onWheel: (wheel) => { wheel.accepted = false }
    }
}
