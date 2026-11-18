import QtQuick
import QtQuick.Controls.Basic
import Drift

PanelFrame {
    id: root

    readonly property var entries: [qsTr("Start")].concat(EditorState.undoHistory)
    readonly property int current: EditorState.undoIndex

    function followCurrent() {
        list.positionViewAtIndex(root.current, ListView.Contain)
    }

    onCurrentChanged: Qt.callLater(root.followCurrent)

    AssetsPanelHeader {
        id: header
        width: parent.width
        height: Theme.panelHeaderHeight
        title: qsTr("History")
    }

    ThemedButton {
        anchors.right: header.right
        anchors.rightMargin: Theme.spacingSm
        anchors.verticalCenter: header.verticalCenter
        text: qsTr("Clear history…")
        glyph: Theme.icons.trash
        variant: "ghost"
        enabled: EditorState.undoHistory.length > 0
        onClicked: {
            clearDialog.keepLast = 0
            keepField.value = 0
            clearDialog.open()
        }
    }

    ThemedDialog {
        id: clearDialog

        property int keepLast: 0

        title: qsTr("Clear history")
        acceptText: qsTr("Clear")
        acceptVariant: "destructive"
        preferredWidth: Theme.dialogWidthSm
        acceptOnReturn: false

        onAccepted: EditorState.clearUndoHistory(clearDialog.keepLast)

        contentItem: Column {
            spacing: Theme.spacingLg
            width: parent ? parent.width : 320

            ThemedLabel {
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("Frees undo memory. Undone steps (redo) are dropped too, and cleared steps can't be restored.")
            }
            ThemedLabel {
                text: qsTr("Keep last steps")
            }
            ThemedNumberField {
                id: keepField
                width: parent.width
                from: 0
                to: Math.max(1, EditorState.undoIndex)
                onEdited: v => clearDialog.keepLast = Math.round(v)
            }
        }
    }

    EmptyState {
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingLg
        visible: EditorState.undoHistory.length === 0
        glyph: Theme.icons.undo
        title: qsTr("Nothing to undo yet. Your edits will be listed here.")
    }

    ListView {
        id: list
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        visible: EditorState.undoHistory.length > 0
        clip: true
        model: root.entries
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar {}
        Component.onCompleted: root.followCurrent()

        delegate: Rectangle {
            required property int index
            required property string modelData
            readonly property bool isCurrent: index === root.current

            width: ListView.view.width
            height: 28
            color: isCurrent ? Theme.accent : (hover.hovered ? Theme.popoverHover : "transparent")

            Text {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: Theme.pagePadding
                anchors.rightMargin: Theme.pagePadding
                anchors.verticalCenter: parent.verticalCenter
                text: modelData
                elide: Text.ElideRight
                color: isCurrent ? Theme.accentForeground : Theme.foreground
                opacity: index > root.current ? 0.45 : 1
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                font.italic: index === 0
            }

            HoverHandler { id: hover }
            TapHandler {
                enabled: !isCurrent
                onTapped: EditorState.setUndoIndex(index)
            }
        }
    }
}
