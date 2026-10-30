import QtQuick
import QtQuick.Controls.Basic
import Drift

// Home's Projects tab: every recent project, searchable, newest first as the list stores them.
Item {
    id: root

    signal newProjectRequested()
    signal openProjectRequested()
    signal openRecentRequested(string path)

    readonly property var recents: EditorState.projectFile.recentProjects
    readonly property string query: searchField.text.trim().toLowerCase()
    readonly property var shown: root.query.length === 0
        ? root.recents
        : root.recents.filter((p) => (p.name || "").toLowerCase().indexOf(root.query) !== -1)

    Item {
        id: headerBlock
        x: Theme.androidPagePadding
        y: Theme.spacingLg
        width: parent.width - Theme.androidPagePadding * 2
        height: Math.max(searchField.height, openButton.height)
        visible: root.recents.length > 0

        ThemedTextField {
            id: searchField
            anchors.left: parent.left
            anchors.right: openButton.left
            anchors.rightMargin: Theme.spacingMd
            anchors.verticalCenter: parent.verticalCenter
            placeholderText: qsTr("Search projects")
        }

        ThemedButton {
            id: openButton
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            variant: "secondary"
            glyph: Theme.icons.folder
            text: qsTr("Open")
            tooltip: qsTr("Open a project from this device")
            onClicked: {
                Haptics.select()
                root.openProjectRequested()
            }
        }
    }

    ListView {
        anchors.top: headerBlock.visible ? headerBlock.bottom : parent.top
        anchors.topMargin: Theme.spacingLg
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.androidPagePadding
        anchors.rightMargin: Theme.androidPagePadding
        visible: root.shown.length > 0
        clip: true
        spacing: Theme.spacingMd
        boundsBehavior: Flickable.StopAtBounds
        model: root.shown

        delegate: AndroidHomeRecentCard {
            width: ListView.view.width
            row: true
            onOpenRequested: (path) => root.openRecentRequested(path)
        }
    }

    EmptyState {
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.spacing3xl, 300)
        visible: root.shown.length === 0
        glyph: Theme.icons.film
        title: root.recents.length === 0 ? qsTr("No projects yet") : qsTr("No matches")
        hint: root.recents.length === 0
              ? qsTr("Projects you save will show up here.")
              : qsTr("Try a different name.")
        actionText: root.recents.length === 0 ? qsTr("New project") : ""
        actionVariant: "primary"
        onActionTriggered: root.newProjectRequested()
    }
}
