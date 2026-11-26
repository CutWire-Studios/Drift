import QtQuick
import QtQuick.Controls.Basic
import Drift

// One generator or particle preset in the Generators tab. Click adds it at the playhead.
Column {
    id: root
    required property var modelData
    required property real cardSize
    signal added()

    readonly property var glyphs: ({
        "background": Theme.icons.layers,
        "pattern": Theme.icons.shapes,
        "leader": Theme.icons.film,
        "particles": Theme.icons.sparkles
    })

    width: cardSize
    spacing: Theme.spacingSm
    scale: tap.pressed ? 0.97 : 1.0
    Behavior on scale {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }

    function add() {
        const reply = EditorState.addGeneratorClip(root.modelData.id, -1, -1, ({}))
        if (reply.ok)
            root.added()
        else
            Toasts.error(reply.error || qsTr("Couldn’t add the generator."))
    }

    Rectangle {
        width: cardSize
        height: cardSize
        radius: Theme.c.shapePresetCard.radius
        color: tap.containsMouse ? Theme.c.shapePresetCard.backgroundHover : Theme.c.shapePresetCard.background
        border.width: Theme.c.shapePresetCard.borderWidth
        border.color: tap.containsMouse ? Theme.c.shapePresetCard.borderHover : "transparent"

        Behavior on color {
            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }

        IconGlyph {
            anchors.centerIn: parent
            glyph: root.glyphs[root.modelData.category] || Theme.icons.sparkles
            iconSize: Math.round(root.cardSize * 0.34)
            iconColor: Theme.c.shapePresetCard.foregroundMuted
        }

        MouseArea {
            id: tap
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.add()
        }

        ThemedToolTip {
            text: qsTr("%1 — click to add at the playhead").arg(root.modelData.label)
            visible: tap.containsMouse
        }

        AssetQuickAddButton {
            z: 5
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 4
            revealed: tap.containsMouse
            tooltip: qsTr("Add at playhead")
            onClicked: root.add()
        }
    }

    Text {
        width: parent.width
        text: root.modelData.label
        color: Theme.c.shapePresetCard.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeCard
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
    }
}
