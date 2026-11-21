import QtQuick
import Drift

// Empty inspector: icon, title, the sheet-vs-desktop hint and, on desktop, chips naming what a
// selected clip can do. A chip emits chipRequested(id) and the panel decides where it leads.
Column {
    id: root

    property bool sheetMode: false
    property real panelWidth: 260
    // Replaces the click-a-clip hint; set by the standalone single-tab panes.
    property string hint: ""

    signal chipRequested(string id)

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
              : qsTr("Select a clip to…")
        color: Theme.c.propertiesEmptyState.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    Flow {
        visible: root.hint === "" && !root.sheetMode
        width: parent.width
        spacing: Theme.spacingSm
        layoutDirection: Qt.LeftToRight

        Repeater {
            model: [
                { id: "captions", label: qsTr("Captions"), tip: qsTr("Open the subtitle cues to generate or edit captions") },
                { id: "speed", label: qsTr("Speed"), tip: qsTr("Select the clip at the playhead and open its Speed tab") },
                { id: "look", label: qsTr("Look"), tip: qsTr("Select the clip at the playhead and open its Effects tab") },
                { id: "text", label: qsTr("Text"), tip: qsTr("Select the clip at the playhead and open its Text tab") },
                { id: "background", label: qsTr("Background"), tip: qsTr("Open Masking to remove or replace the background") }
            ]

            ThemedChip {
                required property var modelData
                text: modelData.label
                variant: "outline"
                tooltip: modelData.tip
                onClicked: root.chipRequested(modelData.id)
            }
        }
    }
}
