pragma ComponentBehavior: Bound
import QtQuick
import Drift

Item {
    id: root

    // Channel -> stored curve JSON; the plot parses it and falls back to the identity curve.
    property var curves: ({})
    property string channel: hueMode ? "hue" : "master"
    property string histogram: ""
    property bool showTabs: true
    // Hue curves: x is hue around the wheel, the ends share one y so the curve wraps, and the
    // channels are hue/sat/lum instead of master/R/G/B.
    property bool hueMode: false

    signal began()
    signal changed(string channel, string json)
    signal ended()

    readonly property var channels: hueMode ? ["hue", "sat", "lum"] : ["master", "r", "g", "b"]
    readonly property var channelNames: hueMode
        ? ({ hue: qsTr("Hue"), sat: qsTr("Saturation"), lum: qsTr("Luminance") })
        : ({ master: qsTr("Master"), r: "R", g: "G", b: "B" })
    readonly property var channelTips: ({
        hue: qsTr("Hue vs Hue: shift one color toward another"),
        sat: qsTr("Hue vs Saturation: make one color more or less vivid"),
        lum: qsTr("Hue vs Luminance: brighten or darken one color")
    })
    readonly property var channelColors: hueMode
        ? ({ hue: Theme.c.colorGradeCurveChannels.hue,
             sat: Theme.c.colorGradeCurveChannels.sat,
             lum: Theme.c.colorGradeCurveChannels.lum })
        : ({ master: Theme.c.colorGradeCurveChannels.master,
             r: Theme.c.colorGradeCurveChannels.r,
             g: Theme.c.colorGradeCurveChannels.g,
             b: Theme.c.colorGradeCurveChannels.b })
    readonly property real handleRadius: Theme.touchInput ? 8 : 5
    implicitWidth: 240
    readonly property bool hasSelection: plot.selected >= 0
    implicitHeight: hint.y + hint.height

    Item {
        id: tabs
        width: parent.width
        height: root.showTabs ? Math.max(chips.height, channelReset.height) : 0
        visible: root.showTabs

        Flow {
            id: chips
            width: parent.width - channelReset.width - Theme.spacingSm
            spacing: Theme.spacingSm
            Repeater {
                model: root.channels
                delegate: ThemedChip {
                    id: tab
                    required property string modelData
                    // The maps can still be unset when hueMode flips during creation and the
                    // Repeater builds tabs first; the bindings settle once they are.
                    text: root.channelNames?.[modelData] ?? ""
                    tooltip: root.hueMode ? (root.channelTips?.[modelData] ?? "") : ""
                    selected: root.channel === modelData
                    onClicked: root.channel = modelData
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 2
                        radius: 1
                        color: root.channelColors?.[tab.modelData] ?? "transparent"
                        opacity: tab.selected ? 1 : 0.4
                    }
                }
            }
        }

        IconButton {
            id: channelReset
            anchors.right: parent.right
            anchors.top: parent.top
            glyph: Theme.icons.reset
            tooltip: qsTr("Reset channel")
            buttonSize: Theme.controlHeightSm
            onClicked: plot.resetChannel()
        }
    }

    Rectangle {
        id: frame
        y: tabs.height + (root.showTabs ? Theme.spacingMd : 0)
        width: parent.width
        height: width
        radius: Theme.c.fadeCurvePlotFrame.radius
        color: Theme.c.fadeCurvePlotFrame.background
        border.width: Theme.c.fadeCurvePlotFrame.borderWidth
        border.color: Theme.c.fadeCurvePlotFrame.border

        Image {
            anchors.fill: parent
            anchors.margins: parent.border.width
            source: root.histogram
            visible: root.histogram.length > 0
            opacity: 0.25
            fillMode: Image.Stretch
            asynchronous: true
        }

        CurvePlot {
            id: plot
            anchors.fill: parent
            curves: root.curves
            channel: root.channel
            hueMode: root.hueMode
            channelColors: root.channelColors
            gridColor: Theme.c.fadeCurveCanvas.grid
            gridOpacity: Theme.c.fadeCurveCanvas.gridOpacity
            gridLineWidth: Theme.c.fadeCurveCanvas.gridLineWidth
            curveLineWidth: Theme.c.fadeCurveCanvas.curveLineWidth
            dimLineWidth: Theme.c.fadeCurveCanvas.handleLineWidth
            handleRadius: root.handleRadius
            pointBackground: Theme.c.fadeCurveWindow.pointBackground
            knobBorderWidth: Theme.c.fadeCurveWindow.knobBorderWidth
            onBegan: root.began()
            onChanged: (channel, json) => root.changed(channel, json)
            onEnded: root.ended()
        }
    }

    Text {
        id: readout
        y: frame.y + frame.height + Theme.spacingXs
        width: parent.width
        height: Math.ceil(fontMetrics.height)
        opacity: root.hasSelection ? 1 : 0
        text: !root.hasSelection ? ""
              : root.hueMode
                ? qsTr("Hue %1° → %2").arg(Math.round(plot.selectedPoint.x * 360))
                      .arg(((plot.selectedPoint.y - 0.5) * 2).toFixed(2))
                : qsTr("In %1 → Out %2").arg(Math.round(plot.selectedPoint.x * 255))
                      .arg(Math.round(plot.selectedPoint.y * 255))
        color: Theme.mutedForeground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    Text {
        id: hint
        y: readout.y + readout.height + Theme.spacingXs
        width: parent.width
        wrapMode: Text.WordWrap
        opacity: plot.pointCount <= 2 ? 1 : 0
        text: qsTr("Click to add a point. Drag to move it. Double-click or drag it off to remove.")
        color: Theme.mutedForeground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    FontMetrics {
        id: fontMetrics
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }
}
