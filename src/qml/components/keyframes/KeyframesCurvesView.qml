pragma ComponentBehavior: Bound
import QtQuick
import Drift

Item {
    id: root

    required property var panel

    readonly property var timeView: panel.timeView
    readonly property alias curves: curvesModel
    readonly property bool busy: surface.busy
    readonly property real graphMargin: Theme.spacingLg
    readonly property real graphHeight: curvesModel.graphHeight

    property string focusedTrack: ""
    property string hoverId: ""

    signal contextRequested(real x, real y, var hit)

    readonly property var series: curvesModel.series
    readonly property int focusedIndex: curvesModel.focusedIndex

    function fitValues(selectedOnly) {
        curvesModel.fitValues(selectedOnly)
    }

    function cancelDrag() {
        surface.cancelDrag()
    }

    clip: true

    KeyframeCurvesModel {
        id: curvesModel
        rowsModel: root.panel.rowsModel
        selection: root.panel.selection
        normalize: root.panel.normalize
        curveShown: root.panel.curveShown
        focusedProp: root.panel.focusedProp
        focusedTrack: root.focusedTrack
        showHandles: root.panel.showHandles
        graphMargin: root.graphMargin
        viewHeight: root.height
    }

    Connections {
        target: root.panel

        function onTrackChanged() { curvesModel.requestFit() }
        function onClipChanged() { curvesModel.requestFit() }
        function onNormalizeChanged() {
            if (!root.panel.normalize)
                curvesModel.fitValues(false)
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.c.keyframesCurves.background
    }

    Rectangle {
        readonly property real x0: root.timeView.xFor(root.timeView.clipStart)
        readonly property real x1: root.timeView.xFor(root.timeView.clipStart + root.timeView.clipDuration)
        x: Math.max(0, x0)
        width: Math.max(0, Math.min(root.width, x1) - x)
        height: parent.height
        color: Qt.rgba(Theme.c.keyframesCurves.clipSpan.r, Theme.c.keyframesCurves.clipSpan.g,
                       Theme.c.keyframesCurves.clipSpan.b, Theme.c.keyframesCurves.opacityClipSpan)
    }

    Repeater {
        model: curvesModel.gridTicks
        delegate: Rectangle {
            required property var modelData

            y: root.graphMargin + root.graphHeight * (1 - modelData.frac)
            width: root.width
            height: Theme.c.panelFrame.borderWidth
            color: Qt.rgba(Theme.c.keyframesCurves.grid.r, Theme.c.keyframesCurves.grid.g,
                           Theme.c.keyframesCurves.grid.b, Theme.c.keyframesCurves.opacityGrid)
        }
    }

    KeyframeCurvesItem {
        anchors.fill: parent
        model: curvesModel
        selection: root.panel.selection
        controller: EditorState.keyframes
        pxPerSecond: root.timeView.pxPerSecond
        originX: root.timeView.contentX
        graphMargin: root.graphMargin
        clipStart: root.timeView.clipStart
        clipDuration: root.timeView.clipDuration
        lineWidth: Theme.c.keyframeGraphCurveCanvas.lineWidth
        lineWidthMuted: Theme.c.keyframeGraphCurveCanvas.lineWidthMuted
        opacityContext: Theme.c.keyframeGraphCurveCanvas.opacityContext
        opacityDisabled: Theme.c.keyframeGraphCurveCanvas.opacityDisabled
        showKeys: true
        hoverId: root.hoverId
        keyOpacityContext: Theme.c.keyframesCurves.opacityContext
        animationDuration: Theme.durationFast
        dotStyle: Theme.plainMap(Theme.c.keyframesKeyDot)
        handleStyle: Theme.plainMap(Theme.c.keyframeGraphTangentHandle)
    }

    KeyframesCurvesInteraction {
        id: surface
        z: 3
        anchors.fill: parent
        panel: root.panel
        view: root
        onContextRequested: (x, y, hit) => root.contextRequested(x, y, hit)
    }

    Item {
        z: 6
        anchors.fill: parent

        Repeater {
            model: curvesModel.labelRange.labelled ? curvesModel.gridTicks : []
            delegate: Text {
                required property var modelData

                x: Theme.spacingXs
                y: root.graphMargin + root.graphHeight * (1 - modelData.frac) - height
                text: modelData.text
                color: curvesModel.labelRange.color ?? Theme.c.keyframesCurves.label
                font.family: Theme.monoFontFamily
                font.pixelSize: Theme.fontSizeXs
            }
        }
    }
}
