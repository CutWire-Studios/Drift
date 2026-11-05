import QtQuick
import QtQuick.Controls.Basic
import Drift

// Standard "nothing here yet" placeholder: icon frame, title, hint, optional CTA.
//
// Generalized from the one good hand-built instance in PropertiesPanel so that
// empty media grids, empty timelines, empty tabs and empty lists all read the
// same instead of being variously a rich card, plain centered text, or nothing.
Item {
    id: root

    property string glyph: Theme.icons.info
    // Set alongside a loader glyph so it actually turns.
    property bool glyphSpinning: false
    property string title: ""
    property string hint: ""
    // Leave empty to omit the button.
    property string actionText: ""
    property string actionVariant: "secondary"
    // Compact drops the icon frame — for small panes where it would dominate.
    property bool compact: false

    signal actionTriggered()

    // Parents that size from this item's height report exactly the full layout, so they
    // always fit; a zero-height parent is treated as not laid out yet.
    readonly property real available: parent && parent.height > 0 ? parent.height : Infinity
    function layoutHeight(icon, hint, action) {
        let n = 0
        let h = 0
        if (icon) { h += 48; n++ }
        if (root.title.length > 0) { h += titleText.implicitHeight; n++ }
        if (hint && root.hint.length > 0) { h += hintText.implicitHeight; n++ }
        if (action && root.actionText.length > 0) { h += actionButton.implicitHeight; n++ }
        return h + Math.max(0, n - 1) * Theme.spacingLg
    }
    readonly property bool actionFits: actionButton.implicitWidth <= root.width
    readonly property bool showIcon: !compact && layoutHeight(true, true, actionFits) <= available
    readonly property bool showAction: actionFits && layoutHeight(false, true, true) <= available
    readonly property bool showHint: layoutHeight(false, true, false) <= available

    implicitWidth: Math.min(260, parent ? parent.width : 260)
    // Height follows the column directly — anchors.centerIn on a child whose
    // parent sizes from that child's implicitHeight is circular and can collapse.
    width: parent ? parent.width : implicitWidth
    height: Math.min(column.height, available)
    implicitHeight: column.height
    clip: true

    Column {
        id: column
        width: Math.min(root.width, 260)
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: Theme.spacingLg

        Rectangle {
            visible: root.showIcon
            anchors.horizontalCenter: parent.horizontalCenter
            width: 48
            height: 48
            radius: Theme.c.emptyState.radius
            color: Theme.c.emptyState.background

            IconGlyph {
                anchors.centerIn: parent
                glyph: root.glyph
                iconSize: Theme.iconSizeXl
                iconColor: Theme.c.emptyState.icon
                spinning: root.glyphSpinning
            }
        }

        Text {
            id: titleText
            visible: root.title.length > 0
            width: parent.width
            text: root.title
            color: Theme.c.emptyState.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
            font.weight: Font.Medium
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        Text {
            id: hintText
            visible: root.hint.length > 0 && root.showHint
            width: parent.width
            text: root.hint
            color: Theme.c.emptyState.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        ThemedButton {
            id: actionButton
            visible: root.actionText.length > 0 && root.showAction
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.actionText
            variant: root.actionVariant
            onClicked: root.actionTriggered()
        }
    }
}
