import QtQuick
import Drift

Column {
    property int clipDataRevision: 0
    id: maskSection
    visible: maskSection.missingMask
    width: parent.width
    spacing: Theme.spacingSm

    readonly property bool usesMaskEffect: {
        void clipDataRevision
        const effects = EditorState.selectedClipEffects || []
        for (let i = 0; i < effects.length; i++) {
            if (effects[i].needsMask === true)
                return true
        }
        return false
    }
    readonly property var target: {
        void clipDataRevision
        return maskSection.usesMaskEffect ? EditorState.masking.maskTarget() : ({ kind: "none" })
    }
    readonly property bool missingMask: {
        void clipDataRevision
        if (!maskSection.usesMaskEffect || maskSection.target.kind !== "clip")
            return false
        const stack = EditorState.masking.maskStack(maskSection.target.track, maskSection.target.clip)
        return !stack.some(m => m.enabled !== false)
    }

    function openMasking(tool) {
        if (ShellBridge.phoneShell) {
            if (tool === "ai") {
                const data = EditorState.selectedClipData
                ShellBridge.main.openSegmentation(
                    target.track, target.clip,
                    data.start !== undefined ? data.start : 0,
                    data.duration !== undefined ? data.duration : 0)
            } else {
                EditorState.addMaskToClip(target.track, target.clip, "rectangle")
            }
            return
        }
        EditorState.selectClip(target.track, target.clip)
        Workspaces.activeId = "masking-tracking"
        Qt.callLater(() => EditorState.preview.maskingTool = tool)
    }

    Text {
        width: parent.width
        wrapMode: Text.WordWrap
        text: qsTr("This effect needs a mask.")
        color: Theme.c.effectsMaskSection.warning
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    ThemedButton {
        width: parent.width
        text: qsTr("Add new mask")
        variant: "secondary"
        onClicked: maskSection.openMasking("rect")
    }

    ThemedButton {
        width: parent.width
        text: qsTr("Cutout subject")
        variant: "secondary"
        onClicked: maskSection.openMasking("ai")
    }
}
