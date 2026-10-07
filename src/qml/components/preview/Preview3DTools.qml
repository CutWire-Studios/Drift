import QtQuick
import Drift
import ".."

// The 3D view's own tools, as a floating column: the gizmo's tool and axes, looking through the
// camera, and putting the camera or the view back.
Rectangle {
    id: tools

    function withShortcut(label, key) {
        return key.length > 0 ? qsTr("%1 (%2)").arg(label).arg(key) : label
    }

    width: column.implicitWidth + Theme.spacingXs * 2
    height: column.implicitHeight + Theme.spacingXs * 2
    radius: Theme.radiusMd
    color: Theme.panelBackground
    border.width: Theme.borderWidth
    border.color: Theme.panelBorder

    Column {
        id: column
        anchors.centerIn: parent
        spacing: 0

        Repeater {
            model: [
                { value: "move", glyph: Theme.icons.move3d, label: qsTr("Move"), action: "gizmoMove" },
                { value: "rotate", glyph: Theme.icons.rotate3d, label: qsTr("Rotate"), action: "gizmoRotate" },
                { value: "scale", glyph: Theme.icons.scale3d, label: qsTr("Scale"), action: "gizmoScale" }
            ]
            delegate: IconButton {
                required property var modelData
                glyph: modelData.glyph
                variant: "text"
                tooltip: tools.withShortcut(modelData.label, EditorState.shortcutFor(modelData.action))
                Accessible.name: modelData.label
                active: EditorState.preview.gizmoTool === modelData.value
                onClicked: EditorState.preview.gizmoTool = modelData.value
            }
        }

        IconButton {
            glyph: Theme.icons.locateFixed
            variant: "text"
            tooltip: EditorState.preview.gizmoOrientation === "local"
                     ? qsTr("Gizmo follows the selection's own axes (click for world axes)")
                     : qsTr("Gizmo follows the world axes (click for the selection's own)")
            active: EditorState.preview.gizmoOrientation === "local"
            onClicked: EditorState.preview.gizmoOrientation =
                       EditorState.preview.gizmoOrientation === "local" ? "global" : "local"
        }

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Theme.iconSizeBase
            height: Theme.borderWidth
            color: Theme.panelBorder
        }

        IconButton {
            glyph: Theme.icons.video
            variant: "text"
            tooltip: tools.withShortcut(qsTr("Look through the camera"), qsTr("Numpad 0"))
            active: EditorState.preview.lookThrough
            onClicked: EditorState.preview.toggleLookThrough()
        }
        IconButton {
            glyph: Theme.icons.refresh
            variant: "text"
            tooltip: qsTr("Reset the scene camera's position")
            enabled: {
                void EditorState.tracksRevision
                void EditorState.playheadSeconds
                return EditorState.preview.cameraActive()
            }
            onClicked: EditorState.preview.resetSceneCamera()
        }
        IconButton {
            glyph: Theme.icons.zoomFit
            variant: "text"
            tooltip: tools.withShortcut(qsTr("Frame the selection"), "F")
            onClicked: EditorState.preview.frameSelection()
        }
        IconButton {
            glyph: Theme.icons.reset
            variant: "text"
            tooltip: tools.withShortcut(qsTr("Reset the view"), "Home")
            onClicked: EditorState.preview.resetView()
        }
    }
}
