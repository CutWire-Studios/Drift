import QtQuick
import Drift
import ".."

// One tab per open composite, after the main timeline's. Only shown while a composite is open,
// so a project without any keeps the timeline's full height.
Item {
    id: root

    readonly property var tabs: [{ id: "", name: qsTr("Main") }].concat(EditorState.sequenceTabs)

    visible: EditorState.sequenceTabs.length > 0
    height: visible ? 30 : 0

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.panelBorder
    }

    Row {
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacingLg
        anchors.bottom: parent.bottom
        height: parent.height
        spacing: Theme.spacingSm

        Repeater {
            model: root.tabs

            delegate: Rectangle {
                id: tab
                required property var modelData
                readonly property bool active: modelData.id === EditorState.activeSequenceId

                height: parent.height - Theme.spacingSm
                anchors.bottom: parent.bottom
                width: tabRow.implicitWidth + Theme.spacingLg * 2
                radius: Theme.radiusSm
                color: active ? Theme.panelAccent
                              : (tabMouse.containsMouse ? Theme.popoverHover : "transparent")

                MouseArea {
                    id: tabMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                    onClicked: (mouse) => {
                        if (mouse.button === Qt.MiddleButton)
                            EditorState.closeSequenceTab(tab.modelData.id)
                        else
                            EditorState.openSequence(tab.modelData.id)
                    }
                }

                Row {
                    id: tabRow
                    anchors.centerIn: parent
                    spacing: Theme.spacingMd

                    IconGlyph {
                        anchors.verticalCenter: parent.verticalCenter
                        glyph: tab.modelData.id === "" ? Theme.icons.film : Theme.icons.layers
                        iconSize: Theme.iconSizeSm
                        iconColor: tab.active ? Theme.panelForeground : Theme.mutedForeground
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: tab.modelData.name
                        color: tab.active ? Theme.panelForeground : Theme.mutedForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSm
                        font.weight: tab.active ? Font.Medium : Font.Normal
                    }

                    IconButton {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: tab.modelData.id !== ""
                        glyph: Theme.icons.x
                        buttonSize: 18
                        iconSize: Theme.iconSizeSm
                        tooltip: qsTr("Close tab")
                        onClicked: EditorState.closeSequenceTab(tab.modelData.id)
                    }
                }
            }
        }
    }
}
