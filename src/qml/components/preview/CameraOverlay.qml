import QtQuick
import QtQuick.Controls.Basic
import Drift

// On-canvas control for the scene camera, live only while a Camera clip is selected.
//
// The camera sits at the eye, off-canvas, so there is nothing on screen to grab the way a clip's
// box can be grabbed — a 3D gizmo cannot be drawn for it. What this offers instead is a drag
// surface over the whole canvas, reading the gizmo tool the inspector already sets: Move pans,
// Rotate orbits, Scale dollies. Left-drag over a selected camera did nothing before, so no
// existing gesture changes meaning, and the preview's own wheel-zoom and middle-drag pan keep
// inspecting the canvas rather than moving the camera.
Item {
    id: root

    // Overlay px per canvas px, as the other preview overlays use.
    property real sx: 1
    property bool touch: false

    // True while the selected clip is the camera clip, which is the only time this is live.
    readonly property bool cameraSelected: {
        void EditorState.tracksRevision
        void EditorState.selectionRevision
        const data = EditorState.selectedClipData
        return !!data && data.kind === "adjustment" && data.adjustmentKind === "camera"
    }

    readonly property string tool: {
        void EditorState.gizmoTool
        return EditorState.gizmoTool || "move"
    }

    // Set for the duration of a drag so the owner can suppress its own refreshes.
    property bool dragging: false

    signal dragStarted()
    signal dragFinished()

    visible: cameraSelected
    enabled: cameraSelected
    z: 940

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        hoverEnabled: true
        cursorShape: root.tool === "rotate" ? Qt.SizeAllCursor
                     : root.tool === "scale" ? Qt.SizeVerCursor
                                             : Qt.OpenHandCursor

        property var startState: null
        property real pressX: 0
        property real pressY: 0

        onPressed: (mouse) => {
            const state = EditorState.cameraStateAtPlayhead()
            if (!state || !state.active) {
                mouse.accepted = false
                return
            }
            startState = state
            pressX = mouse.x
            pressY = mouse.y
            root.dragging = true
            // One undo entry for the whole drag, as the clip grips and the 3D gizmo do.
            EditorState.beginPreviewDrag()
            root.dragStarted()
        }

        onPositionChanged: (mouse) => {
            if (!root.dragging || !startState)
                return
            // Ctrl bypasses the 15-degree orbit steps, matching the rotate grip's modifier.
            const snap = root.tool === "rotate"
                         && (mouse.modifiers & Qt.ControlModifier) === 0
            startState = EditorState.previewApplyCameraDrag(startState, root.tool,
                                                            mouse.x - pressX, mouse.y - pressY,
                                                            root.sx, snap)
            // The returned state is the new starting point only in the sense that it carries the
            // values written; the press point stays fixed, so the drag is always solved from where
            // it began rather than accumulating.
        }

        onReleased: {
            if (!root.dragging)
                return
            root.dragging = false
            startState = null
            EditorState.commitPreviewDrag()
            root.dragFinished()
        }

        onCanceled: {
            if (!root.dragging)
                return
            root.dragging = false
            startState = null
            EditorState.cancelPreviewDrag()
            root.dragFinished()
        }
    }

    // What the drag will do, so the tool is not a guess. Sits out of the way at the top.
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Theme.spacingLg
        width: label.implicitWidth + Theme.spacingLg * 2
        height: label.implicitHeight + Theme.spacingMd * 2
        radius: Theme.radiusSm
        color: Theme.panelMuted
        opacity: 0.85
        visible: root.cameraSelected

        Text {
            id: label
            anchors.centerIn: parent
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            text: root.tool === "rotate" ? qsTr("Drag to orbit the camera")
                  : root.tool === "scale" ? qsTr("Drag up and down to dolly the camera")
                                          : qsTr("Drag to pan the camera")
        }
    }
}
