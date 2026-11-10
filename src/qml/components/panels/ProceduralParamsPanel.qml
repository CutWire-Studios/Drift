import QtQuick
import QtQuick.Controls.Basic
import Drift

// The selected procedural clip's package, seed and parameters: the same controls as the Properties
// panel's Procedural tab.
PanelFrame {
    id: root
    SearchAnchor {
        searchId: "procedural-params-panel"
        title: qsTr("Procedural parameters panel")
        keywords: "procedural params seed package keyframe"
    }

    property bool shown: true

    readonly property var clipData: EditorState.selectedClipData
    readonly property bool hasProcedural: !!clipData && clipData.kind === "procedural" && !!clipData.procedural

    Flickable {
        id: flick
        anchors.fill: parent
        visible: root.hasProcedural
        clip: true
        contentWidth: width
        contentHeight: inspector.height + Theme.spacingLg * 2
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar { }

        ProceduralInspector {
            id: inspector
            x: Theme.spacingLg
            y: Theme.spacingLg
            width: flick.width - Theme.spacingLg * 2
        }
    }

    EmptyState {
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.spacing3xl, 280)
        visible: !root.hasProcedural
        compact: true
        title: qsTr("Select a procedural clip")
        hint: qsTr("Pick a Procedural 3D clip on the timeline to edit its parameters here.")
    }
}
