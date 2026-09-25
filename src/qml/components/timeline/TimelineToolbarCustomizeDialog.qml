import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

// Arranges the timeline toolbar: one list where everything above the "More menu" divider
// is a toolbar button and everything below it goes in the More menu, both in list order.
ThemedDialog {
    id: root

    // The TimelineToolbar being customised; supplies action metadata, defaults and layout.
    property var toolbar

    title: qsTr("Customize timeline toolbar")
    acceptText: qsTr("Save")
    preferredWidth: Theme.dialogWidthSm

    readonly property real rowHeight: 34

    ListModel { id: entries }

    function load(toolbarItems, menuItems) {
        entries.clear()
        for (const id of toolbarItems)
            entries.append({ "itemId": id })
        entries.append({ "itemId": "divider" })
        for (const id of menuItems)
            entries.append({ "itemId": id })
    }

    function dividerIndex() {
        for (var i = 0; i < entries.count; i++) {
            if (entries.get(i).itemId === "divider")
                return i
        }
        return entries.count
    }

    function moveEntry(from, to) {
        if (to < 0 || to >= entries.count || to === from)
            return
        entries.move(from, to, 1)
    }

    onOpened: load(toolbar.layout.toolbar, toolbar.layout.menu)

    onAccepted: {
        const toolbarItems = []
        const menuItems = []
        const split = dividerIndex()
        for (var i = 0; i < entries.count; i++) {
            const id = entries.get(i).itemId
            if (i < split)
                toolbarItems.push(id)
            else if (i > split)
                menuItems.push(id)
        }
        EditorState.setTimelineToolbarLayout(toolbarItems, menuItems)
    }

    contentItem: Column {
        width: parent ? parent.width : Theme.dialogWidthSm
        spacing: Theme.spacingMd

        ThemedLabel {
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTr("Drag items to reorder them. Items above the divider are toolbar buttons; the rest are in the More menu.")
        }

        Row {
            spacing: Theme.spacingMd

            ThemedButton {
                text: qsTr("Add separator")
                onClicked: entries.insert(root.dividerIndex(), { "itemId": "separator" })
            }
            ThemedButton {
                text: qsTr("Reset to defaults")
                onClicked: root.load(root.toolbar.defaultToolbarItems, root.toolbar.defaultMenuItems)
            }
        }

        ListView {
            id: list
            width: parent.width
            height: Math.min(contentHeight, Math.max(root.rowHeight * 4,
                             root.availableContentHeight - 120))
            clip: true
            model: entries
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar {}

            moveDisplaced: Transition {
                NumberAnimation { properties: "y"; duration: Theme.durationFast; easing.type: Theme.easing }
            }

            delegate: Rectangle {
                id: row
                required property string itemId
                required property int index
                readonly property bool isDivider: itemId === "divider"
                readonly property bool isSeparator: itemId === "separator"

                width: list.width
                height: root.rowHeight
                radius: Theme.radiusXs
                color: isDivider ? "transparent"
                       : (grip.pressed ? Theme.panelAccent : "transparent")

                // Divider: the fixed boundary between toolbar and menu.
                Row {
                    visible: row.isDivider
                    anchors.fill: parent
                    spacing: Theme.spacingMd

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: (parent.width - dividerLabel.width) / 2 - parent.spacing
                        height: Theme.borderWidth
                        color: Theme.panelBorder
                    }
                    Text {
                        id: dividerLabel
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("More menu")
                        color: Theme.mutedForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                        font.weight: Font.DemiBold
                    }
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: (parent.width - dividerLabel.width) / 2 - parent.spacing
                        height: Theme.borderWidth
                        color: Theme.panelBorder
                    }
                }

                IconGlyph {
                    id: gripGlyph
                    visible: !row.isDivider
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacingSm
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: Theme.icons.gripVertical
                    iconSize: Theme.iconSizeMd
                    iconColor: Theme.mutedForeground

                    // Moves the row live as the pointer crosses other rows; the model
                    // move keeps this delegate, so the press survives it.
                    MouseArea {
                        id: grip
                        anchors.fill: parent
                        anchors.margins: -6
                        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                        preventStealing: true
                        onPositionChanged: (mouse) => {
                            if (!pressed)
                                return
                            const p = mapToItem(list.contentItem, mouse.x, mouse.y)
                            const target = list.indexAt(1, p.y)
                            if (target >= 0)
                                root.moveEntry(row.index, target)
                        }
                    }
                }

                IconGlyph {
                    id: actionGlyph
                    visible: !row.isDivider && !row.isSeparator
                    anchors.left: gripGlyph.right
                    anchors.leftMargin: Theme.spacingLg
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: visible ? root.toolbar.actionMeta[row.itemId].glyph : ""
                    iconSize: Theme.iconSizeBase
                    iconColor: Theme.panelForeground
                }

                Text {
                    visible: !row.isDivider
                    anchors.left: row.isSeparator ? gripGlyph.right : actionGlyph.right
                    anchors.leftMargin: Theme.spacingLg
                    anchors.right: rowButtons.left
                    anchors.rightMargin: Theme.spacingSm
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.isSeparator ? qsTr("— Separator —")
                                          : (row.isDivider ? "" : root.toolbar.actionMeta[row.itemId].label)
                    color: row.isSeparator ? Theme.mutedForeground : Theme.panelForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    elide: Text.ElideRight
                }

                Row {
                    id: rowButtons
                    visible: !row.isDivider
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 0

                    IconButton {
                        glyph: Theme.icons.chevronUp
                        variant: "text"
                        tooltip: qsTr("Move up")
                        enabled: row.index > 0
                        onClicked: root.moveEntry(row.index, row.index - 1)
                    }
                    IconButton {
                        glyph: Theme.icons.chevronDown
                        variant: "text"
                        tooltip: qsTr("Move down")
                        enabled: row.index < entries.count - 1
                        onClicked: root.moveEntry(row.index, row.index + 1)
                    }
                    IconButton {
                        visible: row.isSeparator
                        glyph: Theme.icons.x
                        variant: "text"
                        tooltip: qsTr("Remove separator")
                        onClicked: entries.remove(row.index)
                    }
                }
            }
        }
    }
}
