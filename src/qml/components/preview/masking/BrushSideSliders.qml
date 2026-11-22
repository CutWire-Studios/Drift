import QtQuick
import Drift

// Size and opacity as two upright sliders on the right edge, where a thumb holding the phone can
// reach them. ThemedSlider only lies flat, so each is turned a quarter; it stays the same control.
Rectangle {
    id: panel

    readonly property real trackLength: Math.max(96, Math.min(parent ? parent.height * 0.5 : 160, 220))
    readonly property real column: Theme.androidMinTouchTarget

    width: column * 2 + Theme.spacingSm * 3
    height: trackLength + labelRow.height + valueRow.height + Theme.spacingSm * 4
    radius: Theme.c.manipContextBar.radius
    color: Theme.c.manipContextBar.background
    border.width: Theme.c.manipContextBar.borderWidth
    border.color: Theme.c.manipContextBar.border

    MouseArea {
        anchors.fill: parent
    }

    Row {
        id: labelRow
        x: Theme.spacingSm
        y: Theme.spacingSm
        spacing: Theme.spacingSm

        Repeater {
            model: [qsTr("Size"), qsTr("Opacity")]

            Text {
                required property string modelData
                width: panel.column
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                text: modelData
                color: Theme.c.manipContextBar.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
        }
    }

    Item {
        id: sizeColumn
        x: Theme.spacingSm
        y: labelRow.y + labelRow.height + Theme.spacingSm
        width: panel.column
        height: panel.trackLength

        ThemedSlider {
            id: sizeSlider
            x: 0
            y: sizeColumn.height
            width: sizeColumn.height
            height: sizeColumn.width
            rotation: -90
            transformOrigin: Item.TopLeft
            label: qsTr("Brush size")
            from: 0.2
            to: 50
            showValueTooltip: false
            Binding on value {
                when: !sizeSlider.pressed
                value: EditorState.masking.brushSize * 100
            }
            onMoved: EditorState.masking.brushSize = value / 100
        }
    }

    Item {
        id: opacityColumn
        x: sizeColumn.x + panel.column + Theme.spacingSm
        y: sizeColumn.y
        width: panel.column
        height: panel.trackLength

        ThemedSlider {
            id: opacitySlider
            x: 0
            y: opacityColumn.height
            width: opacityColumn.height
            height: opacityColumn.width
            rotation: -90
            transformOrigin: Item.TopLeft
            label: qsTr("Brush opacity")
            from: 5
            to: 100
            showValueTooltip: false
            Binding on value {
                when: !opacitySlider.pressed
                value: EditorState.masking.brushOpacity * 100
            }
            onMoved: EditorState.masking.brushOpacity = value / 100
        }
    }

    Row {
        id: valueRow
        x: Theme.spacingSm
        y: sizeColumn.y + sizeColumn.height + Theme.spacingSm
        spacing: Theme.spacingSm

        Text {
            width: panel.column
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("%1 %").arg((EditorState.masking.brushSize * 100).toFixed(1))
            color: Theme.c.manipContextBar.foreground
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        Text {
            width: panel.column
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("%1 %").arg(Math.round(EditorState.masking.brushOpacity * 100))
            color: Theme.c.manipContextBar.foreground
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }
}
