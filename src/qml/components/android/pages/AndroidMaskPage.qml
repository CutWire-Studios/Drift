import QtQuick
import QtQuick.Controls.Basic
import Drift
// Picking a mask adds a Mask-kind adjustment to the clip the page was opened on; picking another
// removes it and adds the new one to the same clip. The tool row draws one on the preview instead:
// a shape, a polygon, a brush or an AI pick, or tracks a point or surface on the clip; what a tool creates counts as the page's mask, so it
// is replaced and cancelled like a picked one. Edit hands the preview back to the mask's handles.
AndroidAddPage {
    id: root

    // The clip being masked. clipId (the mask itself) is what the range bar and gizmo follow.
    property string targetId: ""
    property string pickedMask: ""
    readonly property var masks: EditorState.maskCatalog()

    Component.onCompleted: root.targetId = EditorState.clipAt(EditorState.selectedTrack,
                                                              EditorState.selectedClip).id || ""

    readonly property var tools: [
        { id: "rect", label: qsTr("Rectangle"), draw: true },
        { id: "ellipse", label: qsTr("Ellipse"), draw: true },
        { id: "polygon", label: qsTr("Polygon"), draw: true },
        { id: "brush", label: qsTr("Brush"), draw: false },
        { id: "ai", label: qsTr("AI select"), draw: false },
        { id: "tracker", label: qsTr("Track"), draw: false },
        { id: "", label: qsTr("Edit"), draw: true }
    ]
    readonly property bool aiRuntimeReady: Addons.runtimeAvailable()

    function useTool(toolId, draw) {
        if (toolId !== "" && root.targetId === "")
            return
        if (!draw)
            root.select(root.targetId)
        EditorState.preview.maskingTool = toolId
    }

    // The mask a tool just made is the new selection; take it over from whatever the page held.
    function adoptToolMask() {
        const sel = EditorState.clipAt(EditorState.selectedTrack, EditorState.selectedClip)
        if (!sel.id || sel.id === root.targetId || sel.id === root.clipId)
            return
        if (String(sel.adjustmentKind || "").toLowerCase() !== "mask")
            return
        root.adopt()
    }

    Component.onDestruction: EditorState.preview.maskingTool = ""

    Connections {
        target: EditorState
        function onSelectionChanged() { Qt.callLater(root.adoptToolMask) }
    }

    function applyMask(maskId) {
        if (root.targetId === "")
            return
        EditorState.preview.maskingTool = ""
        root.discard()
        root.select(root.targetId)
        EditorState.addMaskToClip(EditorState.selectedTrack, EditorState.selectedClip, maskId)
        root.adopt()
        root.pickedMask = maskId
    }

    propertiesComponent: Component {
        MasksInspector { }
    }

    Flow {
        id: toolRow
        x: Theme.pagePadding
        y: Theme.spacingSm
        width: parent.width - Theme.pagePadding * 2
        spacing: Theme.spacingSm

        Repeater {
            model: root.tools

            ThemedChip {
                required property var modelData
                readonly property bool unavailable: modelData.id === "ai" && !root.aiRuntimeReady
                chipHeight: Theme.androidMinTouchTarget
                text: modelData.label
                variant: "outline"
                enabled: !unavailable && (modelData.id === "" || root.targetId !== "")
                selected: EditorState.preview.maskingTool === modelData.id
                tooltip: modelData.label
                tooltipState: unavailable ? qsTr("Needs AI Engine") : ""
                onClicked: root.useTool(modelData.id, modelData.draw)
            }
        }
    }

    Flickable {
        id: scroll
        anchors.top: toolRow.bottom
        anchors.topMargin: Theme.spacingSm
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        contentHeight: maskGrid.height + Theme.pagePadding * 2
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar { }

        Grid {
            id: maskGrid
            x: Theme.pagePadding
            y: Theme.pagePadding
            width: scroll.width - Theme.pagePadding * 2
            columns: Math.max(1, Math.round((width + Theme.assetCardGap) / (Theme.assetCardWidth + Theme.assetCardGap)))
            readonly property real cardSize: Math.floor((width + Theme.assetCardGap) / columns) - Theme.assetCardGap
            columnSpacing: Theme.assetCardGap
            rowSpacing: Theme.assetCardGap

            Repeater {
                model: root.masks
                delegate: MaskPresetCard {
                    cardSize: maskGrid.cardSize
                    hasVisualSelection: root.targetId !== ""
                    pickerMode: true
                    picked: root.pickedMask === modelData.id
                    onApplyRequested: (maskId) => root.applyMask(maskId)
                }
            }
        }
    }
}
