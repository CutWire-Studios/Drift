pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
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
    property var history: []
    default property alias actions: actionRow.data

    // Phone only. With tabs the bar is the page's one header row: tabs on the left, a search
    // icon and the actions on the right, and the field replaces the tabs while searching.
    // Without tabs the field is always out. Desktop ignores all of this.
    readonly property bool phone: Theme.phone
    property var tabs: []
    property string currentTabId: ""
    property bool searchEnabled: true
    property bool searchOpen: false
    readonly property bool fieldShown: !phone || tabs.length === 0 || searchOpen || field.text.length > 0
    signal tabSelected(string tabId)
    function closeSearch() {
        field.text = ""
        root.searchOpen = false
        field.focus = false
    }

    function commitHistory() {
        const query = field.text.trim()
        if (query.length === 0 || root.historyKey.length === 0)
            return
        const rest = root.history.filter(function(entry) {
            return entry.toLowerCase() !== query.toLowerCase()
        })
        root.storeHistory([query].concat(rest).slice(0, root.historyLimit))
    }

    function removeHistoryEntry(index) {
        const next = root.history.slice()
        next.splice(index, 1)
        root.storeHistory(next)
    }

    function storeHistory(entries) {
        root.history = entries
        EditorState.preferences.setAssetSearchHistory(root.historyKey, entries)
    }

    function loadHistory() {
        root.history = root.historyKey.length > 0
                ? EditorState.preferences.assetSearchHistory(root.historyKey) : []
    }

    onHistoryKeyChanged: loadHistory()
    Component.onCompleted: loadHistory()

    width: parent ? parent.width : 0
    readonly property real topInset: phone ? 0 : Theme.pagePadding
    implicitHeight: phone ? Theme.androidRowHeight : Theme.pagePadding + Theme.controlHeight
    height: implicitHeight

    ThemedTextField {
        id: field
        x: Theme.pagePadding
        y: root.phone ? (root.height - height) / 2 : Theme.pagePadding
        visible: root.fieldShown
        width: root.width - Theme.pagePadding * 2
               - (actionRow.visible && actionRow.width > 0 ? actionRow.width + Theme.spacingSm : 0)
               - (closeButton.visible ? closeButton.width + Theme.spacingSm : 0)
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

    AndroidTabStrip {
        id: tabStrip
        visible: root.phone && root.tabs.length > 0 && !root.fieldShown
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacingSm
        anchors.verticalCenter: parent.verticalCenter
        width: Math.min(naturalWidth, root.width - Theme.pagePadding - searchToggle.width - actionRow.width)
        tabs: root.tabs
        currentId: root.currentTabId
        onTabSelected: tabId => root.tabSelected(tabId)
    }

    IconButton {
        id: searchToggle
        visible: root.phone && root.tabs.length > 0 && root.searchEnabled && !root.fieldShown
        anchors.right: actionRow.left
        anchors.verticalCenter: parent.verticalCenter
        glyph: Theme.icons.search
        variant: "text"
        tooltip: qsTr("Search")
        onClicked: {
            root.searchOpen = true
            field.forceActiveFocus()
        }
    }

    IconButton {
        id: closeButton
        visible: root.phone && root.tabs.length > 0 && root.fieldShown
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingSm
        anchors.verticalCenter: parent.verticalCenter
        glyph: Theme.icons.x
        variant: "text"
        tooltip: qsTr("Close search")
        onClicked: root.closeSearch()
    }

    Row {
        id: actionRow
        visible: !(root.phone && root.tabs.length > 0 && root.fieldShown)
        anchors.right: parent.right
        anchors.rightMargin: root.phone ? Theme.spacingSm : Theme.pagePadding
        y: root.phone ? (root.height - height) / 2 : Theme.pagePadding
        height: Theme.controlHeight
        spacing: Theme.spacingXs
    }
}
