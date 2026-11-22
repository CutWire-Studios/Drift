import QtQuick
import Drift

// A labelled slider for the phone tool pages: label left and value right on one line, the
// slider full width below. Properties bound through `value` follow the model while the finger
// is up. PropertyKeyframeRow draws the same arrangement for keyframed clip properties.
Column {
    id: row

    property string label: ""
    property string valueText: ""
    property real value: 0
    property real from: 0
    property real to: 1
    property real stepSize: 0
    property bool lockWhilePlaying: false
    property var valueFormatter: function(v) { return Number(v).toFixed(2) }
    // A reset button after the value, enabled while `resetEnabled`.
    property bool resettable: false
    property bool resetEnabled: true
    property string resetTooltip: qsTr("Reset")
    readonly property alias pressed: slider.pressed
    readonly property real liveValue: slider.value

    signal moved(real value)
    signal dragBegan()
    signal dragEnded()
    signal resetRequested()

    width: parent ? parent.width : implicitWidth
    spacing: 0
    opacity: enabled ? 1 : 0.5

    Item {
        width: parent.width
        height: Math.max(labelText.implicitHeight, row.resettable ? Theme.androidMinTouchTarget : 0)

        Text {
            id: labelText
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: row.label
            color: Theme.c.androidSliderRow.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
        }

        Text {
            anchors.right: resetButton.visible ? resetButton.left : parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: row.valueText.length > 0 ? row.valueText
                                           : row.valueFormatter(slider.pressed ? slider.value : row.value)
            color: Theme.c.androidSliderRow.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
        }

        IconButton {
            id: resetButton
            visible: row.resettable
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            buttonSize: Theme.androidMinTouchTarget
            iconSize: Theme.iconSizeBase
            glyph: Theme.icons.reset
            enabled: row.resetEnabled
            tooltip: row.resetTooltip
            onClicked: row.resetRequested()
        }
    }

    ThemedSlider {
        id: slider
        width: parent.width
        height: Theme.androidMinTouchTarget
        from: row.from
        to: row.to
        stepSize: row.stepSize
        label: row.label
        lockWhilePlaying: row.lockWhilePlaying
        valueFormatter: row.valueFormatter
        Binding on value {
            when: !slider.pressed
            value: row.value
        }
        onPressedChanged: {
            if (pressed)
                row.dragBegan()
            else
                row.dragEnded()
        }
        onMoved: row.moved(value)
    }
}
