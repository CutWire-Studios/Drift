import QtQuick
import Drift

// First-run hint. The crop tool has three non-obvious inputs and
// no menu surface to discover them from, so they are spelled out
// while the tool is open. Fades once the user has driven all
// three, and stays gone for the rest of the session.
Rectangle {
    id: cropHint

    required property bool hintDismissed
    required property bool didResize
    required property bool didZoom
    required property bool didPan

    anchors.horizontalCenter: parent.horizontalCenter
    anchors.top: parent.top
    width: hintRow.width + Theme.spacing2xl
    height: hintRow.height + Theme.spacingLg
    radius: Theme.c.cropOverlayHint.radius
    // Sits on the preview scrim, not a panel surface, so it uses
    // the on-media tokens — panelForeground would go dark-on-dark
    // in light mode.
    color: Theme.c.cropOverlayHint.background
    border.width: Theme.c.cropOverlayHint.borderWidth
    border.color: Theme.c.cropOverlayHint.border

    opacity: cropHint.hintDismissed ? 0 : 1
    visible: opacity > 0

    Behavior on opacity {
        NumberAnimation { duration: Theme.durationSlow; easing.type: Theme.easing }
    }

    Row {
        id: hintRow
        anchors.centerIn: parent
        spacing: Theme.spacingLg

        Text {
            text: qsTr("Drag the edges to reframe")
            color: cropHint.didResize ? Theme.c.cropOverlayHint.foregroundMuted : Theme.c.cropOverlayHint.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
        Text {
            text: "·"
            color: Theme.c.cropOverlayHint.separator
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
        Text {
            text: Theme.platformShortcutText(qsTr("Ctrl + scroll to zoom"))
            color: cropHint.didZoom ? Theme.c.cropOverlayHint.foregroundMuted : Theme.c.cropOverlayHint.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
        Text {
            text: "·"
            color: Theme.c.cropOverlayHint.separator
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
        Text {
            text: qsTr("Middle-drag to pan")
            color: cropHint.didPan ? Theme.c.cropOverlayHint.foregroundMuted : Theme.c.cropOverlayHint.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }
}
