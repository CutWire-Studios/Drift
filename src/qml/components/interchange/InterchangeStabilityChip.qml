import QtQuick
import Drift

Rectangle {
    id: root

    property string stability: "experimental"

    readonly property string label: root.stability === "stable" ? qsTr("Stable")
                                    : root.stability === "beta" ? qsTr("Beta") : qsTr("Experimental")

    implicitWidth: chipText.implicitWidth + Theme.spacingLg
    implicitHeight: chipText.implicitHeight + Theme.spacingXs * 2
    radius: Theme.c.interchange.chipRadius
    color: Theme.c.interchange.chipBackground

    Text {
        id: chipText
        anchors.centerIn: parent
        text: root.label
        color: Theme.c.interchange.chipForeground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }
}
