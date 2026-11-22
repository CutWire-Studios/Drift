pragma ComponentBehavior: Bound
import QtQuick
import Drift

// One panel slot drawn as a mock block: tab strip on top, the active panel's name below.
// Drag it (or a single tab) onto another block or back to the palette.
Rectangle {
    id: root

    required property var designer
    required property var node
    required property var path

    readonly property var panels: node.panels
    readonly property var activePanel: panels[Math.min(node.active, panels.length - 1)]
    readonly property real headerHeight: Theme.controlHeightSm
    readonly property bool hovered: designer.hoverSlot === root
    readonly property string zone: hovered ? designer.hoverZone : ""

    readonly property bool dragAllowed: {
        const dragged = root.designer.dragPanels
        if (dragged.length === 0)
            return false
        const mine = root.panels.filter(p => dragged.some(d => d.type === p.type))
        if (mine.length === root.panels.length)
            return false
        return !(root.zone === "center" && mine.length > 0)
    }
    readonly property string targetType: {
        const dragged = root.designer.dragPanels
        const free = root.panels.find(p => !dragged.some(d => d.type === p.type))
        return free ? free.type : root.panels[0].type
    }

    radius: Theme.radiusMd
    color: Theme.panelSecondaryBg
    border.width: Theme.c.designerSlot.borderWidth
    border.color: Theme.panelBorder
    clip: true

    DesignerDragArea {
        anchors.fill: parent
        designer: root.designer
        panels: root.panels
        onContextRequested: root.designer.openMenu(root.activePanel.type, root, 0, 0)
    }

    Column {
        anchors.centerIn: parent
        width: parent.width - Theme.spacingLg * 2
        spacing: Theme.spacingXs
        visible: root.height > root.headerHeight + 24

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            text: root.designer.labelOf(root.activePanel.type)
            color: Theme.panelForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeBase
            font.weight: Font.Medium
        }

        Text {
            width: parent.width
            visible: text.length > 0
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            text: root.designer.tabLabel(root.activePanel.type, root.activePanel.tab)
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }

    Rectangle {
        id: header
        width: parent.width
        height: root.headerHeight
        color: Theme.panelAccent

        Row {
            id: tabRow
            anchors.left: parent.left
            anchors.right: more.left
            height: parent.height
            clip: true

            Repeater {
                model: root.panels

                Rectangle {
                    id: tab

                    required property var modelData
                    required property int index
                    readonly property bool isActive: index === root.node.active

                    height: header.height
                    width: Math.min(tabLabel.implicitWidth + Theme.spacingLg * 2, tabRow.width / Math.max(1, root.panels.length))
                    color: isActive && root.panels.length > 1 ? Theme.panelSecondaryBg : "transparent"

                    Text {
                        id: tabLabel
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacingLg
                        anchors.rightMargin: Theme.spacingLg
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        text: root.designer.labelOf(tab.modelData.type)
                        color: tab.isActive ? Theme.panelForeground : Theme.mutedForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                        font.weight: tab.isActive ? Font.DemiBold : Font.Normal
                    }

                    DesignerDragArea {
                        anchors.fill: parent
                        designer: root.designer
                        panels: [tab.modelData]
                        onTapped: root.designer.activatePanel(tab.modelData.type)
                        onContextRequested: root.designer.openMenu(tab.modelData.type, tab, 0, tab.height)
                    }
                }
            }
        }

        Item {
            id: more
            anchors.right: parent.right
            width: header.height
            height: header.height

            IconGlyph {
                anchors.centerIn: parent
                glyph: Theme.icons.ellipsis
                iconSize: Theme.iconSizeSm
                iconColor: Theme.mutedForeground
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.designer.openMenu(root.activePanel.type, more, 0, more.height)
            }
        }
    }

    Rectangle {
        id: indicator
        visible: root.hovered && root.dragAllowed
        color: Qt.alpha(Theme.c.designerSlot.indicator, Theme.c.designerSlot.indicatorOpacity)
        border.width: Theme.c.designerSlot.indicatorBorderWidth
        border.color: Theme.accent
        radius: Theme.radiusSm
        x: root.zone === "right" ? root.width / 2 : 0
        y: root.zone === "bottom" ? root.height / 2 : 0
        width: root.zone === "left" || root.zone === "right" ? root.width / 2 : root.width
        height: root.zone === "top" || root.zone === "bottom" ? root.height / 2 : root.height

        Text {
            anchors.centerIn: parent
            visible: root.zone === "center"
            text: qsTr("Add as tab")
            color: Theme.accentForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }

    function zoneAt(x, y) {
        const left = x / width
        const right = 1 - left
        const top = y / height
        const bottom = 1 - top
        const nearest = Math.min(left, right, top, bottom)
        if (nearest > 0.25)
            return "center"
        if (nearest === left)
            return "left"
        if (nearest === right)
            return "right"
        return nearest === top ? "top" : "bottom"
    }

    Component.onCompleted: designer.registerSlot(root)
    Component.onDestruction: designer.unregisterSlot(root)
}
