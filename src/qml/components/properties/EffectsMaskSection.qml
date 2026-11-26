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

    // Landmarks are baked onto the media clip; clipToMap reports the linked clip's state when the
    // selection is an adjustment pinned to it, which is the same answer the face section reads.
    readonly property bool canFaceTrack: {
        void clipDataRevision
        const data = EditorState.selectedClipData
        return data && data.canFaceTrack === true
    }
    readonly property bool hasFaceTrack: {
        void clipDataRevision
        const data = EditorState.selectedClipData
        return data && data.hasFaceTrack === true
    }
    readonly property bool faceReady: EditorState.faces.available() && Addons.runtimeAvailable()

    // Set while a scan this button started is running, so its result goes on to mask the faces.
    // A scan started from the face section must not pull masks in behind it.
    property bool maskAfterScan: false

    Connections {
        target: EditorState.faces
        function onFinished(ok, message) {
            if (!maskSection.maskAfterScan)
                return
            maskSection.maskAfterScan = false
            if (ok)
                maskSection.blurFaces()
        }
    }

    // The masks on their own would crop the clip to the faces. It is the mask-reading effect
    // already in this stack that turns them into the area it blurs, which is why this sits here
    // rather than in the face section.
    function blurFaces() {
        EditorState.faces.addFaceMasks(maskSection.target.track, maskSection.target.clip)
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
        ShellBridge.goDeeper({ panel: "masking", track: target.track, clip: target.clip, tool: tool })
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
        text: qsTr("Remove background")
        variant: "secondary"
        onClicked: maskSection.openMasking("ai")
    }

    // One click for the whole job: scan the clip when it has not been scanned, then lay a mask on
    // every face that follows it. Hidden on an adjustment layer, which covers whatever is beneath
    // it and so has no one clip whose faces could be traced.
    ThemedButton {
        visible: maskSection.canFaceTrack && maskSection.faceReady
                 && !EditorState.faces.detecting
        width: parent.width
        text: maskSection.hasFaceTrack ? qsTr("Blur faces") : qsTr("Find and blur faces")
        variant: "secondary"
        onClicked: {
            if (maskSection.hasFaceTrack) {
                maskSection.blurFaces()
                return
            }
            maskSection.maskAfterScan = true
            EditorState.faces.detect(maskSection.target.track, maskSection.target.clip)
        }
    }

    // Without this the button simply vanishes for the length of the scan.
    Text {
        width: parent.width
        wrapMode: Text.WordWrap
        visible: EditorState.faces.detecting
        text: EditorState.faces.status
        color: Theme.c.effectsMaskSection.warning
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }
}
