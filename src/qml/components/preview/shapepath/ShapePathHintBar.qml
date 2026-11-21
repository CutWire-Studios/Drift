import QtQuick
import Drift

// What the path tool is doing and which keys it answers to, plus the few actions that have no
// gesture: continue from an end, close or open the contour, and Done.
Rectangle {
    id: bar

    required property string title
    required property string hint
    required property real maxHintWidth
    property string note: ""
    property bool canExtend: false
    property bool extending: false
    property bool canToggleClosed: false
    property bool closed: false
    signal extendToggled()
    signal closedToggled()
    signal done()

    anchors.horizontalCenter: parent.horizontalCenter
    anchors.bottom: parent.bottom
    anchors.bottomMargin: Theme.spacingLg
    width: barRow.implicitWidth + Theme.spacingLg * 2
    height: barRow.implicitHeight + Theme.spacingMd * 2
    radius: Theme.c.shapePathHintBar.radius
    color: Theme.c.shapePathHintBar.background
    border.width: Theme.c.shapePathHintBar.borderWidth
    border.color: Theme.c.shapePathHintBar.border
    z: 10

    Row {
        id: barRow
        anchors.centerIn: parent
        spacing: Theme.spacingLg

        Column {
            anchors.verticalCenter: parent.verticalCenter

            Text {
                text: bar.title
                color: Theme.c.shapePathHintBar.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                font.weight: Font.Medium
            }
            Text {
                width: Math.min(implicitWidth, bar.maxHintWidth)
                wrapMode: Text.WordWrap
                text: bar.hint
                color: Theme.c.shapePathHintBar.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
            Text {
                visible: bar.note !== ""
                width: Math.min(implicitWidth, bar.maxHintWidth)
                wrapMode: Text.WordWrap
                text: bar.note
                color: Theme.c.shapePathHintBar.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
        }

        ThemedButton {
            anchors.verticalCenter: parent.verticalCenter
            visible: bar.canExtend
            variant: bar.extending ? "primary" : "secondary"
            text: qsTr("Add points")
            tooltip: qsTr("Keep drawing from the selected end of the path")
            onClicked: bar.extendToggled()
        }
        ThemedButton {
            anchors.verticalCenter: parent.verticalCenter
            visible: bar.canToggleClosed
            variant: "secondary"
            text: bar.closed ? qsTr("Open path") : qsTr("Close path")
            tooltip: bar.closed ? qsTr("Remove the segment joining the last point to the first")
                                : qsTr("Join the last point back to the first")
            onClicked: bar.closedToggled()
        }
        ThemedButton {
            anchors.verticalCenter: parent.verticalCenter
            variant: "primary"
            text: qsTr("Done")
            onClicked: bar.done()
        }
    }
}
