import QtQuick
import Drift

// Small A or B tag in the corner of a compare pane.
Rectangle {
    id: tag

    property string text: ""

    x: Theme.spacingMd
    y: Theme.spacingMd
    z: 4
    width: label.implicitWidth + Theme.spacingLg
    height: label.implicitHeight + Theme.spacingSm
    radius: Theme.radiusPill
    color: Theme.c.sourceLabCompare.labelBackground

    Text {
        id: label
        anchors.centerIn: parent
        text: tag.text
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
        font.bold: true
        color: Theme.c.sourceLabCompare.labelForeground
    }
}
