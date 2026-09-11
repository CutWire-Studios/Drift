import QtQuick
import QtQuick.Controls.Basic
import Drift

// Header Agent button. Session-only localhost MCP, written for the person
// connecting an assistant — not for someone reading a protocol spec.
ThemedDialog {
    id: root

    title: qsTr("Agent access")
    preferredWidth: Theme.dialogWidthMd
    showAccept: false
    rejectText: qsTr("Close")

    function openDialog() {
        body.detailsOpen = false
        open()
    }

    contentItem: Flickable {
        id: contentFlick
        width: parent ? parent.width : Theme.dialogWidthMd
        implicitHeight: Math.min(body.height, root.availableContentHeight)
        contentWidth: width
        contentHeight: body.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentHeight > height
        ScrollBar.vertical: AppScrollBar {
            policy: contentFlick.contentHeight > contentFlick.height
                    ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
        }

        AgentAccessControls {
            id: body
            width: contentFlick.width
        }
    }
}
