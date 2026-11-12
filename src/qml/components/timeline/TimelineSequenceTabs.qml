import QtQuick
import Drift

// Tab strip switching between the main timeline and the opened composite clips.
ListView {
    id: strip

    property Item leftGroup
    property Item rightGroup

    // Sits between the two toolbar button groups. Prefer dead centre, but slide aside
    // to stay clear of them and shrink (scrolling) when the gap is tight.
    // leftGroup is stretched up to rightGroup so it can clip, so its buttons
    // end at implicitWidth, not width.
    // Worked out left-to-right and flipped at the end: an explicit x is not
    // mirrored along with the anchors that swap the two groups under RTL.
    readonly property real gapStart: Theme.spacingLg
                                     + Math.min(leftGroup.implicitWidth, leftGroup.width) + 12
    readonly property real gapEnd: parent.width - Theme.spacingLg - rightGroup.width - 12
    readonly property real ltrX: Math.max(gapStart, Math.min((parent.width - width) / 2,
                                                              gapEnd - width))

    x: Qt.application.layoutDirection === Qt.RightToLeft ? parent.width - ltrX - width : ltrX
    anchors.verticalCenter: parent.verticalCenter
    visible: gapEnd - gapStart >= 96
    width: Math.min(contentWidth, Math.max(0, gapEnd - gapStart))
    height: Math.min(Theme.controlHeightSm, parent.height - Theme.spacingXs * 2)
    orientation: ListView.Horizontal
    layoutDirection: Qt.application.layoutDirection
    spacing: Theme.spacingXs
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    interactive: contentWidth > width

    model: [{ id: "", name: qsTr("Main") }].concat(EditorState.sequenceTabs)

    onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)

    Connections {
        target: EditorState
        function onSequenceTabsChanged() {
            const tabs = strip.model
            for (var i = 0; i < tabs.length; i++) {
                if (tabs[i].id === EditorState.activeSequenceId) {
                    strip.currentIndex = i
                    return
                }
            }
        }
    }

    footer: Item {
        width: addButton.width + Theme.spacingXs
        height: strip.height

        Rectangle {
            id: addButton
            x: Theme.spacingXs
            width: Theme.touchInput ? 28 : 22
            height: strip.height
            radius: Theme.c.timelineSequenceTabs.radius
            color: addMouse.containsMouse ? Theme.c.timelineSequenceTabs.backgroundHover : "transparent"

            IconGlyph {
                anchors.centerIn: parent
                glyph: Theme.icons.plus
                iconSize: 14
                iconColor: Theme.c.timelineSequenceTabs.icon
            }
            MouseArea {
                id: addMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: EditorState.newTimeline()
            }
            ThemedToolTip {
                visible: addMouse.containsMouse
                text: qsTr("New timeline")
            }
        }
    }

    delegate: Rectangle {
        id: tab

        required property var modelData
        readonly property bool isMain: modelData.id === ""
        readonly property bool active: EditorState.activeSequenceId === modelData.id

        width: Math.min(tabRow.implicitWidth + Theme.spacingMd * 2, 200)
        height: strip.height
        radius: Theme.c.timelineSequenceTabs.radius
        color: active ? Theme.c.timelineSequenceTabs.backgroundActive
                      : (tabMouse.containsMouse ? Theme.c.timelineSequenceTabs.backgroundHover : "transparent")

        Row {
            id: tabRow
            anchors.verticalCenter: parent.verticalCenter
            x: Theme.spacingMd
            spacing: Theme.spacingXs

            IconGlyph {
                glyph: tab.isMain ? Theme.icons.film : Theme.icons.layers
                iconSize: 14
                iconColor: Theme.c.timelineSequenceTabs.icon
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: tab.modelData.name
                color: tab.active ? Theme.c.timelineSequenceTabs.foregroundActive
                                  : Theme.c.timelineSequenceTabs.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                elide: Text.ElideRight
                width: Math.min(implicitWidth, tab.isMain ? 120 : 120 - closeButton.width)
                anchors.verticalCenter: parent.verticalCenter
            }
            Item {
                id: closeButton
                visible: !tab.isMain
                width: tab.isMain ? 0 : (Theme.touchInput ? 28 : 18)
                height: tab.height
                anchors.verticalCenter: parent.verticalCenter

                IconGlyph {
                    anchors.centerIn: parent
                    glyph: Theme.icons.x
                    iconSize: 12
                    iconColor: Theme.c.timelineSequenceTabs.icon
                    opacity: closeMouse.containsMouse ? 1 : 0.7
                }
                MouseArea {
                    id: closeMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: EditorState.closeSequenceTab(tab.modelData.id)
                }
            }
        }

        MouseArea {
            id: tabMouse
            anchors.fill: parent
            z: -1
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: EditorState.openSequence(tab.modelData.id)
        }

        ThemedToolTip {
            visible: tabMouse.containsMouse && !closeMouse.containsMouse
            text: tab.modelData.name
        }
    }
}
