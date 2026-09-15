import QtQuick
import QtQuick.Controls.Basic
import Drift

// Shown at startup in place of a fresh empty project whenever "Reopen last project
// on startup" is off: what you were working on, and how to start something. The
// mobile equivalent is AndroidProjectsPage; this mirrors its structure (new project,
// open project, recent projects) with desktop-sized controls and ProjectRow's
// styling (RecentProjectsPopup) for the recents list.
Rectangle {
    id: root

    signal newProjectRequested()
    signal openProjectRequested()
    signal openRecentRequested(string path)

    color: Theme.appBackground

    readonly property var items: EditorState.recentProjects

    component ProjectRow: Rectangle {
        id: projectRow

        required property var modelData
        readonly property bool exists: modelData.exists !== false
        readonly property bool hovered: rowHover.hovered

        width: ListView.view ? ListView.view.width : 0
        height: 56
        radius: Theme.radiusMd
        color: hovered && exists ? Theme.accent : Theme.panelBackground
        border.width: Theme.borderWidth
        border.color: Theme.panelBorder
        opacity: exists ? 1 : 0.6

        Behavior on color {
            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }

        HoverHandler {
            id: rowHover
        }

        Row {
            anchors.left: parent.left
            anchors.right: removeButton.left
            anchors.leftMargin: Theme.spacingXl
            anchors.rightMargin: Theme.spacingLg
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingLg

            Rectangle {
                width: 8
                height: 8
                radius: 4
                anchors.verticalCenter: parent.verticalCenter
                color: projectRow.exists ? Theme.constructive : Theme.mutedForeground
            }

            Column {
                width: parent.width - 8 - Theme.spacingLg
                anchors.verticalCenter: parent.verticalCenter
                spacing: 1

                Text {
                    width: parent.width
                    text: projectRow.modelData.name + (projectRow.exists ? "" : qsTr(" (missing)"))
                    color: Theme.panelForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                }

                Text {
                    width: parent.width
                    text: projectRow.modelData.path
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    elide: Text.ElideLeft
                }
            }
        }

        ThemedToolTip {
            text: projectRow.exists
                  ? projectRow.modelData.path
                  : qsTr("This file has been moved or deleted:\n%1").arg(projectRow.modelData.path)
            visible: projectRow.hovered && !removeArea.containsMouse
        }

        Item {
            id: removeButton
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingSm
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.iconSizeMd + Theme.spacingSm
            height: Theme.iconSizeMd + Theme.spacingSm
            opacity: projectRow.hovered ? 1 : 0

            Behavior on opacity {
                NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
            }

            IconGlyph {
                anchors.centerIn: parent
                glyph: Theme.icons.x
                iconSize: Theme.iconSizeMd
                iconColor: removeArea.containsMouse ? Theme.foreground : Theme.mutedForeground
            }

            ThemedToolTip {
                text: qsTr("Remove from recents")
                visible: removeArea.containsMouse
            }

            MouseArea {
                id: removeArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: (mouse) => {
                    mouse.accepted = true
                    EditorState.removeRecentProject(projectRow.modelData.path)
                }
            }
        }

        MouseArea {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.right: removeButton.left
            hoverEnabled: true
            cursorShape: projectRow.exists ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: {
                if (!projectRow.exists)
                    return
                root.openRecentRequested(projectRow.modelData.path)
            }
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: Math.max(height, column.implicitHeight + Theme.spacing3xl * 2)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick

        Column {
            id: column
            width: Math.min(560, flick.width - Theme.spacing3xl * 2)
            anchors.horizontalCenter: parent.horizontalCenter
            y: Math.max(Theme.spacing3xl, (flick.height - implicitHeight) / 2)
            spacing: Theme.spacing3xl

            Column {
                width: parent.width
                spacing: Theme.spacingXs

                Text {
                    text: "Drift"
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: 26
                    font.weight: Font.Bold
                }

                Text {
                    text: qsTr("Create polished videos fast")
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeBase
                }
            }

            Row {
                width: parent.width
                spacing: Theme.spacingLg

                ThemedButton {
                    variant: "primary"
                    glyph: Theme.icons.plus
                    text: qsTr("New Project")
                    onClicked: root.newProjectRequested()
                }

                ThemedButton {
                    variant: "secondary"
                    glyph: Theme.icons.folder
                    text: qsTr("Open Project…")
                    onClicked: root.openProjectRequested()
                }
            }

            Column {
                width: parent.width
                spacing: Theme.spacingMd

                Text {
                    text: qsTr("Recent Projects")
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    font.weight: Font.Medium
                }

                ThemedLabel {
                    width: parent.width
                    visible: root.items.length === 0
                    wrapMode: Text.WordWrap
                    text: qsTr("Nothing here yet — projects you save will show up in this list.")
                }

                ListView {
                    id: recentList
                    width: parent.width
                    // Non-interactive: the outer Flickable scrolls the whole page, so this
                    // grows to its full content height rather than clipping and needing a
                    // second scroll gesture.
                    height: contentHeight
                    spacing: Theme.spacingSm
                    visible: root.items.length > 0
                    interactive: false
                    model: root.items
                    delegate: ProjectRow { }
                }
            }
        }
    }
}
