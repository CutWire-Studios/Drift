import QtQuick
import QtQuick.Controls.Basic
import Drift
import "components"

// The home screen's start destination: what you were working on, and how to start something.
//
// The layout picker used to sit here, above the recents and above the buttons, so the first
// thing a returning user saw was a form asking about aspect ratios. It is gone: "New project"
// opens the picker directly and the canvas is inferred from the first clip, which is what every
// phone editor does and what the desktop's own "Decide later" path already allowed.
Item {
    id: root

    signal newProjectRequested()
    signal quickEditRequested()
    signal openProjectRequested()
    signal openRecentRequested(string path)

    // Quick edit leads: it is the same first two taps as New project, and the editor it
    // lands in is the reduced one, so the shortest path is also the prominent one. New
    // project keeps its own tile because escalating out of quick mode costs a tap, and
    // starting a multi-clip edit should not have to pay it.
    readonly property var tiles: [
        { id: "quick", label: qsTr("Quick edit"), detail: qsTr("Pick a clip, start now"),
          icon: Theme.icons.sparkles, primary: true },
        { id: "new", label: qsTr("New project"), detail: qsTr("Choose a canvas, start empty"),
          icon: Theme.icons.plus, primary: false }
    ]

    function triggerTile(tileId) {
        if (tileId === "quick")
            root.quickEditRequested()
        else if (tileId === "new")
            root.newProjectRequested()
    }

    Flickable {
        id: flick
        anchors.fill: parent
        anchors.topMargin: root.SafeArea.margins.top
        anchors.leftMargin: root.SafeArea.margins.left
        anchors.rightMargin: root.SafeArea.margins.right
        contentWidth: width
        contentHeight: pageColumn.implicitHeight + Theme.spacing2xl
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick

        Column {
            id: pageColumn
            width: parent.width
            spacing: Theme.spacing2xl
            topPadding: Theme.spacing2xl
            leftPadding: Theme.androidPagePadding
            rightPadding: Theme.androidPagePadding

            readonly property real contentWidth: width - leftPadding - rightPadding

            Column {
                width: pageColumn.contentWidth
                spacing: 2

                Text {
                    text: "Drift"
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: 22
                    font.weight: Font.Bold
                }

                Text {
                    text: qsTr("Create polished videos fast")
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm
                }
            }

            // --- Start something ----------------------------------------------
            Column {
                width: pageColumn.contentWidth
                spacing: Theme.androidTouchGap

                Repeater {
                    model: root.tiles

                    delegate: AbstractButton {
                        id: tile
                        required property var modelData

                        width: pageColumn.contentWidth
                        height: Theme.androidHomeTileHeight
                        hoverEnabled: true

                        Accessible.role: Accessible.Button
                        Accessible.name: modelData.label
                        Accessible.description: modelData.detail

                        scale: tile.down ? Theme.pressScale : 1.0
                        Behavior on scale {
                            NumberAnimation { duration: Theme.durationPress; easing.type: Theme.easing }
                        }

                        onClicked: {
                            Haptics.select()
                            root.triggerTile(modelData.id)
                        }

                        background: Rectangle {
                            radius: Theme.radiusMd
                            color: tile.modelData.primary
                                   ? (tile.down ? Qt.darker(Theme.primary, 1.15) : Theme.primary)
                                   : (tile.down ? Theme.panelMuted : Theme.panelBackground)
                            border.width: tile.modelData.primary ? 0 : Theme.borderWidth
                            border.color: Theme.panelBorder
                        }

                        contentItem: Row {
                            spacing: Theme.spacingLg
                            leftPadding: Theme.spacingXl
                            rightPadding: Theme.spacingXl

                            IconGlyph {
                                anchors.verticalCenter: parent.verticalCenter
                                glyph: tile.modelData.icon
                                iconSize: Theme.iconSizeLg
                                iconColor: tile.modelData.primary
                                           ? Theme.primaryForeground : Theme.panelForeground
                            }

                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 2

                                Text {
                                    text: tile.modelData.label
                                    color: tile.modelData.primary
                                           ? Theme.primaryForeground : Theme.panelForeground
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeBase
                                    font.weight: Font.Medium
                                }

                                Text {
                                    text: tile.modelData.detail
                                    color: tile.modelData.primary
                                           ? Theme.primaryForeground : Theme.mutedForeground
                                    opacity: tile.modelData.primary ? 0.85 : 1
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeXs
                                }
                            }
                        }
                    }
                }
            }

            // --- Recent projects ----------------------------------------------
            //
            // A wrapping grid rather than the single horizontal strip this used to be. The strip
            // showed two and a half cards and hid the rest behind a sideways flick with nothing
            // saying so, which on the one screen whose whole job is "get back to your work" is
            // the wrong trade. Opening a project from disk is the rarer case, so it loses its
            // full-width tile and becomes the button beside this heading.
            Column {
                width: pageColumn.contentWidth
                spacing: Theme.spacingMd

                Item {
                    width: parent.width
                    height: Math.max(recentsLabel.implicitHeight, openButton.height)

                    ThemedLabel {
                        id: recentsLabel
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Recent projects")
                        tone: "default"
                        size: "sm"
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

                ThemedLabel {
                    width: parent.width
                    visible: EditorState.recentProjects.length === 0
                    wrapMode: Text.WordWrap
                    text: qsTr("Nothing here yet — projects you save will show up in this list.")
                }

                Grid {
                    id: recentGrid
                    width: parent.width
                    columns: 2
                    spacing: Theme.spacingMd
                    visible: EditorState.recentProjects.length > 0

                    readonly property real cellWidth:
                        (width - spacing * (columns - 1)) / columns

                    Repeater {
                        model: EditorState.recentProjects

                        delegate: Rectangle {
                            id: card
                            required property var modelData
                            width: recentGrid.cellWidth
                            height: Theme.androidHomeRecentCardHeight
                            radius: Theme.radiusMd
                            color: Theme.panelBackground
                            border.width: Theme.borderWidth
                            border.color: Theme.panelBorder
                            opacity: modelData.exists === false ? 0.55 : 1

                            Accessible.role: Accessible.Button
                            Accessible.name: card.projectLabel

                            readonly property string projectLabel: {
                                const n = card.modelData.name || ""
                                return n.replace(/\.drift$/i, "") || qsTr("Untitled")
                            }

                            Column {
                                anchors.fill: parent
                                anchors.margins: Theme.spacingLg
                                spacing: Theme.spacingSm

                                Rectangle {
                                    width: parent.width
                                    height: 32
                                    radius: Theme.radiusSm
                                    color: Theme.panelAccent

                                    IconGlyph {
                                        anchors.centerIn: parent
                                        glyph: Theme.icons.film
                                        iconSize: 18
                                        iconColor: Theme.mutedForeground
                                    }

                                    // Same on-disk signal the desktop recents list carries;
                                    // the card's dimming alone did not say what was wrong.
                                    Rectangle {
                                        anchors.right: parent.right
                                        anchors.top: parent.top
                                        anchors.margins: 4
                                        width: 8
                                        height: 8
                                        radius: 4
                                        color: card.modelData.exists === false
                                               ? Theme.mutedForeground : Theme.constructive
                                    }
                                }

                                Text {
                                    width: parent.width
                                    text: card.projectLabel
                                    color: Theme.panelForeground
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeXs
                                    font.weight: Font.Medium
                                    elide: Text.ElideMiddle
                                }

                                // Elided from the left: on a narrow card the tail — the folder
                                // and file name — is the half that tells two projects apart.
                                Text {
                                    width: parent.width
                                    text: card.modelData.path
                                    color: Theme.mutedForeground
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeXs
                                    elide: Text.ElideLeft
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                pressAndHoldInterval: 450
                                property bool heldMenu: false
                                onPressed: heldMenu = false
                                onPressAndHold: {
                                    heldMenu = true
                                    Haptics.pickUp()
                                    cardMenu.popup()
                                }
                                onClicked: {
                                    if (heldMenu)
                                        return
                                    if (card.modelData.exists === false) {
                                        Toasts.warning(qsTr("That project file is missing."))
                                        return
                                    }
                                    Haptics.select()
                                    root.openRecentRequested(card.modelData.path)
                                }
                            }

                            // Long-press was the only way to reach this, which is a gesture
                            // with no visible equivalent — the one rule the rest of the rail
                            // already follows. The button is the equivalent; the long-press
                            // stays as an accelerator.
                            IconButton {
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: Theme.spacingXs
                                buttonSize: Theme.iconButtonSize
                                iconSize: Theme.iconSizeMd
                                glyph: Theme.icons.ellipsis
                                variant: "text"
                                tooltip: qsTr("Project actions")
                                onClicked: cardMenu.popup()
                            }

                            ThemedContextMenu {
                                id: cardMenu

                                ThemedMenuItem {
                                    text: qsTr("Remove from recents")
                                    icon.name: Theme.icons.trash
                                    onTriggered: EditorState.removeRecentProject(card.modelData.path)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
