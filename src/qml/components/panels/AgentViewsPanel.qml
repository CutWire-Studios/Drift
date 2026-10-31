pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Drift

PanelFrame {
    id: root
    SearchAnchor {
        searchId: "agent-views"
        title: qsTr("Agent Views")
        keywords: "agent images gallery frames audio sheet captures ai screenshots"
    }

    readonly property var imageLog: EditorState.mcp.imageLog
    readonly property int cellMin: 190
    readonly property int columns: Math.max(1, Math.floor((width - Theme.spacingMd) / cellMin))

    function timeText(ms) {
        return Qt.formatTime(new Date(ms), "HH:mm:ss")
    }

    function rangeText(start, end) {
        if (start < 0)
            return ""
        const fmt = (s) => Math.floor(s / 60) + ":" + String(Math.floor(s % 60)).padStart(2, "0")
        return end - start > 0.5 ? qsTr("%1 – %2").arg(fmt(start)).arg(fmt(end)) : fmt(start)
    }

    AssetsPanelHeader {
        id: panelHeader
        width: parent.width
        height: Theme.panelHeaderHeight
        title: qsTr("Agent Views")
    }

    EmptyState {
        anchors.top: panelHeader.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingLg
        visible: root.imageLog.count === 0
        glyph: Theme.icons.image
        title: qsTr("No images yet.")
        hint: qsTr("Images the AI requests, such as frames, audio sheets and captures, appear here during this session.")
    }

    GridView {
        id: grid
        anchors.top: panelHeader.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingSm
        visible: root.imageLog.count > 0
        clip: true
        model: root.imageLog
        cellWidth: Math.floor(width / root.columns)
        cellHeight: Math.round(cellWidth * 0.75) + 40
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar {}

        delegate: Item {
            id: cell
            required property int entryId
            required property string tool
            required property url url
            required property double time
            required property double rangeStart
            required property double rangeEnd

            width: grid.cellWidth
            height: grid.cellHeight

            Rectangle {
                anchors.fill: parent
                anchors.margins: Theme.spacingXs
                radius: Theme.c.agentViews.tileRadius
                color: hover.hovered ? Theme.c.agentViews.tileHover : Theme.c.agentViews.tileBackground
                border.color: Theme.c.agentViews.tileBorder
                border.width: 1
                clip: true

                Image {
                    id: thumb
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: caption.top
                    anchors.margins: Theme.spacingXs
                    source: cell.url
                    sourceSize.width: 480
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    mipmap: true
                }

                Column {
                    id: caption
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: Theme.spacingSm
                    height: 32

                    Text {
                        width: parent.width
                        text: cell.tool
                        elide: Text.ElideRight
                        color: Theme.c.agentViews.foreground
                        font.family: Theme.monoFontFamily
                        font.pixelSize: Theme.fontSizeXs
                        font.weight: Font.Medium
                    }

                    Text {
                        width: parent.width
                        readonly property string range: root.rangeText(cell.rangeStart, cell.rangeEnd)
                        text: range.length > 0 ? root.timeText(cell.time) + "  ·  " + range : root.timeText(cell.time)
                        elide: Text.ElideRight
                        color: Theme.c.agentViews.foregroundMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                    }
                }

                HoverHandler {
                    id: hover
                    cursorShape: Qt.PointingHandCursor
                }
                TapHandler {
                    onTapped: ShellBridge.main.openImageViewer(cell.entryId)
                }
            }
        }
    }
}
