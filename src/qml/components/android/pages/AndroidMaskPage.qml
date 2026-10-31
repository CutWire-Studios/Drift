import QtQuick
import QtQuick.Controls.Basic
import Drift
// Picking a mask adds a Mask-kind adjustment to the clip the page was opened on; picking another
// removes it and adds the new one to the same clip.
AndroidAddPage {
    id: root

    // The clip being masked. clipId (the mask itself) is what the range bar and gizmo follow.
    property string targetId: ""
    readonly property var masks: EditorState.maskCatalog()

    Component.onCompleted: root.targetId = EditorState.clipAt(EditorState.selectedTrack,
                                                              EditorState.selectedClip).id || ""

    function applyMask(maskId) {
        if (root.targetId === "")
            return
        root.discard()
        root.select(root.targetId)
        EditorState.addMaskToClip(EditorState.selectedTrack, EditorState.selectedClip, maskId)
        root.adopt()
    }

    propertiesComponent: Component {
        MasksInspector { }
    }

    Flickable {
        id: scroll
        anchors.fill: parent
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
                    onApplyRequested: (maskId) => root.applyMask(maskId)
                }
            }
        }
    }
}
