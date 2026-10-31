import QtQuick
import Drift

// One setting: title and muted description on the left, its control right-aligned.
// Stacked rows put the control full width underneath, for lists and text fields.
Item {
    id: root

    property string title: ""
    property string description: ""
    property string keywords: ""
    property string searchId: ""
    property string query: ""
    property bool stacked: false
    property bool available: true
    default property alias control: controlHost.data

    readonly property bool matches: query.length === 0
        || (title + " " + description + " " + keywords).toLowerCase().indexOf(query) >= 0

    readonly property real contentHeight: stacked
        ? textColumn.height + Theme.spacingMd + controlHost.height
        : Math.max(textColumn.height, controlHost.height)

    Component.onCompleted: {
        if (root.searchId !== "")
            ShellBridge.registerAnchor(root.searchId, root)
    }
    Component.onDestruction: {
        if (root.searchId !== "")
            ShellBridge.unregisterAnchor(root.searchId, root)
    }

    visible: available && matches
    width: parent.width
    Accessible.role: Accessible.Grouping
    Accessible.name: root.title
    height: Theme.spacingLg + contentHeight + Theme.spacingLg + divider.height

    Column {
        id: textColumn
        x: 0
        y: Theme.spacingLg
        width: root.stacked ? root.width : root.width - controlHost.width - Theme.spacingXl
        spacing: Theme.spacingXs

        Text {
            width: parent.width
            text: root.title
            color: Theme.c.settingsRow.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeBase
            wrapMode: Text.WordWrap
        }

        Text {
            width: parent.width
            visible: text.length > 0
            text: root.description
            color: Theme.c.settingsRow.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            wrapMode: Text.WordWrap
        }
    }

    Item {
        id: controlHost
        x: root.stacked ? 0 : root.width - width
        y: root.stacked ? textColumn.y + textColumn.height + Theme.spacingMd
                        : Theme.spacingLg + (root.contentHeight - height) / 2
        width: root.stacked ? root.width : childrenRect.width
        height: childrenRect.height
    }

    Rectangle {
        id: divider
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: Theme.c.settingsRow.borderWidth
        color: Theme.c.settingsRow.divider
    }
}
