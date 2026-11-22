import QtQuick
import Drift
// The editor viewport: the world from a free viewpoint, with the scene camera and the stage drawn
// in it. Renders as the Editor preview stream, which only runs while this panel is shown, beside
// the Edit preview's camera output. The tools sit in a header above the image, not on it.
PanelFrame {
    id: root
    SearchAnchor {
        searchId: "viewport3d-panel"
        title: qsTr("3D viewport")
        keywords: "3d viewport scene view orbit free camera perspective world"
    }

    property bool shown: false

    readonly property var viewPresets: [
        { id: "perspective", label: qsTr("Perspective") },
        { id: "front", label: qsTr("Front") },
        { id: "back", label: qsTr("Back") },
        { id: "left", label: qsTr("Left") },
        { id: "right", label: qsTr("Right") },
        { id: "top", label: qsTr("Top") },
        { id: "bottom", label: qsTr("Bottom") },
        { id: "camera", label: qsTr("Scene camera") }
    ]
    readonly property bool hasCamera: {
        void EditorState.tracksRevision
        return EditorState.preview.projectHasCamera()
    }

    function applyViewPreset(id) {
        const preview = EditorState.preview
        if (id === "camera") {
            if (!preview.lookThrough)
                preview.toggleLookThrough()
        } else if (id === "perspective") {
            if (preview.lookThrough)
                preview.toggleLookThrough()
            preview.orthographic = false
        } else {
            preview.setAxisView(id)
        }
    }

    color: Theme.c.previewPanel.background
    border.width: Theme.c.previewPanel.borderWidth
    radius: Theme.c.previewPanel.radius

    readonly property string scope: "viewport3d"
    readonly property int priority: 8
    readonly property var focusItem: Window.activeFocusItem

    function cycleGizmoOrientation() {
        const order = ["world", "local", "view"]
        const preview = EditorState.preview
        preview.gizmoOrientation = order[(order.indexOf(preview.gizmoOrientation) + 1) % order.length]
    }

    // The viewport is in use while the panel is shown or the preview is in 3D mode (which can be
    // requested before the panel is on screen).
    readonly property bool engaged: shown || EditorState.preview.viewport3dOpen

    // The gizmo tools work whenever the viewport is in use; the view keys (F, Home, the numpad)
    // share chords with the masking and timeline keys, so they need the keyboard in the panel.
    function handleAction(id, force) {
        if (!engaged || (focusItem && focusItem.cursorPosition !== undefined))
            return false
        switch (id) {
        case "viewport3d.gizmoMove": EditorState.preview.gizmoTool = "move"; return true
        case "viewport3d.gizmoRotate": EditorState.preview.gizmoTool = "rotate"; return true
        case "viewport3d.gizmoScale": EditorState.preview.gizmoTool = "scale"; return true
        case "viewport3d.gizmoOrientation": cycleGizmoOrientation(); return true
        }
        return id.startsWith("viewport3d.") && (force || ShellBridge.focusWithin(root, focusItem)) && mode.handleViewAction(id)
    }

    onShownChanged: {
        EditorState.playback.setStreamEnabled(1, shown)
        EditorState.preview.viewport3dOpen = shown
    }
    onEngagedChanged: {
        if (engaged)
            ShellBridge.registerActionHandler(root)
        else
            ShellBridge.unregisterActionHandler(root)
    }
    Component.onCompleted: {
        if (engaged)
            ShellBridge.registerActionHandler(root)
    }
    Component.onDestruction: {
        ShellBridge.unregisterActionHandler(root)
        EditorState.playback.setStreamEnabled(1, false)
        EditorState.preview.viewport3dOpen = false
    }

    Rectangle {
        id: bar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: toolFlow.implicitHeight + Theme.spacingXs * 2 + Theme.c.viewport3dPanel.dividerWidth
        color: Theme.c.viewport3dPanel.toolbarBackground

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: Theme.c.viewport3dPanel.dividerWidth
            color: Theme.c.viewport3dPanel.divider
        }

        Flow {
            id: toolFlow
            x: Theme.spacingSm
            y: Theme.spacingXs
            width: parent.width - Theme.spacingSm * 2
            spacing: Theme.spacingXs

            ThemedComboBox {
                id: viewCombo
                implicitHeight: Theme.controlHeightSm
                width: Math.max(96, widestContentWidth)
                model: root.viewPresets.map(p => p.label)
                displayText: EditorState.preview.lookThrough ? qsTr("Scene camera") : qsTr("View")
                tooltip: qsTr("Look at the scene from a preset side, or through the scene camera")
                onActivated: root.applyViewPreset(root.viewPresets[currentIndex].id)
            }

            ThemedChip {
                text: qsTr("Ortho")
                tooltip: qsTr("Orthographic projection")
                tooltipActionId: "viewport3d.toggleOrthographic"
                selected: EditorState.preview.orthographic
                onClicked: EditorState.preview.toggleOrthographic()
            }

            Rectangle { width: Theme.c.viewport3dPanel.dividerWidth; height: Theme.controlHeightSm - 8; color: Theme.c.viewport3dPanel.divider }

            Repeater {
                model: [
                    { value: "move", glyph: Theme.icons.move3d, label: qsTr("Move"), action: "viewport3d.gizmoMove" },
                    { value: "rotate", glyph: Theme.icons.rotate3d, label: qsTr("Rotate"), action: "viewport3d.gizmoRotate" },
                    { value: "scale", glyph: Theme.icons.scale3d, label: qsTr("Scale"), action: "viewport3d.gizmoScale" }
                ]
                delegate: IconButton {
                    required property var modelData
                    glyph: modelData.glyph
                    variant: "text"
                    tooltip: modelData.label
                    tooltipActionId: modelData.action
                    Accessible.name: modelData.label
                    active: EditorState.preview.gizmoTool === modelData.value
                    onClicked: EditorState.preview.gizmoTool = modelData.value
                }
            }

            ThemedComboBox {
                readonly property var ids: ["world", "local", "view"]
                implicitHeight: Theme.controlHeightSm
                width: Math.max(84, widestContentWidth)
                model: [qsTr("World"), qsTr("Local"), qsTr("View")]
                currentIndex: Math.max(0, ids.indexOf(EditorState.preview.gizmoOrientation))
                tooltip: qsTr("Which axes the gizmo follows: the scene's, the selected object's or the screen's")
                tooltipActionId: "viewport3d.gizmoOrientation"
                onActivated: EditorState.preview.gizmoOrientation = ids[currentIndex]
            }

            Rectangle { width: Theme.c.viewport3dPanel.dividerWidth; height: Theme.controlHeightSm - 8; color: Theme.c.viewport3dPanel.divider }

            IconButton {
                glyph: Theme.icons.fly
                tooltip: qsTr("Fly (WASD to move, Q/E down/up, steer toward the pointer)")
                tooltipActionId: "viewport3d.toggleFly"
                active: EditorState.preview.flying
                onClicked: EditorState.preview.flying = !EditorState.preview.flying
            }
            IconButton {
                glyph: Theme.icons.video
                tooltip: qsTr("Look through the scene camera")
                tooltipActionId: "viewport3d.lookThrough"
                active: EditorState.preview.lookThrough
                onClicked: EditorState.preview.toggleLookThrough()
            }
            IconButton {
                glyph: Theme.icons.lock
                tooltip: qsTr("Lock camera to view: navigating while looking through the camera moves the camera")
                active: EditorState.preview.lockCameraToView
                onClicked: EditorState.preview.lockCameraToView = !EditorState.preview.lockCameraToView
            }
            IconButton {
                glyph: Theme.icons.crosshair
                tooltip: qsTr("Align the scene camera to this view")
                enabled: root.hasCamera
                onClicked: EditorState.preview.alignCameraToView()
            }

            Rectangle { width: Theme.c.viewport3dPanel.dividerWidth; height: Theme.controlHeightSm - 8; color: Theme.c.viewport3dPanel.divider }

            IconButton {
                glyph: Theme.icons.zoomFit
                tooltip: qsTr("Frame the selection")
                tooltipActionId: "viewport3d.frameSelected"
                onClicked: EditorState.preview.frameSelection()
            }
            IconButton {
                glyph: Theme.icons.maximize
                tooltip: qsTr("Frame everything")
                tooltipActionId: "viewport3d.frameAll"
                onClicked: EditorState.preview.frameAll()
            }
            IconButton {
                glyph: Theme.icons.reset
                tooltip: qsTr("Reset the view")
                tooltipActionId: "viewport3d.resetView"
                onClicked: EditorState.preview.resetView()
            }
        }
    }

    PreviewViewport {
        id: editorView
        anchors.top: bar.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingLg
        stream: 1

        toolHost.forcedTool: "transform"
        toolHost.tools: ({ transform: transformTool })
        toolHost.companion: lightsCompanion

        Keys.onPressed: (event) => {
            const id = ShellBridge.scopedAction("viewport3d", event)
            if (id !== "" && root.handleAction(id, true))
                event.accepted = true
        }

        Preview3DMode {
            id: mode
            anchors.fill: parent
            z: -1
            viewport: editorView
        }
    }

    Component {
        id: transformTool
        TransformManip {
            canvas: editorView.canvas
            viewport: 1
        }
    }

    Component {
        id: lightsCompanion
        Item {
            DepthLight3dOverlay {
                x: editorView.canvas.x
                y: editorView.canvas.y
                width: editorView.canvas.width
                height: editorView.canvas.height
                visible: EditorState.preview.handlesVisible
            }
        }
    }
}
