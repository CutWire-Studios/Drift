import QtQuick
import Drift

// A labelled slider for the phone tool pages: label left and value right on one line, the
// slider full width below. Properties bound through `value` follow the model while the finger
// is up. PropertyKeyframeRow draws the same arrangement for keyframed clip properties.
// On a phone the whole row is one line: label, slider, value, reset.
Item {
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
    readonly property string shownValue: valueText.length > 0 ? valueText
                                         : valueFormatter(slider.pressed ? slider.value : value)
    readonly property real labelWidth: Theme.phoneLabelWidth + Theme.spacingMd

    signal moved(real value)
    signal dragBegan()
    signal dragEnded()
    signal resetRequested()

    width: parent ? parent.width : implicitWidth
    implicitHeight: Theme.phone ? Theme.androidControlHeight
                                : header.height + slider.height
    height: implicitHeight
    opacity: enabled ? 1 : 0.5

    Item {
        id: header
        visible: !Theme.phone
        width: parent.width
        height: visible ? Math.max(labelText.implicitHeight, row.resettable ? Theme.androidIconButtonVisual : 0) : 0
    }

    Text {
        id: labelText
        x: 0
        y: Theme.phone ? (row.height - height) / 2 : (header.height - height) / 2
        width: Theme.phone ? row.labelWidth - Theme.spacingMd : implicitWidth
        elide: Text.ElideRight
        text: row.label
        color: Theme.c.androidSliderRow.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeSm
    }

    Text {
        id: valueLabel
        anchors.right: resetButton.visible ? resetButton.left : parent.right
        y: Theme.phone ? (row.height - height) / 2 : (header.height - height) / 2
        width: Theme.phone ? Math.max(48, implicitWidth) : implicitWidth
        horizontalAlignment: Text.AlignRight
        text: row.shownValue
        color: Theme.c.androidSliderRow.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeSm
    }

    IconButton {
        id: resetButton
        visible: row.resettable
        anchors.right: parent.right
        y: Theme.phone ? (row.height - height) / 2 : (header.height - height) / 2
        buttonSize: Theme.androidIconButtonVisual
        iconSize: Theme.iconSizeBase
        glyph: Theme.icons.reset
        enabled: row.resetEnabled
        tooltip: row.resetTooltip
        onClicked: row.resetRequested()
    }

    ThemedSlider {
        id: slider
        x: Theme.phone ? row.labelWidth : 0
        y: Theme.phone ? (row.height - height) / 2 : header.height
        width: Theme.phone ? row.width - row.labelWidth - valueLabel.width - (resetButton.visible ? resetButton.width : 0)
                             - Theme.spacingSm
                           : row.width
        height: Theme.androidControlHeight
        containmentMask: sliderHit
        Item {
            id: sliderHit
            anchors.fill: parent
            anchors.topMargin: -(Theme.androidMinTouchTarget - slider.height) / 2
            anchors.bottomMargin: anchors.topMargin
        }
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
