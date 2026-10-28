import QtQuick
import Drift

Rectangle {
    id: root

    property alias text: label.text

    z: 10
    width: label.implicitWidth + Theme.spacingMd * 2
    height: label.implicitHeight + Theme.spacingSm * 2
    radius: Theme.c.keyframesReadout.radius
    color: Theme.c.keyframesReadout.background
    border.width: Theme.c.keyframesReadout.borderWidth
    border.color: Theme.c.keyframesReadout.border

    Text {
        id: label
        anchors.centerIn: parent
        color: Theme.c.keyframesReadout.foreground
        font.family: Theme.monoFontFamily
        font.pixelSize: Theme.fontSizeXs
    }
}
