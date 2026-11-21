pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Drift

// Themed entry for ThemedContextMenu.
//
// A Menu's `delegate` is only applied to Action-based items, not to MenuItems
// declared inline as menu children — those fall back to the Basic style, whose
// default text colour is near-invisible against the dark menu background. Using
// this component gives inline entries the themed contentItem/background directly.
MenuItem {
    id: root

    // Group label for a menu that mixes several settings (the header's Settings
    // menu). Not an entry: it never highlights and never takes a click.
    property bool sectionHeader: false
    // Action id whose live binding is shown right-aligned and muted.
    property string shortcutId: ""
    readonly property string _shortcutText: shortcutId.length > 0
        ? Theme.shortcutDisplay(EditorState.shortcutFor(shortcutId)) : ""
    enabled: !sectionHeader

    // A section header is a label, not a target: giving it the MenuItem role would have
    // assistive tooling and uiautomator offer it as something to activate.
    Accessible.role: sectionHeader ? Accessible.StaticText : Accessible.MenuItem
    Accessible.name: root.text

    // A Menu lays entries out in a ListView, which still reserves a row for a
    // hidden item, so conditional entries left blank gaps behind.
    implicitHeight: visible ? (sectionHeader ? Theme.controlHeightSm
                                             : Theme.controlHeightSm + Theme.spacingSm)
                            : 0
    height: implicitHeight
    hoverEnabled: true

    contentItem: Item {
        implicitWidth: row.implicitWidth + (shortcutLabel.visible ? shortcutLabel.implicitWidth + Theme.spacingLg * 2 : 0)
        implicitHeight: row.implicitHeight

        Row {
            id: row
            spacing: Theme.spacingLg
            leftPadding: Theme.spacingLg
            rightPadding: Theme.spacingLg

            IconGlyph {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.icon.name.length > 0
                width: visible ? implicitWidth : 0
                glyph: root.icon.name
                iconSize: Theme.iconSizeMd
                iconColor: root.enabled ? Theme.c.themedMenuItem.foreground
                                        : Theme.c.themedMenuItem.foregroundDisabled
                opacity: root.enabled ? 1 : 0.5
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.text
                color: root.sectionHeader ? Theme.c.themedMenuItem.sectionForeground
                                          : Theme.c.themedMenuItem.foreground
                // A header is dimmer by design, so it must not also take the disabled dimming.
                opacity: root.sectionHeader || root.enabled ? 1 : 0.5
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                font.weight: root.sectionHeader ? Font.DemiBold : Font.Normal
                elide: Text.ElideRight
            }
        }

        Text {
            id: shortcutLabel
            visible: root._shortcutText.length > 0
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingLg
            anchors.verticalCenter: parent.verticalCenter
            text: root._shortcutText
            color: Theme.c.themedMenuItem.shortcutForeground
            opacity: root.enabled ? 1 : 0.5
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }

    // The Basic style's submenu arrow is drawn in its own dark-on-dark colour.
    arrow: IconGlyph {
        x: root.width - width - Theme.spacingLg
        anchors.verticalCenter: parent.verticalCenter
        visible: root.subMenu !== null
        glyph: Theme.icons.chevronRight
        iconSize: Theme.iconSizeMd
        iconColor: Theme.c.themedMenuItem.icon
    }

    background: ThemeSlot {
        slot: "ThemedMenuItemBackground"
        control: root
        fallback: Component {
            Rectangle {
                radius: Theme.c.themedMenuItem.radius
                color: root.highlighted && root.enabled ? Theme.c.themedMenuItem.backgroundHighlighted
                                                       : "transparent"

                Behavior on color {
                    ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                }
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        cursorShape: root.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onWheel: (wheel) => { wheel.accepted = false }
    }

    onTriggered: Haptics.press()
}
