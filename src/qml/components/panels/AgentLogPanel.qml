import QtQuick
import QtQuick.Controls.Basic
import Drift

PanelFrame {
    id: root

    property int expandedId: -1
    property bool followTail: true

    function timeText(ms) {
        return Qt.formatTime(new Date(ms), "HH:mm:ss")
    }

    function durationText(ms) {
        return ms < 1000 ? qsTr("%1 ms").arg(ms) : qsTr("%1 s").arg((ms / 1000).toFixed(1))
    }

    AssetsPanelHeader {
        id: header
        width: parent.width
        height: Theme.panelHeaderHeight
        title: qsTr("Agent log")
    }

    Item {
        id: status
        anchors.top: header.bottom
        width: parent.width
        height: 40

        Rectangle {
            id: dot
            anchors.left: parent.left
            anchors.leftMargin: Theme.pagePadding
            anchors.verticalCenter: parent.verticalCenter
            width: 8
            height: 8
            radius: 4
            color: EditorState.mcp.error.length > 0 ? Theme.destructive
                   : (EditorState.mcp.running ? Theme.constructive : Theme.mutedForeground)
        }

        Text {
            anchors.left: dot.right
            anchors.leftMargin: Theme.spacingSm
            anchors.right: connectBtn.left
            anchors.rightMargin: Theme.spacingSm
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideMiddle
            color: EditorState.mcp.error.length > 0 ? Theme.destructive : Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            text: EditorState.mcp.error.length > 0 ? EditorState.mcp.error
                  : (EditorState.mcp.running
                     ? (EditorState.mcp.url.length > 0 ? qsTr("Listening on %1").arg(EditorState.mcp.url)
                                                       : qsTr("Access is on"))
                     : qsTr("Agent access is off"))
        }

        ThemedButton {
            id: connectBtn
            anchors.right: clearBtn.left
            anchors.rightMargin: Theme.spacingSm
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Connect an agent…")
            variant: "secondary"
            onClicked: ShellBridge.main.openAgentAccess()
        }

        IconButton {
            id: clearBtn
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingSm
            anchors.verticalCenter: parent.verticalCenter
            glyph: Theme.icons.trash
            variant: "text"
            tooltip: qsTr("Clear log")
            enabled: EditorState.mcp.toolLog.length > 0
            onClicked: {
                root.expandedId = -1
                EditorState.mcp.clearToolLog()
            }
        }
    }

    EmptyState {
        anchors.top: status.bottom
        anchors.bottom: footer.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingLg
        visible: EditorState.mcp.toolLog.length === 0
        glyph: Theme.icons.linkTwo
        title: EditorState.mcp.running ? qsTr("No tool calls yet. What a connected agent does will show up here.")
                                       : qsTr("Turn on agent access to let an assistant edit this project.")
    }

    ListView {
        id: list
        anchors.top: status.bottom
        anchors.bottom: footer.top
        anchors.left: parent.left
        anchors.right: parent.right
        visible: EditorState.mcp.toolLog.length > 0
        clip: true
        model: EditorState.mcp.toolLog
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar {}

        onMovementEnded: root.followTail = list.atYEnd
        onCountChanged: if (root.followTail) Qt.callLater(list.positionViewAtEnd)
        onContentHeightChanged: if (root.followTail) Qt.callLater(list.positionViewAtEnd)

        delegate: Rectangle {
            id: row
            required property var modelData
            readonly property bool expanded: root.expandedId === modelData.id
            readonly property bool failed: !modelData.pending && !modelData.ok
            readonly property int imageId: EditorState.mcp.imageLog.count >= 0
                                           ? EditorState.mcp.imageLog.entryForCall(modelData.id) : -1
            readonly property url imageUrl: imageId >= 0 ? EditorState.mcp.imageLog.entry(imageId).url : ""

            width: ListView.view.width
            height: 28 + (expanded ? detail.height + Theme.spacingSm : 0)
            color: hover.hovered ? Theme.popoverHover : "transparent"

            Row {
                id: summaryRow
                x: Theme.pagePadding
                height: 28
                width: parent.width - 2 * Theme.pagePadding
                spacing: Theme.spacingSm

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 6
                    height: 6
                    radius: 3
                    color: row.modelData.pending ? Theme.accent
                           : (row.failed ? Theme.destructive : Theme.constructive)
                    SequentialAnimation on opacity {
                        running: row.modelData.pending
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.25; duration: 500 }
                        NumberAnimation { to: 1; duration: 500 }
                    }
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.timeText(row.modelData.time)
                    color: Theme.mutedForeground
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.modelData.tool
                    color: row.failed ? Theme.destructive : Theme.foreground
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.fontSizeXs
                    font.weight: Font.Medium
                }

                Rectangle {
                    visible: row.imageId >= 0
                    anchors.verticalCenter: parent.verticalCenter
                    width: 40
                    height: 22
                    radius: 2
                    color: Theme.c.agentViews.tileBackground
                    border.color: Theme.c.agentViews.tileBorder
                    border.width: 1
                    clip: true

                    Image {
                        anchors.fill: parent
                        anchors.margins: 1
                        source: row.imageUrl
                        sourceSize.height: 44
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: ShellBridge.main.openImageViewer(row.imageId)
                    }
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(0, summaryRow.width - x - durationText.width - summaryRow.spacing)
                    text: row.modelData.summary
                    elide: Text.ElideRight
                    color: Theme.mutedForeground
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                Text {
                    id: durationText
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.modelData.pending ? qsTr("running…") : root.durationText(row.modelData.ms)
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
            }

            TextEdit {
                id: detail
                visible: row.expanded
                x: Theme.pagePadding
                y: 28
                width: parent.width - 2 * Theme.pagePadding
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.Wrap
                text: row.failed && row.modelData.error.length > 0
                      ? row.modelData.summary + "\n\n" + row.modelData.error
                      : row.modelData.summary
                color: row.failed ? Theme.destructive : Theme.foreground
                font.family: Theme.monoFontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            HoverHandler { id: hover }
            TapHandler {
                onTapped: root.expandedId = row.expanded ? -1 : row.modelData.id
            }
        }
    }

    Rectangle {
        id: footer
        anchors.bottom: parent.bottom
        width: parent.width
        height: 36
        color: "transparent"

        Rectangle {
            width: parent.width
            height: 1
            color: Theme.border
        }

        Row {
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingSm
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingSm

            IconButton {
                glyph: Theme.icons.undo
                variant: "text"
                tooltip: qsTr("Undo")
                enabled: EditorState.undoAvailable
                onClicked: EditorState.undo()
            }

            IconButton {
                glyph: Theme.icons.redo
                variant: "text"
                tooltip: qsTr("Redo")
                enabled: EditorState.redoAvailable
                onClicked: EditorState.redo()
            }
        }
    }
}
