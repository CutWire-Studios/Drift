import QtQuick
import Drift

// One settings category: large title, then its groups and rows.
Column {
    id: root

    property string pageId: ""
    property string title: ""
    property string query: ""
    default property alias content: body.data

    readonly property bool hasMatches: {
        if (root.query.length === 0)
            return true
        for (let i = 0; i < body.children.length; ++i) {
            const child = body.children[i]
            if (child.matches !== undefined && child.visible)
                return true
        }
        return false
    }

    width: parent.width
    spacing: Theme.spacingXl

    Text {
        width: parent.width
        visible: !Theme.phone
        text: root.title
        color: Theme.c.settingsPage.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.c.settingsPage.titleSize
        font.weight: Font.DemiBold
        wrapMode: Text.WordWrap
    }

    Column {
        id: body
        width: parent.width
    }
}
