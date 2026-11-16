import QtQuick
import Drift

// Context menu for one bookmark flag.
ThemedContextMenu {
    id: menu

    property int bookmarkIndex: -1
    property string bookmarkLabel: ""
    property real bookmarkSeconds: 0
    // Empty means this flag is on the theme's marker colour.
    property string bookmarkColor: ""

    signal renameRequested(int index, string label)

    ThemedMenuItem {
        text: qsTr("Go to bookmark")
        icon.name: Theme.icons.bookmark
        onTriggered: EditorState.goToBookmark(menu.bookmarkIndex)
    }
    ThemedMenuItem {
        text: qsTr("Rename…")
        onTriggered: menu.renameRequested(menu.bookmarkIndex, menu.bookmarkLabel)
    }
    ThemedMenuSeparator { }

    // A row of shades rather than a submenu of colour names: picking a colour is a glance, and
    // the point of colouring flags is telling sections of a timeline apart at a glance too.
    // The first swatch is the theme's own colour, which is also how a flag is put back.
    Item {
        implicitWidth: colorRow.implicitWidth + Theme.spacingMd * 2
        implicitHeight: Theme.controlHeight

        Row {
            id: colorRow
            anchors.verticalCenter: parent.verticalCenter
            x: Theme.spacingMd
            spacing: Theme.spacingXs

            Repeater {
                model: ["", "#e5484d", "#f5a524", "#46a758", "#3e9bff", "#8e4ec6", "#e93d82"]

                delegate: Rectangle {
                    id: swatch

                    required property string modelData

                    readonly property bool current: menu.bookmarkColor.toLowerCase()
                                                    === swatch.modelData.toLowerCase()

                    width: 18
                    height: 18
                    radius: width / 2
                    color: swatch.modelData.length > 0 ? swatch.modelData
                                                       : Theme.c.timelineBookmarkMarker.foreground
                    border.width: swatch.current || swatchHover.hovered
                                  ? Theme.c.timelineBookmarkMarker.borderWidth * 2 : 0
                    border.color: Theme.c.timelineBookmarkMarker.border

                    HoverHandler { id: swatchHover }

                    TapHandler {
                        onTapped: {
                            EditorState.setBookmarkColor(menu.bookmarkIndex, swatch.modelData)
                            menu.close()
                        }
                    }

                    ThemedToolTip {
                        visible: swatchHover.hovered
                        text: swatch.modelData.length > 0 ? swatch.modelData : qsTr("Default colour")
                    }
                }
            }
        }
    }

    ThemedMenuSeparator { }
    ThemedMenuItem {
        text: qsTr("Delete")
        icon.name: Theme.icons.trash
        onTriggered: EditorState.removeBookmark(menu.bookmarkIndex)
    }
}
