import QtQuick
import QtQuick.Window
import Drift
// Unit curve editor. X is always position through the effect; Y is what that position maps to.
// Two modes share it: "fade" edits a clip's edge-fade gain (silent at the bottom, full at the
// top) and "transition" edits a transition's progress curve. The geometry is identical, only the
// session it drives differs, so the editor is parameterised rather than duplicated.
ToolWindow {
    id: root

    property int trackIndex: -1
    property int clipIndex: -1
    property string mode: "fade" // "fade" | "transition"
    property string transitionId: ""
    readonly property bool isTransition: root.mode === "transition"
    // "points" = polyline through any number of knots; "bezier" = one cubic with pinned ends and
    // two handles, i.e. CSS cubic-bezier(). Two different shapes, not two views of one.
    property string shapeMode: "points"
    readonly property bool isBezier: root.shapeMode === "bezier"
    property var handles: [0.42, 0.0, 0.58, 1.0]
    property var points: []
    property bool closingAfterApply: false

    width: 520
    height: 420
    minimumWidth: 420
    minimumHeight: 340
    title: root.isTransition ? qsTr("Custom curve") : root.sessionHeading
    color: Theme.c.fadeCurveWindow.background

    // Which session the editor is driving. Everything below goes through these four.
    function sessionPoints() {
        return root.isTransition ? EditorState.curves.transitionPoints : EditorState.curves.fadePoints
    }

    function sessionHandles() {
        return root.isTransition ? EditorState.curves.transitionHandles : EditorState.curves.fadeHandles
    }

    function sessionMode() {
        return root.isTransition ? EditorState.curves.transitionMode : EditorState.curves.fadeMode
    }

    // Pushes the graph's shape into the session, which auditions it on the live clip or transition.
    function pushToSession() {
        if (root.isBezier) {
            const h = root.handles
            if (root.isTransition)
                EditorState.curves.setTransitionHandles(h[0], h[1], h[2], h[3])
            else
                EditorState.curves.setFadeHandles(h[0], h[1], h[2], h[3])
        } else if (root.isTransition) {
            EditorState.curves.setTransitionPoints(root.points)
        } else {
            EditorState.curves.setFadePoints(root.points)
        }
    }

    // The session sorts, clamps and pins what it is given, so its answer is what the graph shows.
    function pullFromSession() {
        root.points = root.sessionPoints()
        root.handles = root.sessionHandles()
        syncGraph()
    }

    function syncGraph() {
        if (graphView.graph.dragging || (!root.isBezier && root.points.length < 2))
            return
        if (root.isBezier)
            graphView.graph.setCubic(root.handles)
        else
            graphView.graph.setPoints(root.points.map(p => ({ x: p.t, y: p.g })))
    }

    // Switching mode is itself an edit: the session commits whichever shape is live, so moving to
    // bezier pushes the current handles and moving back pushes the current points.
    function setShapeMode(next) {
        if (root.shapeMode === next)
            return
        root.shapeMode = next
        root.pushToSession()
    }

    function sessionPreset(preset) {
        if (root.isTransition)
            EditorState.curves.resetTransitionPreset(preset)
        else
            EditorState.curves.resetFadePreset(preset)
    }

    function sessionApply() {
        if (root.isTransition)
            EditorState.curves.applyTransition()
        else
            EditorState.curves.applyFade()
    }

    function sessionEnd() {
        if (root.isTransition)
            EditorState.curves.endTransition()
        else
            EditorState.curves.endFade()
    }

    function applyPreset(preset) {
        root.sessionPreset(root.isBezier ? "bezier:" + preset : preset)
        root.pullFromSession()
        graphView.graph.clearSelection()
    }

    function syncFromSession() {
        root.shapeMode = root.sessionMode()
        root.pullFromSession()
        graphView.graph.clearSelection()
    }

    // Edge and target of the open clip session, "in"|"out" and "fade"|"animation".
    readonly property string sessionEdge: EditorState.curves.fadeEdge
    readonly property bool isAnimationTarget: !root.isTransition && EditorState.curves.fadeTarget === "animation"
    readonly property string sessionHeading: root.isAnimationTarget
        ? (root.sessionEdge === "out" ? qsTr("Out animation curve") : qsTr("In animation curve"))
        : (root.sessionEdge === "out" ? qsTr("Fade out curve") : qsTr("Fade in curve"))

    function openFor(track, clip, edge, target) {
        root.mode = "fade"
        root.transitionId = ""
        root.trackIndex = track
        root.clipIndex = clip
        root.closingAfterApply = false
        EditorState.curves.beginFade(track, clip, edge || "in", target || "fade")
        root.syncFromSession()
        root.show()
        root.raise()
        root.requestActivate()
    }

    function openForTransition(track, transitionId) {
        root.mode = "transition"
        root.transitionId = transitionId
        root.trackIndex = track
        root.clipIndex = -1
        root.closingAfterApply = false
        EditorState.curves.beginTransition(track, transitionId)
        root.syncFromSession()
        root.show()
        root.raise()
        root.requestActivate()
    }

    onClosing: {
        if (!root.closingAfterApply)
            root.sessionEnd()
    }

    Connections {
        target: EditorState.curves
        function onFadeApplied() {
            if (root.isTransition)
                return
            root.closingAfterApply = true
            root.close()
        }
        function onTransitionApplied() {
            if (!root.isTransition)
                return
            root.closingAfterApply = true
            root.close()
        }
    }

    Column {
        anchors.fill: parent
        anchors.margins: Theme.spacingXl
        spacing: Theme.spacingLg

        Row {
            width: parent.width
            spacing: Theme.spacingMd

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.isTransition
                      ? (EditorState.curves.transitionName.length
                         ? qsTr("Progress curve — %1").arg(EditorState.curves.transitionName)
                         : qsTr("Progress curve"))
                      : (EditorState.curves.fadeClipName.length
                         ? qsTr("%1 — %2").arg(root.sessionHeading).arg(EditorState.curves.fadeClipName)
                         : root.sessionHeading)
                color: Theme.c.fadeCurveWindow.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeBase
                font.weight: Font.Medium
                elide: Text.ElideRight
                width: Math.max(80, parent.width - (edgeRow.visible ? edgeRow.width + parent.spacing : 0))
            }

            Row {
                id: edgeRow
                visible: !root.isTransition
                spacing: 6
                anchors.verticalCenter: parent.verticalCenter

                ThemedChip {
                    text: qsTr("In")
                    selected: root.sessionEdge !== "out"
                    onClicked: root.openFor(root.trackIndex, root.clipIndex, "in",
                                            EditorState.curves.fadeTarget)
                }
                ThemedChip {
                    text: qsTr("Out")
                    selected: root.sessionEdge === "out"
                    onClicked: root.openFor(root.trackIndex, root.clipIndex, "out",
                                            EditorState.curves.fadeTarget)
                }
            }
        }

        Row {
            id: presetRow
            spacing: 6

            ThemedChip {
                text: qsTr("Points")
                selected: !root.isBezier
                onClicked: root.setShapeMode("points")
            }
            ThemedChip {
                text: qsTr("Bezier")
                selected: root.isBezier
                onClicked: root.setShapeMode("bezier")
            }

            FadeCurveToolbarRule { }

            ThemedChip {
                text: qsTr("Linear")
                onClicked: root.applyPreset("linear")
            }
            ThemedChip {
                text: root.isBezier ? qsTr("Ease") : qsTr("Smooth")
                onClicked: root.applyPreset(root.isBezier ? "ease" : "smooth")
            }
            ThemedChip {
                text: root.isBezier ? qsTr("Ease In") : qsTr("Natural")
                onClicked: root.applyPreset(root.isBezier ? "easeIn" : "equalPower")
            }
            ThemedChip {
                visible: root.isBezier
                text: qsTr("Ease Out")
                onClicked: root.applyPreset("easeOut")
            }
        }

        Text {
            width: parent.width
            text: root.isBezier
                  ? qsTr("Drag the two handles to shape the cubic. The ends stay pinned, and handles are held inside the box so the curve cannot fold back on itself. Shift keeps a handle flat, Ctrl suspends snapping, Esc cancels a drag.")
                  : qsTr("Click the curve to add a point and drag the points to shape the ramp (ends stay pinned). Delete or double-click removes, drag an empty spot to select several, Esc cancels a drag.")
            color: Theme.c.fadeCurveWindow.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            wrapMode: Text.WordWrap
        }

        CurveGraphView {
            id: graphView
            width: parent.width
            height: Math.max(180, root.height - 232)
            padX: 16
            padY: 16
            filled: true
            unitRange: true
            backend: root.isBezier ? "cubic" : "polyline"
            focus: true
            undoText: qsTr("Edit curve")

            onSetupDone: root.syncGraph()
            onEdited: {
                if (root.isBezier) {
                    root.handles = graphView.graph.cubic()
                } else {
                    root.points = graphView.graph.points().map(p => ({ t: p.x, g: p.y }))
                }
                root.pushToSession()
            }
            onGestureEnded: root.pullFromSession()
            onGestureCancelled: {
                if (root.isBezier)
                    root.handles = graphView.graph.cubic()
                else
                    root.points = graphView.graph.points().map(p => ({ t: p.x, g: p.y }))
                root.pushToSession()
            }
        }

        Row {
            width: parent.width
            spacing: Theme.spacingMd
            layoutDirection: Qt.RightToLeft

            ThemedButton {
                text: qsTr("Apply")
                onClicked: root.sessionApply()
            }
            ThemedButton {
                text: qsTr("Cancel")
                variant: "ghost"
                onClicked: root.close()
            }
            Item { width: parent.width; height: 1 }
        }
    }
}
