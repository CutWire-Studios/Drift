import QtQuick
import Drift

Item {
    property var host
    id: row

    required property int toastId
    required property string severity
    required property string message
    required property int repeats
    required property string actionText
    required property string time
    required property bool unread

    width: ListView.view.width
    height: Math.max(host.rowHeight, body.implicitHeight + Theme.spacingLg * 2)

    function copyMessage() {
        const start = Math.max(0, messageEdit.text.indexOf(row.message))
        messageEdit.select(start, start + row.message.length)
        messageEdit.copy()
        messageEdit.deselect()
        copiedFeedback.restart()
    }

    HoverHandler { id: rowHover }
    Timer { id: copiedFeedback; interval: 1500 }

    IconGlyph {
        id: rowIcon
        anchors.left: parent.left
        anchors.leftMargin: host.horizontalPadding
        anchors.top: parent.top
        anchors.topMargin: Theme.spacingLg
        glyph: host._glyph(row.severity)
        iconSize: Theme.iconSizeBase
        iconColor: {
            switch (row.severity) {
            case "error": return Theme.c.notificationHistoryRow.accentError
            case "warning": return Theme.c.notificationHistoryRow.accentWarning
            case "success": return Theme.c.notificationHistoryRow.accentSuccess
            default: return Theme.c.notificationHistoryRow.accent
            }
        }
    }

    Column {
        id: body
        anchors.left: rowIcon.right
        anchors.leftMargin: Theme.spacingMd
        anchors.right: copyButton.left
        anchors.rightMargin: Theme.spacingMd
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2

        // Selectable so an error can be copied (#97), with a mouse only: with selectByMouse on,
        // the first touch drag on a message doesn't scroll the list. Touch has the Copy button.
        TextEdit {
            id: messageEdit
            width: parent.width
            readOnly: true
            selectByMouse: !Theme.touchInput
            textFormat: TextEdit.PlainText
            wrapMode: TextEdit.Wrap
            // replace, not chained arg(): a "%1" inside the message would take the count.
            text: row.repeats > 1
                  ? qsTr("%1  (×%2)").replace("%2", () => String(row.repeats)).replace("%1", () => row.message)
                  : row.message
            color: Theme.c.notificationHistoryRow.foreground
            selectionColor: Theme.c.notificationHistoryRow.selection
            selectedTextColor: Theme.c.notificationHistoryRow.selectionForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
            font.weight: row.unread ? Font.Medium : Font.Normal
            // Ctrl+C is also the app-wide "copy clips" shortcut; with text selected here it
            // copies the text.
            Keys.onShortcutOverride: (event) => {
                if (event.matches(StandardKey.Copy) && messageEdit.selectedText.length > 0)
                    event.accepted = true
            }
            Keys.onPressed: (event) => {
                if (event.matches(StandardKey.Copy) && messageEdit.selectedText.length > 0) {
                    messageEdit.copy()
                    event.accepted = true
                }
            }
        }

        Text {
            width: parent.width
            text: row.time
            color: Theme.c.notificationHistoryRow.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }

    // Shown on hover or keyboard focus with a pointer; always on touch, where text isn't
    // drag-selected.
    IconButton {
        id: copyButton
        anchors.right: rowAction.visible ? rowAction.left : unreadDot.left
        anchors.rightMargin: Theme.spacingSm
        anchors.verticalCenter: parent.verticalCenter
        opacity: Theme.touchInput || rowHover.hovered || visualFocus || copiedFeedback.running ? 1 : 0
        glyph: copiedFeedback.running ? Theme.icons.check : Theme.icons.copy
        iconSize: Theme.iconSizeSm
        variant: "ghost"
        tooltip: copiedFeedback.running ? qsTr("Copied") : qsTr("Copy message")
        onClicked: row.copyMessage()
    }

    ThemedButton {
        id: rowAction
        anchors.right: unreadDot.left
        anchors.rightMargin: Theme.spacingMd
        anchors.verticalCenter: parent.verticalCenter
        visible: row.actionText.length > 0
        text: row.actionText
        variant: "secondary"
        onClicked: {
            Toasts.trigger(row.toastId)
            host.actionTaken()
        }
    }

    Rectangle {
        id: unreadDot
        anchors.right: parent.right
        anchors.rightMargin: host.horizontalPadding
        anchors.verticalCenter: parent.verticalCenter
        width: 8
        height: 8
        radius: Theme.c.notificationHistoryRow.indicatorRadius
        color: Theme.c.notificationHistoryRow.indicator
        opacity: row.unread ? 1 : 0
    }

    Rectangle {
        anchors.left: parent.left
        anchors.leftMargin: host.horizontalPadding
        anchors.right: parent.right
        anchors.rightMargin: host.horizontalPadding
        anchors.bottom: parent.bottom
        height: Theme.c.notificationHistoryRow.borderWidth
        color: Theme.c.notificationHistoryRow.divider
    }
}
