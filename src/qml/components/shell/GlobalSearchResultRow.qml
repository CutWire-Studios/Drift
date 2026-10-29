import QtQuick
import Drift

Rectangle {
    required property int index
    required property string kind
    required property string label
    required property string sub
    required property string hint
    required property string shortcut
    required property bool enabled
    required property string target
    property int highlightIndex
    property var kindLabels
    signal activated(int rowIndex)
    id: resultRow

    readonly property bool runnable: enabled && target !== ""
    readonly property string detail: [sub, hint].filter(s => s && s.length > 0).join("  ·  ")
    readonly property string shortcutText: shortcut.length > 0 ? Theme.shortcutDisplay(shortcut) : ""

    width: ListView.view.width
    height: column.implicitHeight + Theme.spacingMd * 2
    radius: Theme.c.globalSearchResultRow.radius
    color: rowMouse.containsMouse || highlightIndex === index
           ? Theme.c.globalSearchResultRow.backgroundHover : "transparent"
    opacity: enabled ? 1.0 : 0.5

    Column {
        id: column
        anchors.left: parent.left
        anchors.right: badges.left
        anchors.leftMargin: Theme.spacingLg
        anchors.rightMargin: Theme.spacingLg
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.spacingXs

        Text {
            width: parent.width
            text: resultRow.label
            elide: Text.ElideRight
            color: Theme.c.globalSearchResultRow.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
        }

        Text {
            width: parent.width
            visible: text.length > 0
            text: resultRow.detail
            elide: Text.ElideRight
            color: Theme.c.globalSearchResultRow.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }

    Row {
        id: badges
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingLg
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.spacingLg

        Text {
            visible: text.length > 0
            text: resultRow.shortcutText
            color: Theme.c.globalSearchResultRow.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        Text {
            text: kindLabels[resultRow.kind] || ""
            color: Theme.c.globalSearchResultRow.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            font.weight: Font.Medium
        }
    }

    MouseArea {
        id: rowMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: resultRow.runnable ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: resultRow.activated(resultRow.index)
    }
}
