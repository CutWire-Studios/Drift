import QtQuick
import Drift

// The one 2D curve editor. The interaction (hit-testing at 14 px for a mouse and 24 px for a
// finger, clamping, tangent rules, snapping, marquee, nudge, add / delete / reset, and one undo
// step per gesture with Esc reverting it) lives in CurveGraphModel; this draws what the model
// lists and forwards the pointer and keys.
//
// Backends: "cubic" (easing and the fade bezier: pinned ends, two handles), "polyline" (the fade
// points), "speed" (log-speed ramp with tangents) and "samples" (a read-only curve). The host
// pushes its data in with graph.setCubic / setPoints / setSamples whenever `setupDone` fires or its
// source changes, and reads it back from `edited`. A host that edits the project sets
// `projectBacked` so a drag runs through EditorState.drag.
//
// Mouse: click the curve to add a point, drag points and handles, Alt-drag a handle to break it,
// Shift holds a point to one axis and a handle flat, Ctrl suspends snapping, drag an empty spot for a
// marquee, Delete / double-click removes, arrows nudge 1 px (Shift 10), Esc cancels a drag, digits
// type a value. Touch: long-press a point for its bar, pinch and drag with two fingers to zoom
// and pan, Snap on by default.
FocusScope {
    id: view

    property string backend: "cubic"
    property bool unitRange: false
    property real minSpeed: 0.1
    property real maxSpeed: 100
    property real padX: 8
    property real padY: 8
    property bool filled: false
    property bool readOnly: false
    property bool zoomX: false
    property bool zoomY: false
    property bool projectBacked: false
    property string undoText: qsTr("Edit curve")
    property bool showBackground: true
    property bool touchUi: Theme.touchInput
    property point typingAt: Qt.point(0, 0)

    readonly property alias graph: graph
    readonly property bool dragging: graph.dragging
    property bool barOpen: false
    property int pressDevice: DragSession.Mouse

    signal setupDone()
    signal edited()
    signal gestureBegan()
    signal gestureEnded()
    signal gestureCancelled()

    function setup() {
        if (backend === "cubic")
            graph.setupCubic(unitRange)
        else if (backend === "polyline")
            graph.setupPolyline()
        else if (backend === "speed")
            graph.setupSpeed(minSpeed, maxSpeed)
        else
            graph.setupSamples()
        graph.zoomX = zoomX
        graph.zoomY = zoomY
        setupDone()
    }

    property bool ready: false
    onBackendChanged: if (ready) setup()
    onUnitRangeChanged: if (ready) setup()
    Component.onCompleted: {
        ready = true
        setup()
    }

    function selectedKnob() {
        const list = graph.knobs
        for (const k of list) {
            if (k.selected && k.kind !== "anchor")
                return k
        }
        return null
    }

    function openBarAt(index) {
        if (index < 0 || readOnly)
            return
        graph.selectOnly(index)
        barOpen = true
    }

    activeFocusOnTab: true

    CurveGraphModel {
        id: graph
        plot: Qt.rect(view.padX, view.padY, Math.max(1, view.width - view.padX * 2), Math.max(1, view.height - view.padY * 2))
        session: view.projectBacked ? EditorState.drag : null
        undoText: view.undoText
        onEdited: view.edited()
        onGestureBegan: view.gestureBegan()
        onGestureEnded: view.gestureEnded()
        onGestureCancelled: view.gestureCancelled()
    }

    Binding { target: graph; property: "readOnly"; value: view.readOnly || view.backend === "samples" }

    Rectangle {
        anchors.fill: parent
        visible: view.showBackground
        radius: Theme.c.curveGraph.radius
        color: Theme.c.curveGraph.background
        border.width: Theme.c.curveGraph.borderWidth
        border.color: Theme.c.curveGraph.border
    }

    Item {
        id: stage
        anchors.fill: parent
        clip: true

        CurveGraphGrid {
            anchors.fill: parent
            graph: graph
        }

        CurveGraphCurve {
            anchors.fill: parent
            graph: graph
            filled: view.filled
        }

        Rectangle {
            visible: graph.snapGuideX >= 0
            x: graph.snapGuideX - width / 2
            y: graph.plot.top
            width: Theme.c.manipSnapLine.strokeWidth
            height: graph.plot.height
            color: Theme.c.manipSnapLine.background
        }

        Rectangle {
            visible: graph.snapGuideY >= 0
            x: graph.plot.left
            y: graph.snapGuideY - height / 2
            width: graph.plot.width
            height: Theme.c.manipSnapLine.strokeWidth
            color: Theme.c.manipSnapLine.background
        }

        Rectangle {
            visible: graph.marqueeActive
            x: graph.marquee.x
            y: graph.marquee.y
            width: graph.marquee.width
            height: graph.marquee.height
            color: Qt.rgba(Theme.c.curveGraph.marqueeFill.r, Theme.c.curveGraph.marqueeFill.g,
                           Theme.c.curveGraph.marqueeFill.b, Theme.c.curveGraph.marqueeOpacity)
            border.width: Theme.c.curveGraph.borderWidth
            border.color: Theme.c.curveGraph.marqueeBorder
        }

        CurveGraphKnobs {
            anchors.fill: parent
            graph: graph
        }
    }

    ThemedChip {
        visible: graph.zoomed
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.spacingXs
        text: qsTr("Reset view")
        variant: "outline"
        onClicked: graph.resetView()
    }

    MouseArea {
        id: input
        anchors.fill: parent
        hoverEnabled: true
        preventStealing: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        enabled: !view.readOnly && view.backend !== "samples"
        cursorShape: graph.hot >= 0 ? Qt.PointingHandCursor : Qt.CrossCursor

        function deviceOf(mouse) {
            return mouse.source !== Qt.MouseEventNotSynthesized ? DragSession.Touch : DragSession.Mouse
        }

        onPressed: (mouse) => {
            view.forceActiveFocus()
            view.pressDevice = deviceOf(mouse)
            if (mouse.button === Qt.RightButton) {
                view.openBarAt(graph.pointAt(Qt.point(mouse.x, mouse.y), view.pressDevice))
                return
            }
            if (view.barOpen)
                view.barOpen = false
            graph.press(Qt.point(mouse.x, mouse.y), mouse.modifiers, view.pressDevice)
        }
        onPositionChanged: (mouse) => {
            if (pressed)
                graph.move(Qt.point(mouse.x, mouse.y), mouse.modifiers)
            else
                graph.hover(Qt.point(mouse.x, mouse.y), DragSession.Mouse)
        }
        onReleased: (mouse) => {
            if (mouse.button === Qt.LeftButton)
                graph.release(Qt.point(mouse.x, mouse.y), mouse.modifiers)
        }
        onCanceled: graph.cancel()
        onExited: graph.clearHover()
        onDoubleClicked: (mouse) => graph.doubleClick(Qt.point(mouse.x, mouse.y), deviceOf(mouse))
        onPressAndHold: (mouse) => {
            if (mouse.button !== Qt.LeftButton || mouse.source === Qt.MouseEventNotSynthesized)
                return
            const idx = graph.pointAt(Qt.point(mouse.x, mouse.y), DragSession.Touch)
            if (idx < 0)
                return
            graph.cancelPress()
            view.openBarAt(idx)
        }
        onWheel: (wheel) => {
            if ((view.zoomX || view.zoomY) && Theme.primaryModifierPressed(wheel.modifiers) && wheel.angleDelta.y !== 0) {
                graph.zoomAt(wheel.x, wheel.y, wheel.angleDelta.y > 0 ? 1.15 : 1 / 1.15)
                wheel.accepted = true
            } else {
                wheel.accepted = false
            }
        }
    }

    ViewNavigation {
        anchors.fill: parent
        autoApply: false
        visible: view.zoomX || view.zoomY
        wheelZoom: false
        onZoomed: (x, y, factor) => graph.zoomAt(x, y, factor)
        onPanned: (dx, dy) => graph.panBy(dx, dy)
    }

    LiveReadout {
        id: hud
        visible: graph.dragging || entering
        text: graph.hudText
        pointer: graph.dragging ? graph.pointer : view.typingAt
        touch: view.pressDevice !== DragSession.Mouse
        editable: !view.touchUi || view.pressDevice === DragSession.Mouse
        unit: graph.entryUnit
        onApplied: (text, value) => graph.applyTyped(value)
        onEntryFinished: view.forceActiveFocus()
    }

    CurveGraphPointBar {
        id: bar
        visible: view.barOpen
        graph: graph
        backendName: view.backend
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spacingSm
        z: 95
        onDismissed: view.barOpen = false
    }

    Keys.onPressed: (event) => {
        if (view.readOnly || view.backend === "samples") {
            event.accepted = false
            return
        }
        if (event.key === Qt.Key_Escape) {
            if (hud.entering)
                hud.cancelEntry()
            else if (graph.dragging)
                graph.cancel()
            else if (view.barOpen)
                view.barOpen = false
            else {
                event.accepted = false
                return
            }
            event.accepted = true
        } else if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
            graph.deleteSelected()
            event.accepted = true
        } else if (event.key === Qt.Key_A && (event.modifiers & Qt.ControlModifier)) {
            graph.selectAll()
            event.accepted = true
        } else if (event.key === Qt.Key_Left || event.key === Qt.Key_Right
                   || event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
            graph.nudge(event.key === Qt.Key_Left ? -1 : event.key === Qt.Key_Right ? 1 : 0,
                        event.key === Qt.Key_Up ? 1 : event.key === Qt.Key_Down ? -1 : 0,
                        (event.modifiers & Qt.ShiftModifier) !== 0)
            event.accepted = true
        } else if (hud.editable && graph.primary >= 0 && !hud.entering
                   && event.text.length === 1 && "0123456789-.".indexOf(event.text) >= 0) {
            const k = view.selectedKnob()
            if (k && !graph.dragging)
                view.typingAt = Qt.point(k.x, k.y)
            hud.beginEntry(event.text)
            event.accepted = true
        } else {
            event.accepted = false
        }
    }
}
