import QtQuick
import Drift

// Empty inspector: icon, title, and the sheet-vs-desktop hint.
Column {
    id: root

    property bool sheetMode: false
    property real panelWidth: 260
    // Replaces the click-a-clip hint; set by the standalone single-tab panes.
    property string hint: ""

    width: Math.max(1, Math.min(260, panelWidth - 32))
    spacing: 16

    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        width: 48
        height: 48
        radius: Theme.c.propertiesEmptyState.radius
        color: "transparent"
        border.width: Theme.c.propertiesEmptyState.borderWidth
        border.color: Theme.c.propertiesEmptyState.border

        IconGlyph {
            anchors.centerIn: parent
            glyph: Theme.icons.sliders
            iconSize: 22
            iconColor: Theme.c.propertiesEmptyState.icon
        }
    }

    Text {
        width: parent.width
        horizontalAlignment: Text.AlignHCenter
        text: qsTr("It's empty here")
        color: Theme.c.propertiesEmptyState.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeBase
        font.weight: Font.Medium
    }

    Text {
        width: parent.width
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        text: root.hint !== "" ? root.hint
              : root.sheetMode
              ? qsTr("Tap a clip on the timeline to edit its properties")
              : qsTr("Click a clip on the timeline to edit its properties")
        color: Theme.c.propertiesEmptyState.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }
}
