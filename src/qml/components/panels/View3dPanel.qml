pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Drift
// How the 3D viewport looks and behaves (grid, snapping, lens, navigation), and the lights of the
// selected model. These are editor preferences, kept across projects.
PanelFrame {
    id: root
    SearchAnchor {
        searchId: "view3d-panel"
        title: qsTr("View & Lights")
        keywords: "3d viewport grid snap navigation lights"
    }

    property bool shown: false

    readonly property var clipData: {
        void EditorState.selectionRevision
        void EditorState.tracksRevision
        return EditorState.selectedClipData
    }
    readonly property bool hasLightable: !!clipData && clipData.kind === "model3d"
                                         && !!clipData.model3d && !clipData.model3d.isSplat

    component Heading: Text {
        color: Theme.c.view3dPanel.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
        font.weight: Font.Medium
    }

    AssetsPanelHeader {
        id: header
        width: parent.width
        height: Theme.panelHeaderHeight
        title: qsTr("View & Lights")
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

            Heading {
                text: qsTr("Grid")
                SearchAnchor {
                    searchId: "view3d-grid"
                    title: qsTr("3D grid")
                    keywords: "floor ground plane show hide size spacing"
                }
            }
            ThemedSwitch {
                text: qsTr("Show grid")
                tooltip: qsTr("The floor grid drawn under the scene in the 3D viewport")
                Binding on checked { value: EditorState.preview.showGrid }
                onToggled: EditorState.preview.showGrid = checked
            }
            Column {
                width: parent.width
                spacing: 4
                enabled: EditorState.preview.showGrid
                Text {
                    text: qsTr("Grid size")
                    color: Theme.c.view3dPanel.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
                ThemedNumberField {
                    id: gridField
                    width: parent.width
                    from: 0
                    to: 100000
                    step: 10
                    unit: "px"
                    Binding on value { when: !gridField.activeFocus; value: EditorState.preview.gridSize }
                    onEdited: v => EditorState.preview.gridSize = v
                }
                Text {
                    width: parent.width
                    text: qsTr("Spacing between lines. 0 picks one from the canvas size.")
                    color: Theme.c.view3dPanel.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    wrapMode: Text.WordWrap
                }
            }

            Heading {
                text: qsTr("Snapping")
                SearchAnchor {
                    searchId: "view3d-snap"
                    title: qsTr("3D snapping")
                    keywords: "snap step translate rotate rotation increment"
                }
            }
            Row {
                width: parent.width
                spacing: Theme.spacingMd

                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 4
                    Text {
                        text: qsTr("Move step")
                        color: Theme.c.view3dPanel.foregroundMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                    }
                    ThemedNumberField {
                        id: moveSnapField
                        width: parent.width
                        from: 0
                        to: 10000
                        step: 10
                        unit: "px"
                        Binding on value { when: !moveSnapField.activeFocus; value: EditorState.preview.translateSnap }
                        onEdited: v => EditorState.preview.translateSnap = v
                    }
                }
                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 4
                    Text {
                        text: qsTr("Rotation step")
                        color: Theme.c.view3dPanel.foregroundMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                    }
                    ThemedNumberField {
                        id: rotateSnapField
                        width: parent.width
                        from: 0
                        to: 180
                        step: 5
                        decimals: 1
                        unit: "°"
                        Binding on value { when: !rotateSnapField.activeFocus; value: EditorState.preview.rotationSnap }
                        onEdited: v => EditorState.preview.rotationSnap = v
                    }
                }
            }
            Text {
                width: parent.width
                text: qsTr("Gizmo drags move and turn in these steps; hold Ctrl for free movement. 0 turns a step off.")
                color: Theme.c.view3dPanel.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                wrapMode: Text.WordWrap
            }

            Heading {
                text: qsTr("Navigation")
                SearchAnchor {
                    searchId: "view3d-navigation"
                    title: qsTr("3D navigation")
                    keywords: "blender maya orbit pan dolly field of view fov orthographic lens"
                }
            }
            Column {
                width: parent.width
                spacing: 4
                Text {
                    text: qsTr("Editor field of view")
                    color: Theme.c.view3dPanel.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
                ThemedSlider {
                    id: fovSlider
                    width: parent.width
                    label: qsTr("Editor field of view")
                    from: 10
                    to: 120
                    stepSize: 1
                    valueFormatter: function (v) { return Math.round(v) + "°" }
                    Binding on value { when: !fovSlider.pressed; value: EditorState.preview.editorFov }
                    onMoved: EditorState.preview.editorFov = value
                }
            }
            Column {
                width: parent.width
                spacing: 4
                Text {
                    text: qsTr("Navigation scheme")
                    color: Theme.c.view3dPanel.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
                ThemedComboBox {
                    readonly property var ids: ["blender", "maya"]
                    width: parent.width
                    model: [qsTr("Blender"), qsTr("Maya")]
                    currentIndex: Math.max(0, ids.indexOf(EditorState.preview.navigationScheme))
                    tooltip: qsTr("Blender: middle-drag orbits, Shift pans, Ctrl dollies. Maya: Alt with the left, middle or right button orbits, pans or dollies.")
                    onActivated: EditorState.preview.navigationScheme = ids[currentIndex]
                }
            }
            ThemedSwitch {
                text: qsTr("Orbit around selection")
                tooltip: qsTr("Orbit about the selected clip's centre instead of the view's target")
                Binding on checked { value: EditorState.preview.orbitAroundSelection }
                onToggled: EditorState.preview.orbitAroundSelection = checked
            }
            ThemedSwitch {
                text: qsTr("Orthographic")
                tooltip: qsTr("Draw the viewport without perspective (Numpad 5)")
                Binding on checked { value: EditorState.preview.orthographic }
                onToggled: EditorState.preview.orthographic = checked
            }

            Heading {
                text: qsTr("Lights")
                SearchAnchor {
                    searchId: "view3d-lights"
                    title: qsTr("3D model lights")
                    keywords: "light ambient spot brightness shading"
                }
            }
            Text {
                width: parent.width
                visible: !root.hasLightable
                text: qsTr("Select a 3D model to edit its lights.")
                color: Theme.c.view3dPanel.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                wrapMode: Text.WordWrap
            }
            Model3DLighting {
                width: parent.width
                visible: root.hasLightable
            }
        }
    }
}
