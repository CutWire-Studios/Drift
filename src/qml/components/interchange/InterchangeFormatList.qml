pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Drift

Rectangle {
    id: root

    property var model: []
    property string currentId: ""

    signal selected(string formatId)

    color: Theme.c.interchange.panelBackground
    border.width: Theme.c.interchange.borderWidth
    border.color: Theme.c.interchange.panelBorder
    radius: Theme.c.interchange.radius

    ListView {
        anchors.fill: parent
        anchors.margins: Theme.spacingSm
        clip: true
        spacing: Theme.spacingXs
        model: root.model
        ScrollBar.vertical: AppScrollBar { }

        delegate: AbstractButton {
            id: row

            required property var modelData

            width: ListView.view.width
            implicitHeight: info.implicitHeight + Theme.spacingLg * 2
            hoverEnabled: true
            focusPolicy: Qt.StrongFocus
            onClicked: root.selected(row.modelData.id)

            Accessible.role: Accessible.RadioButton
            Accessible.name: row.modelData.name

            background: Rectangle {
                radius: Theme.c.interchange.radius
                color: root.currentId === row.modelData.id ? Theme.c.interchange.itemSelected
                       : row.hovered ? Theme.c.interchange.itemHover : "transparent"
            }

            contentItem: Column {
                id: info
                spacing: Theme.spacingXs

                Row {
                    spacing: Theme.spacingMd

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.modelData.name
                        color: Theme.c.interchange.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeBase
                    }

                    InterchangeStabilityChip {
                        anchors.verticalCenter: parent.verticalCenter
                        stability: row.modelData.stability
                    }
                }

                Text {
                    text: row.modelData.extensions.map(e => "." + e).join("  ")
                    color: Theme.c.interchange.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
            }
        }
    }
}
