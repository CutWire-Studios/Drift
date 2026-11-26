import QtQuick
import QtQuick.Layouts
import Drift

// Non-modal: the project folder (or a script in it) has a problem. The open project keeps its last
// good state and the folder stays watched; fixing the file clears this by itself.
Rectangle {
    id: root

    readonly property var folder: EditorState.projectFile.folder
    readonly property bool shown: folder.active && folder.errorText.length > 0

    width: parent ? parent.width : 0
    height: shown ? row.implicitHeight + Theme.spacingMd * 2 : 0
    visible: shown
    color: Theme.panelBackground

    Rectangle {
        width: 3
        height: parent.height
        color: Theme.destructive
    }

    RowLayout {
        id: row
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: Theme.spacingLg
        anchors.rightMargin: Theme.spacingLg
        spacing: Theme.spacingLg

        IconGlyph {
            glyph: Theme.icons.warning
            iconSize: Theme.iconSizeMd
            iconColor: Theme.destructive
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("Project folder — %1").arg(root.folder.errorText)
            color: Theme.panelForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
            wrapMode: Text.Wrap
            maximumLineCount: 3
            elide: Text.ElideRight
        }

        Text {
            text: qsTr("Dismiss")
            color: Theme.accentOnPanel
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.folder.dismissError()
            }
        }
    }
}
