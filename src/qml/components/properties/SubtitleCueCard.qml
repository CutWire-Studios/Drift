import QtQuick
import Drift

Item {
    property var editor
    id: cueDelegate
    required property int index
    required property real start
    required property real end
    required property string text
    readonly property bool isActive: index === editor.activeCueIndex
    readonly property bool isSelected: index === editor.selectedCueIndex

    width: ListView.view ? ListView.view.width : 0
    height: card.height + (isSelected ? addRow.height : 0)

    Rectangle {
        id: card
        width: parent.width
        height: Math.max(lineCol.implicitHeight + 14, Theme.touchInput && Theme.compact ? Theme.androidRowHeight : 0)
        radius: Theme.c.subtitleCueCard.radius
        color: cueDelegate.isSelected ? Theme.c.subtitleCueCard.backgroundSelected : (rowHover.containsMouse ? Theme.c.subtitleCueCard.backgroundHover : "transparent")
        border.width: (cueDelegate.isSelected || cueDelegate.isActive || rowHover.containsMouse) ? Theme.c.subtitleCueCard.borderWidth : 0
        border.color: cueDelegate.isActive ? Theme.c.subtitleCueCard.borderActive : (cueDelegate.isSelected ? Theme.c.subtitleCueCard.borderSelected : (rowHover.containsMouse ? Theme.c.subtitleCueCard.borderHover : "transparent"))

        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }

        MouseArea {
            id: rowHover
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                editor.selectCue(cueDelegate.index)
                if (editor.compactEdit)
                    editor.cueEditRequested(cueDelegate.index)
            }
        }

        Column {
            id: lineCol
            x: 12
            anchors.verticalCenter: parent.verticalCenter
            // The bin's slot is reserved even while it is hidden, so the wrapped
            // caption text doesn't reflow as the pointer moves down the list.
            width: parent.width - 24 - deleteButton.width
            spacing: 2

            Text {
                text: editor.formatCueTime(cueDelegate.start) + "  →  "
                      + editor.formatCueTime(cueDelegate.end)
                color: Theme.c.subtitleCueCard.foregroundMuted
                font.family: Theme.touchInput && Theme.compact ? Theme.fontFamily : Theme.monoFontFamily
                font.pixelSize: Theme.touchInput && Theme.compact ? Theme.fontSizeSm : Theme.fontSizeXs
                opacity: (cueDelegate.isActive || cueDelegate.isSelected) ? 0.95 : (rowHover.containsMouse ? 0.85 : 0.65)
                Behavior on opacity { NumberAnimation { duration: 150 } }
            }

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: cueDelegate.text.length ? cueDelegate.text : qsTr("(empty)")
                font.family: Theme.fontFamily
                font.pixelSize: cueDelegate.isActive ? Theme.fontSizeBase : Theme.fontSizeSm
                font.weight: cueDelegate.isActive ? Font.DemiBold : Font.Normal
                font.italic: !cueDelegate.text.length
                color: cueDelegate.isActive ? Theme.c.subtitleCueCard.accentClip
                       : (cueDelegate.isSelected || rowHover.containsMouse ? Theme.c.subtitleCueCard.foreground : Theme.c.subtitleCueCard.foregroundBase)
                opacity: (cueDelegate.isActive || cueDelegate.isSelected) ? 1.0 : (rowHover.containsMouse ? 0.95 : 0.82)
                Behavior on opacity { NumberAnimation { duration: 200 } }
                Behavior on color { ColorAnimation { duration: 200 } }
            }
        }

        IconButton {
            id: deleteButton
            anchors.right: parent.right
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            glyph: Theme.icons.trash
            variant: "ghost"
            iconSize: Theme.iconSizeSm
            tooltip: qsTr("Delete this subtitle")
            opacity: (rowHover.containsMouse || cueDelegate.isSelected || visualFocus
                      || Theme.touchInput) ? 1 : 0
            enabled: opacity > 0
            onClicked: editor.removeCue(cueDelegate.index)

            Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
        }
    }

    // Insertion point for the cue that follows the one being edited.
    Item {
        id: addRow
        anchors.top: card.bottom
        width: parent.width
        height: Theme.touchInput && Theme.compact ? Theme.androidRowHeight : 30
        visible: cueDelegate.isSelected

        IconButton {
            anchors.centerIn: parent
            glyph: Theme.icons.plus
            variant: "ghost"
            iconSize: Theme.iconSizeSm
            tooltip: qsTr("Add a subtitle after this one")
            onClicked: editor.insertCueAfter(cueDelegate.index)
        }
    }
}
