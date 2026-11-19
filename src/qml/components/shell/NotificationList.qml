import QtQuick
import QtQuick.Controls.Basic
import Drift

// The bell's contents, shared by the desktop popup and the phone sheet: an available
// app update pinned on top, then every toast this session, newest first.
Item {
    id: root

    property real horizontalPadding: Theme.spacingLg
    property real rowHeight: Theme.controlHeight + Theme.spacingXl
    // The phone sheet titles itself, so only its Clear button is wanted.
    property bool showTitle: true

    signal updateRequested()
    // An entry's button ran, or the list was cleared; the host closes.
    signal actionTaken()

    // Real size, for the desktop popup to fit (#97): the rows as laid out, or the empty
    // message. The phone sheet fills its own height and ignores it.
    implicitHeight: header.height + updateRow.height
                    + (Toasts.history.count > 0 ? list.contentHeight
                                                : emptyLabel.implicitHeight + Theme.spacingXl * 2)

    // Phone updates come from the store, so the row only exists on desktop.
    readonly property bool showUpdate: Updates.updateAvailable && Qt.platform.os !== "android"

    function _accent(severity) {
        switch (severity) {
        case "error": return Theme.c.notificationList.accentError
        case "warning": return Theme.c.notificationList.accentWarning
        case "success": return Theme.c.notificationList.accentSuccess
        default: return Theme.c.notificationList.accent
        }
    }

    function _glyph(severity) {
        switch (severity) {
        case "error": return Theme.icons.error
        case "warning": return Theme.icons.warning
        case "success": return Theme.icons.success
        default: return Theme.icons.info
        }
    }

    Item {
        id: header
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: root.showTitle || Toasts.history.count > 0 ? Theme.controlHeight + Theme.spacingMd : 0

        Text {
            visible: root.showTitle
            anchors.left: parent.left
            anchors.leftMargin: root.horizontalPadding
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Notifications")
            color: Theme.c.notificationList.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
            font.weight: Font.Medium
        }

        ThemedButton {
            anchors.right: parent.right
            anchors.rightMargin: root.horizontalPadding - Theme.spacingSm
            anchors.verticalCenter: parent.verticalCenter
            visible: Toasts.history.count > 0
            variant: "ghost"
            text: qsTr("Clear")
            onClicked: {
                // Nothing left to read: close the popup or sheet, as an entry's button does. Close
                // first, so the popup keeps its size while it fades out over the rows.
                root.actionTaken()
                Toasts.clearHistory()
            }
        }
    }

    NotificationUpdateRow {
        id: updateRow
        showUpdate: root.showUpdate
        rowHeight: root.rowHeight
        horizontalPadding: root.horizontalPadding
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: header.bottom
        onUpdateRequested: root.updateRequested()
    }

    ThemedLabel {
        id: emptyLabel
        anchors.top: updateRow.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.WordWrap
        visible: Toasts.history.count === 0
        text: qsTr("No notifications yet.")
    }

    ListView {
        id: list
        anchors.top: updateRow.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true
        model: Toasts.history
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar { }

        delegate: NotificationHistoryRow {
            host: root
        }
    }
}
