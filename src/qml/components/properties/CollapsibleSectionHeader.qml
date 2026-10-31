import QtQuick
import Drift

Item {
    property string title: ""
    property string tooltip: ""
    property bool collapsible: true
    property bool expanded: true
    property bool showSwitch: false
    property bool switchChecked: false
    property string switchTooltip: ""
    property bool showSeparator: true
    property string searchId: ""
    signal expandToggled()
    signal switchToggled(bool checked)
    id: root
    width: parent.width

    Component.onCompleted: {
        if (root.searchId !== "")
            ShellBridge.registerAnchor(root.searchId, root)
    }
    Component.onDestruction: {
        if (root.searchId !== "")
            ShellBridge.unregisterAnchor(root.searchId, root)
    }

    ThemeSlot {
        anchors.fill: parent
        z: -1
        slot: "CollapsibleSectionHeaderBackground"
        control: root
        fallback: Component {
            Rectangle {
                anchors.fill: parent
                color: "transparent"
            }
        }
    }
    height: Math.max(Theme.controlHeightSm,
                     titleLabel.implicitHeight,
                     root.showSwitch ? 20 : 0) + Theme.spacingMd

    Row {
        id: titleRow
        anchors.left: parent.left
        anchors.right: root.showSwitch ? switchControl.left : parent.right
        anchors.rightMargin: root.showSwitch ? Theme.spacingSm : 0
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: root.showSeparator ? Theme.spacingMd / 2 : 0
        spacing: Theme.spacingSm

        Item {
            width: root.collapsible ? 14 : 0
            height: 14
            visible: root.collapsible
            anchors.verticalCenter: parent.verticalCenter

            IconGlyph {
                anchors.centerIn: parent
                glyph: root.expanded ? Theme.icons.chevronDown : Theme.icons.chevronRight
                iconSize: 12
                iconColor: Theme.c.collapsibleSectionHeader.icon
            }
        }

        Text {
            id: titleLabel
            width: Math.max(0, titleRow.width - (root.collapsible ? 14 + titleRow.spacing : 0))
            text: root.title
            elide: Text.ElideRight
            color: Theme.c.collapsibleSectionHeader.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
            font.weight: Font.DemiBold
            anchors.verticalCenter: parent.verticalCenter

            HoverHandler { id: titleHover }
            ThemedToolTip {
                text: root.tooltip
                visible: root.tooltip.length > 0 && titleHover.hovered
            }
        }
    }

    MouseArea {
        anchors.fill: titleRow
        enabled: root.collapsible
        cursorShape: root.collapsible ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: {
            root.expandToggled()
            Haptics.detent()
        }
    }

    ThemedSwitch {
        id: switchControl
        anchors.right: parent.right
        anchors.verticalCenter: titleRow.verticalCenter
        visible: root.showSwitch
        checked: root.switchChecked
        tooltip: root.switchTooltip
        // Title already names the feature; keep only the track visible.
        text: ""
        width: 36
        clip: true
        onToggled: root.switchToggled(checked)
    }
}
