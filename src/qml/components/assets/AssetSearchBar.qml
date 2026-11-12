pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtCore
import Drift
// Top row of the catalog tabs: the search field, which also names the tab (these tabs
// have no panel header), with the tab's actions as icon buttons at its right end.
// Enter or applying a result records the query; focusing the empty field lists recent ones.
Item {
    id: root

    property alias text: field.text
    property alias placeholderText: field.placeholderText
    // Names this browser's history in settings. Empty turns history off.
    property string historyKey: ""
    readonly property int historyLimit: 10
    readonly property var history: JSON.parse(store.json)
    default property alias actions: actionRow.data

    function commitHistory() {
        const query = field.text.trim()
        if (query.length === 0 || root.historyKey.length === 0)
            return
        const rest = root.history.filter(function(entry) {
            return entry.toLowerCase() !== query.toLowerCase()
        })
        store.json = JSON.stringify([query].concat(rest).slice(0, root.historyLimit))
    }

    function removeHistoryEntry(index) {
        const next = root.history.slice()
        next.splice(index, 1)
        store.json = JSON.stringify(next)
    }

    Settings {
        id: store
        category: "assetSearchHistory/" + root.historyKey
        property string json: "[]"
    }

    width: parent ? parent.width : 0
    implicitHeight: Theme.pagePadding + Theme.controlHeight
    height: implicitHeight

    ThemedTextField {
        id: field
        x: Theme.pagePadding
        y: Theme.pagePadding
        width: root.width - Theme.pagePadding * 2
               - (actionRow.width > 0 ? actionRow.width + Theme.spacingSm : 0)
        font.family: Theme.fontFamily
        rightPadding: clear.visible ? clear.width + Theme.spacingSm * 2 : Theme.spacingLg

        onAccepted: root.commitHistory()
        onTextChanged: if (text.length > 0) historyPopup.close()
        onActiveFocusChanged: {
            if (activeFocus && text.length === 0 && root.history.length > 0)
                historyPopup.open()
            else if (!activeFocus)
                historyPopup.close()
        }
        Keys.onEscapePressed: (event) => {
            if (historyPopup.opened)
                historyPopup.close()
            else if (field.text.length > 0)
                field.text = ""
            else
                event.accepted = false
        }
    }

    IconButton {
        id: clear
        anchors.right: field.right
        anchors.rightMargin: Theme.spacingSm
        anchors.verticalCenter: field.verticalCenter
        visible: field.text.length > 0
        glyph: Theme.icons.x
        variant: "ghost"
        buttonSize: Theme.controlHeightSm
        iconSize: Theme.iconSizeSm
        tooltip: qsTr("Clear search")
        onClicked: {
            field.text = ""
            field.forceActiveFocus()
        }
    }

    Popup {
        id: historyPopup
        parent: field
        y: field.height + Theme.spacingXs
        width: field.width
        padding: Theme.spacingXs
        popupType: Popup.Item
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

        background: Rectangle {
            radius: Theme.c.themedContextMenu.radius
            color: Theme.c.themedContextMenu.background
            border.width: Theme.c.themedContextMenu.borderWidth
            border.color: Theme.c.themedContextMenu.border
        }

        contentItem: Column {
            width: historyPopup.availableWidth

            Repeater {
                model: root.history

                delegate: Item {
                    id: entry
                    required property var modelData
                    required property int index
                    width: parent ? parent.width : 0
                    height: Theme.controlHeightSm

                    HoverHandler { id: entryHover }

                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.c.themedMenuItem.radius
                        color: entryHover.hovered ? Theme.c.themedMenuItem.backgroundHighlighted : "transparent"
                    }

                    Item {
                        id: label
                        anchors.left: parent.left
                        anchors.right: removeEntry.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom

                        TapHandler {
                            onTapped: field.text = entry.modelData
                        }

                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.spacingMd
                            anchors.rightMargin: Theme.spacingSm
                            verticalAlignment: Text.AlignVCenter
                            text: entry.modelData
                            color: Theme.c.themedMenuItem.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeSm
                            elide: Text.ElideRight
                        }
                    }

                    IconButton {
                        id: removeEntry
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        glyph: Theme.icons.x
                        variant: "ghost"
                        buttonSize: Theme.controlHeightSm
                        iconSize: Theme.iconSizeSm
                        tooltip: qsTr("Remove from history")
                        onClicked: root.removeHistoryEntry(entry.index)
                    }
                }
            }
        }
    }

    Row {
        id: actionRow
        anchors.right: parent.right
        anchors.rightMargin: Theme.pagePadding
        y: Theme.pagePadding
        height: Theme.controlHeight
        spacing: Theme.spacingXs
    }
}
