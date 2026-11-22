import QtQuick
import QtQuick.Window
import Drift
// Speed-ramp editor for one clip. The clip is auditioned on its own — raw decode, no effects,
// no other tracks — so what the graph does to it is the only thing you are looking at.
//
// The graph's x axis is position through the *source*: the filmstrip above it is evenly spaced
// original frames, and the playhead sweeps them at a variable rate. A high point means the
// playhead races through that stretch; a low one means it crawls.
//
// A window rather than a dialog so the timeline stays visible, matching SegmentationWindow and
// DenoiseWindow.
ToolWindow {
    id: root

    property int trackIndex: -1
    property int clipIndex: -1

    // Local mirror of the curve, and the source of truth while the window is open: reading it
    // back out of the controller mid-drag would fight the drag for the same values.
    property var points: []

    readonly property real minSpeed: 0.1
    readonly property real maxSpeed: 100.0

    width: 900
    height: 720
    minimumWidth: 680
    minimumHeight: 560
    title: qsTr("Speed ramp")
    color: Theme.c.speedCurveWindow.background

    function openFor(track, clip) {
        root.trackIndex = track
        root.clipIndex = clip
        EditorState.speedCurve.begin(track, clip)
        root.points = EditorState.speedCurve.points
        root.syncGraph()
        graphView.graph.clearSelection()
        root.show()
        root.raise()
        root.requestActivate()
    }

    onClosing: EditorState.speedCurve.end()

    Connections {
        target: EditorState.speedCurve
        function onApplied() { root.close() }
    }

    function toGraph(points) {
        return points.map(p => ({ x: p.pos, y: p.speed, inDx: p.inDx, inDy: p.inDy,
                                  outDx: p.outDx, outDy: p.outDy, smooth: !p.corner }))
    }

    function fromGraph(points) {
        return points.map(p => ({ pos: p.x, speed: p.y, inDx: p.inDx, inDy: p.inDy,
                                  outDx: p.outDx, outDy: p.outDy, corner: !p.smooth }))
    }

    function syncGraph() {
        if (!graphView.graph.dragging && root.points.length >= 2)
            graphView.graph.setPoints(root.toGraph(root.points))
    }

    // The controller sorts, clamps and pins the ends, so reading its result straight back is what
    // keeps the local mirror honest. Only done when a gesture ends: mid-drag it would fight the drag.
    function commit() {
        EditorState.speedCurve.setPoints(root.points)
        root.points = EditorState.speedCurve.points
        root.syncGraph()
    }

    readonly property var selected: graphView.graph.primary >= 0 && graphView.graph.primary < root.points.length
                                    ? root.points[graphView.graph.primary] : null

    Column {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: footer.top
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingMd

        // ----- Preview ----------------------------------------------------------------------
        SpeedCurvePreviewStage {
            transportItem: transport
            stripItem: strip
            toolbarItem: toolbar
            graphItem: graph
        }

        // ----- Transport --------------------------------------------------------------------
        Row {
            id: transport
            width: parent.width
            spacing: Theme.spacingMd

            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                glyph: EditorState.speedCurve.playing ? Theme.icons.pause : Theme.icons.play
                tooltip: EditorState.speedCurve.playing ? qsTr("Pause") : qsTr("Play")
                onClicked: EditorState.speedCurve.playing
                           ? EditorState.speedCurve.pause()
                           : EditorState.speedCurve.play()
            }

            ThemedLabel {
                anchors.verticalCenter: parent.verticalCenter
                text: EditorState.speedCurve.position.toFixed(2) + qsTr("s / ")
                      + EditorState.speedCurve.retimedDuration.toFixed(2) + qsTr("s")
            }

            ThemedLabel {
                anchors.verticalCenter: parent.verticalCenter
                tone: "muted"
                text: EditorState.speedCurve.sourceDuration.toFixed(2) + qsTr("s → ")
                      + EditorState.speedCurve.retimedDuration.toFixed(2) + qsTr("s")
            }
        }

        // ----- Clip strip -------------------------------------------------------------------
        SpeedCurveFilmstrip { id: strip }

        // ----- Toolbar ----------------------------------------------------------------------
        Row {
            id: toolbar
            width: parent.width
            spacing: Theme.spacingSm

            ThemedButton {
                variant: "secondary"
                text: qsTr("Add point")
                onClicked: graphView.graph.addPointAtX(EditorState.speedCurve.sourcePosition)
            }

            ThemedToggleButton {
                text: qsTr("Sharp")
                enabled: graphView.graph.selectionCount > 0
                checked: graphView.graph.selectionCount > 0 && !graphView.graph.selectedSmooth
                onClicked: graphView.graph.setSelectedSmooth(false)
            }

            ThemedToggleButton {
                text: qsTr("Smooth")
                enabled: graphView.graph.selectionCount > 0
                checked: graphView.graph.selectionCount > 0 && graphView.graph.selectedSmooth
                onClicked: graphView.graph.setSelectedSmooth(true)
            }

            ThemedButton {
                variant: "ghost"
                text: qsTr("Delete point")
                enabled: graphView.graph.canDelete
                onClicked: graphView.graph.deleteSelected()
            }

            ThemedButton {
                variant: "ghost"
                text: qsTr("Reset")
                onClicked: graphView.graph.resetCurve()
            }

            ThemedLabel {
                anchors.verticalCenter: parent.verticalCenter
                tone: "muted"
                visible: root.selected !== null
                text: root.selected ? root.selected.speed.toFixed(2) + "×" : ""
            }
        }

        // ----- Graph ------------------------------------------------------------------------
        Item {
            id: graph
            width: parent.width
            height: 220

            // The plot's x axis has to be the same axis the strip above draws on, or a point on the
            // curve would not sit over its own frame, so the graph is inset vertically only.
            CurveGraphView {
                id: graphView
                anchors.fill: parent
                padX: 0
                padY: Theme.spacingSm
                backend: "speed"
                minSpeed: root.minSpeed
                maxSpeed: root.maxSpeed
                zoomY: true
                undoText: qsTr("Edit speed curve")

                onSetupDone: root.syncGraph()
                onEdited: root.points = root.fromGraph(graphView.graph.points())
                onGestureEnded: root.commit()
                onGestureCancelled: root.points = root.fromGraph(graphView.graph.points())
            }

            // Playhead, sweeping the source axis at whatever rate the curve dictates, and
            // draggable like the timeline's own playhead. It is a thin strip over the graph, so
            // the curve's points keep every press that is not on that strip.
            Item {
                id: plot
                anchors.fill: parent
                anchors.topMargin: Theme.spacingSm
                anchors.bottomMargin: Theme.spacingSm

                SpeedCurveGraphPlayhead { plotItem: plot }
            }
        }
    }

    // ----- Footer ---------------------------------------------------------------------------
    Row {
        id: footer
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingMd

        ThemedLabel {
            anchors.verticalCenter: parent.verticalCenter
            tone: "muted"
            text: qsTr("Applied as a copy on a new track — the original clip is left alone.")
        }

        ThemedButton {
            variant: "ghost"
            text: qsTr("Cancel")
            onClicked: root.close()
        }

        ThemedButton {
            variant: "primary"
            text: qsTr("Apply")
            onClicked: EditorState.speedCurve.apply()
        }
    }
}
