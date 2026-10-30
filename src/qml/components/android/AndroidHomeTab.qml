import QtQuick
import QtQuick.Controls.Basic
import Drift

// Home's first tab: the New project hero, a grid of quick actions, and the recent projects strip.
Item {
    id: root

    signal newProjectRequested()
    signal quickActionRequested(string actionId)
    signal openRecentRequested(string path)
    signal seeAllRequested()

    // Add an entry here and it appears in the grid; AndroidMain.runQuickAction(id) handles it.
    readonly property var quickActions: [
        { id: "quick", label: qsTr("Quick edit"), icon: Theme.icons.sparkles },
        { id: "filter", label: qsTr("Effects"), icon: Theme.icons.wand },
        { id: "adjust", label: qsTr("Adjust"), icon: Theme.icons.sliders },
        { id: "sticker", label: qsTr("Sticker"), icon: Theme.icons.smile },
        { id: "text", label: qsTr("Text"), icon: Theme.icons.type },
        { id: "crop", label: qsTr("Crop"), icon: Theme.icons.crop },
        { id: "trim", label: qsTr("Trim"), icon: Theme.icons.scissors },
        { id: "toAudio", label: qsTr("Video to audio"), icon: Theme.icons.music },
        { id: "toVideo", label: qsTr("Image to video"), icon: Theme.icons.image }
    ]

    readonly property var recents: EditorState.projectFile.recentProjects

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: pageColumn.implicitHeight + Theme.spacing2xl
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick

        Column {
            id: pageColumn
            width: parent.width
            spacing: Theme.spacing2xl
            topPadding: Theme.spacingLg

            readonly property real contentWidth: width - Theme.androidPagePadding * 2

            AbstractButton {
                id: hero
                x: Theme.androidPagePadding
                width: pageColumn.contentWidth
                height: Theme.androidHomeTileHeight
                scale: hero.down ? Theme.pressScale : 1.0
                Behavior on scale {
                    NumberAnimation { duration: Theme.durationPress; easing.type: Theme.easing }
                }

                Accessible.role: Accessible.Button
                Accessible.name: qsTr("New project")

                onClicked: {
                    Haptics.select()
                    root.newProjectRequested()
                }

                background: Rectangle {
                    radius: Theme.c.androidHomeStartTile.radius
                    color: hero.down ? Qt.darker(Theme.c.androidHomeStartTile.primaryBackground, 1.15)
                                     : Theme.c.androidHomeStartTile.primaryBackground
                }

                contentItem: Item {
                    Row {
                        anchors.centerIn: parent
                        spacing: Theme.spacingMd

                        IconGlyph {
                            anchors.verticalCenter: parent.verticalCenter
                            glyph: Theme.icons.plus
                            iconSize: Theme.iconSizeLg
                            iconColor: Theme.c.androidHomeStartTile.primaryForeground
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("New project")
                            color: Theme.c.androidHomeStartTile.primaryForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeBase
                            font.weight: Font.Medium
                        }
                    }
                }
            }

            Grid {
                id: actionGrid
                x: Theme.androidPagePadding
                width: pageColumn.contentWidth
                columns: 4
                rowSpacing: Theme.spacingLg
                columnSpacing: Theme.spacingMd

                readonly property real cellWidth: (width - columnSpacing * (columns - 1)) / columns

                Repeater {
                    model: root.quickActions

                    delegate: AbstractButton {
                        id: tile
                        required property var modelData

                        width: actionGrid.cellWidth
                        implicitHeight: tileColumn.implicitHeight
                        scale: tile.down ? Theme.pressScale : 1.0
                        Behavior on scale {
                            NumberAnimation { duration: Theme.durationPress; easing.type: Theme.easing }
                        }

                        Accessible.role: Accessible.Button
                        Accessible.name: tile.modelData.label

                        onClicked: {
                            Haptics.select()
                            root.quickActionRequested(tile.modelData.id)
                        }

                        contentItem: Column {
                            id: tileColumn
                            spacing: Theme.spacingSm

                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: Math.min(tile.width, 64)
                                height: width
                                radius: Theme.c.androidHomeStartTile.radius
                                color: tile.down ? Theme.c.androidHomeStartTile.backgroundPressed
                                                 : Theme.c.androidHomeStartTile.background
                                border.width: Theme.c.androidHomeStartTile.borderWidth
                                border.color: Theme.c.androidHomeStartTile.border

                                IconGlyph {
                                    anchors.centerIn: parent
                                    glyph: tile.modelData.icon
                                    iconSize: Theme.iconSizeLg
                                    iconColor: Theme.c.androidHomeStartTile.foreground
                                }
                            }

                            Text {
                                width: parent.width
                                text: tile.modelData.label
                                horizontalAlignment: Text.AlignHCenter
                                color: Theme.c.androidHomeStartTile.foreground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeXs
                                wrapMode: Text.WordWrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }

            Column {
                width: parent.width
                spacing: Theme.spacingMd

                Item {
                    x: Theme.androidPagePadding
                    width: pageColumn.contentWidth
                    height: Math.max(recentsLabel.implicitHeight, seeAll.height)

                    ThemedLabel {
                        id: recentsLabel
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Recent projects")
                        tone: "default"
                        size: "base"
                        font.weight: Font.Medium
                    }

                    ThemedButton {
                        id: seeAll
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        visible: root.recents.length > 0
                        variant: "ghost"
                        text: qsTr("See all")
                        onClicked: root.seeAllRequested()
                    }
                }

                ThemedLabel {
                    x: Theme.androidPagePadding
                    width: pageColumn.contentWidth
                    visible: root.recents.length === 0
                    text: qsTr("Nothing here yet — projects you save will show up in this list.")
                }

                ListView {
                    width: parent.width
                    height: visible ? contentItem.childrenRect.height : 0
                    visible: root.recents.length > 0
                    orientation: ListView.Horizontal
                    spacing: Theme.spacingMd
                    leftMargin: Theme.androidPagePadding
                    rightMargin: Theme.androidPagePadding
                    boundsBehavior: Flickable.StopAtBounds
                    model: root.recents.slice(0, 12)

                    delegate: AndroidHomeRecentCard {
                        width: 168
                        onOpenRequested: (path) => root.openRecentRequested(path)
                    }
                }
            }
        }
    }
}
