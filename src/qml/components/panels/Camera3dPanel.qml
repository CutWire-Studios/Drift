pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Drift
// The scene cameras: every camera clip in the sequence, with the selected one's settings below
// (the same CameraInspector the Camera tab of the Properties panel shows).
PanelFrame {
    id: root
    SearchAnchor {
        searchId: "camera3d-panel"
        title: qsTr("Camera")
        keywords: "3d scene camera cameras list look through"
    }

    property bool shown: false

    readonly property var cameras: {
        void EditorState.tracksRevision
        void EditorState.inspectorPlayheadSeconds
        return EditorState.preview.sceneCameras()
    }
    readonly property bool hasCameraTrack: {
        void EditorState.tracksRevision
        return EditorState.hasCameraTrack()
    }

    AssetsPanelHeader {
        id: header
        width: parent.width
        height: Theme.panelHeaderHeight
        title: qsTr("Camera")
    }

    Flickable {
        id: flick
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        clip: true
        contentWidth: width
        contentHeight: content.implicitHeight + Theme.spacingLg * 2
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar {}

        Column {
            id: content
            x: Theme.spacingLg
            y: Theme.spacingLg
            width: flick.width - Theme.spacingLg * 2
            spacing: Theme.spacingMd

            Flow {
                width: parent.width
                spacing: Theme.spacingSm

                ThemedButton {
                    text: qsTr("Add camera")
                    tooltip: root.hasCameraTrack ? qsTr("Add a camera clip at the playhead, for a camera cut")
                                                 : qsTr("Add a camera track with a camera clip over the sequence")
                    onClicked: {
                        if (root.hasCameraTrack)
                            EditorState.addCameraClip()
                        else
                            EditorState.addCameraTrack()
                    }
                }
                IconButton {
                    glyph: Theme.icons.lock
                    tooltip: qsTr("Lock camera to view: navigating while looking through the camera moves the camera")
                    active: EditorState.preview.lockCameraToView
                    onClicked: EditorState.preview.lockCameraToView = !EditorState.preview.lockCameraToView
                }
                IconButton {
                    glyph: Theme.icons.crosshair
                    tooltip: qsTr("Align the scene camera to the 3D viewport's view")
                    enabled: root.cameras.length > 0
                    onClicked: EditorState.preview.alignCameraToView()
                }
            }

            Text {
                width: parent.width
                visible: root.cameras.length === 0
                text: qsTr("No camera yet. Without one every clip is seen through its own Perspective.")
                color: Theme.c.camera3dPanel.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                wrapMode: Text.WordWrap
            }

            Column {
                width: parent.width

                Repeater {
                    model: root.cameras

                    delegate: Rectangle {
                        id: row
                        required property var modelData
                        readonly property bool selected: {
                            void EditorState.selectionRevision
                            return EditorState.selectedTrack === row.modelData.track
                                   && EditorState.selectedClip === row.modelData.clip
                        }

                        width: parent.width
                        height: 30
                        radius: Theme.c.previewPanel.radius
                        color: row.selected ? Theme.c.camera3dPanel.rowSelected
                                            : (rowHover.hovered ? Theme.c.camera3dPanel.rowHover : "transparent")

                        HoverHandler { id: rowHover }
                        TapHandler {
                            onTapped: EditorState.selectClip(row.modelData.track, row.modelData.clip)
                        }

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: Theme.spacingMd
                            anchors.right: lookButton.left
                            anchors.rightMargin: Theme.spacingSm
                            anchors.verticalCenter: parent.verticalCenter
                            text: (row.modelData.name.length > 0 ? row.modelData.name : qsTr("Camera"))
                                  + (row.modelData.active ? qsTr(" (active)") : "")
                            elide: Text.ElideRight
                            color: Theme.c.camera3dPanel.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeSm
                        }

                        IconButton {
                            id: lookButton
                            anchors.right: parent.right
                            anchors.rightMargin: Theme.spacingSm
                            anchors.verticalCenter: parent.verticalCenter
                            buttonSize: 22
                            iconSize: 14
                            glyph: Theme.icons.video
                            tooltip: qsTr("Select this camera and look through it")
                            onClicked: {
                                EditorState.selectClip(row.modelData.track, row.modelData.clip)
                                if (!EditorState.preview.lookThrough)
                                    EditorState.preview.toggleLookThrough()
                            }
                        }
                    }
                }
            }

            CameraInspector {
                width: parent.width
            }
        }
    }
}
