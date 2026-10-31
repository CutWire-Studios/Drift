import QtQuick
import Drift

// A short bottom-centre hint chip, by default for when there is nothing to manipulate.
Rectangle {
    id: hint

    required property bool hasFrame
    property string message: hasFrame ? (ShellBridge.maskingActionHandler !== null
                                         ? qsTr("Pick a mask tool in the Masking panel and draw on the preview")
                                         : qsTr("Drag a mask from the Masks tab onto a clip to edit it here"))
                                      : qsTr("Select a clip at the playhead to edit its masks")

    anchors.horizontalCenter: parent.horizontalCenter
    anchors.bottom: parent.bottom
    anchors.bottomMargin: Theme.spacingLg
    width: hintText.width + Theme.spacingLg * 2
    height: hintText.height + Theme.spacingMd * 2
    radius: Theme.c.maskOverlayEmptyHint.radius
    color: Theme.c.maskOverlayEmptyHint.background
    opacity: Theme.c.maskOverlayEmptyHint.opacity

    Text {
        id: hintText
        anchors.centerIn: parent
        text: hint.message
        color: Theme.c.maskOverlayEmptyHint.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }
}
