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
    signal cancelled()

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
    property bool barOpen: false
    property point typingAt: Qt.point(0, 0)
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

    // The region past the plot where releasing a dragged point deletes it, shown while one is dragged.
    Item {
        id: deleteZone
        visible: plot.deleteZoneVisible
        x: frame.x - frame.width * 0.25
        y: frame.y - frame.height * 0.25
        width: frame.width * 1.5
        height: frame.height * 1.5
        readonly property real opacityNow: plot.deleteZoneActive ? Theme.c.curveGraph.deleteZoneActiveOpacity
                                                                  : Theme.c.curveGraph.deleteZoneOpacity

        Repeater {
            model: [
                { x: 0, y: 0, w: 1, h: 0.1667 },
                { x: 0, y: 0.8333, w: 1, h: 0.1667 },
                { x: 0, y: 0.1667, w: 0.1667, h: 0.6667 },
                { x: 0.8333, y: 0.1667, w: 0.1667, h: 0.6667 }
            ]
            delegate: Rectangle {
                required property var modelData
                x: modelData.x * deleteZone.width
                y: modelData.y * deleteZone.height
                width: modelData.w * deleteZone.width
                height: modelData.h * deleteZone.height
                color: Theme.c.curveGraph.deleteZone
                opacity: deleteZone.opacityNow
            }
        }
    }

    Rectangle {
        id: frame
        y: tabs.height + (root.showTabs ? Theme.spacingMd : 0)
        width: parent.width
        height: width
        radius: Theme.c.curveGraph.radius
        color: Theme.c.curveGraph.background
        border.width: Theme.c.curveGraph.borderWidth
        border.color: Theme.c.curveGraph.border

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
            // Unset for a moment when the plot binds before this item's own default does.
            curves: root.curves ?? ({})
            channel: root.channel
            hueMode: root.hueMode
            channelColors: root.channelColors
            gridColor: Theme.c.curveGraph.grid
            gridOpacity: Theme.c.curveGraph.gridOpacity
            gridLineWidth: Theme.c.curveGraph.gridWidth
            curveLineWidth: Theme.c.curveGraph.curveWidth
            dimLineWidth: Theme.c.curveGraph.armWidth
            handleRadius: root.handleRadius
            pointBackground: Theme.c.manipHandle.background
            knobBorderWidth: Theme.c.manipHandle.borderWidth
            deleteColor: Theme.c.curveGraph.deleteZone
            deleteOpacity: Theme.c.curveGraph.deleteZoneActiveOpacity
            onBegan: root.began()
            onChanged: (channel, json) => root.changed(channel, json)
            onEnded: root.ended()
            onCancelled: root.cancelled()
            onContextRequested: (index, pos) => root.barOpen = true
            onEntryRequested: (seed) => {
                root.typingAt = plot.graph.toPx(plot.selectedPoint.x, plot.selectedPoint.y)
                hud.beginEntry(seed)
            }
        }

        LiveReadout {
            id: hud
            visible: plot.dragging || entering
            text: plot.hudText
            pointer: plot.dragging ? plot.pointer : root.typingAt
            touch: false
            editable: !Theme.touchInput
            unit: plot.graph.entryUnit
            onApplied: (text, value) => plot.applyTyped(value)
            onEntryFinished: plot.forceActiveFocus()
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

    CurveGraphPointBar {
        visible: root.barOpen
        graph: plot.graph
        backendName: "color"
        anchors.horizontalCenter: frame.horizontalCenter
        anchors.bottom: frame.bottom
        anchors.bottomMargin: Theme.spacingSm
        z: 95
        onDismissed: root.barOpen = false
    }

    Text {
        id: hint
        y: readout.y + readout.height + Theme.spacingXs
        width: parent.width
        wrapMode: Text.WordWrap
        opacity: plot.pointCount <= 2 ? 1 : 0
        text: Theme.touchInput ? qsTr("Tap to add a point. Drag to move it. Hold a point for more, or drag it off the plot to remove.")
                               : qsTr("Click to add a point. Drag to move it. Double-click, Delete or drag it off to remove. Esc cancels a drag.")
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
