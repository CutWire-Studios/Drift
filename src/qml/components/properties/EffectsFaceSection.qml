import QtQuick
import QtQuick.Window
import Drift

Column {
    property int clipDataRevision: 0
    id: faceSection
    // Present whenever a face effect is, so re-detecting and clearing stay reachable;
    // it is the warnings below that appear only when the track is missing or too old.
    visible: faceSection.usesFaceEffect
    width: parent.width
    spacing: Theme.spacingSm

    // The model is an addon, but it can equally come from a bundled models/face or
    // DRIFT_FACE_MODEL_DIR, so ask the engine rather than the addon registry. That answer
    // is not a binding, hence the reset below when an addon of this kind appears.
    // Folded together because every control below is gated on the same answer;
    // runtimeReady is kept apart only to say which half is missing.
    property bool runtimeReady: Addons.runtimeAvailable()
    property bool faceReady: EditorState.faces.available()
                             && Addons.runtimeAvailable()

    // Landmarks are baked onto the media clip, and the selection here is the adjustment
    // pinned to it — clipToMap reports the linked clip's state for exactly this.
    property bool canTrack: {
        void clipDataRevision
        const data = EditorState.selectedClipData
        return data && data.canFaceTrack === true
    }
    property bool hasTrack: {
        void clipDataRevision
        const data = EditorState.selectedClipData
        return data && data.hasFaceTrack === true
    }
    // A track baked before contours existed still drives the warps, so it is not stale in
    // general — only the Beauty effects have nothing to work with, and they pass through.
    property bool trackHasContours: {
        void clipDataRevision
        const data = EditorState.selectedClipData
        return data && data.faceTrackHasContours === true
    }
    // Same idea as contours: a pre-mesh track still drives warps and makeup, but the 3D
    // Face Mesh effect has nothing to warp until the clip is scanned again.
    property bool trackHasMesh: {
        void clipDataRevision
        const data = EditorState.selectedClipData
        return data && data.faceTrackHasMesh === true
    }

    readonly property var beautyIds: ["face_retouch", "face_teeth_whiten", "face_eyeliner",
                                      "face_eyeshadow", "face_brow_tint", "face_eye_color"]
    // Effects that need the 468-vertex mesh, not just the contours.
    readonly property var meshIds: ["face_mesh_3d", "face_retouch"]

    // One pass over the stack for all three answers: whether anything here needs a track
    // at all, and whether what needs it needs a *newer* one.
    readonly property var faceUse: {
        void clipDataRevision
        const effects = EditorState.selectedClipEffects || []
        let any = false
        let beauty = false
        let mesh = false
        for (let i = 0; i < effects.length; i++) {
            const id = effects[i].catalogId || ""
            if (id.indexOf("face_") !== 0)
                continue
            any = true
            if (faceSection.beautyIds.indexOf(id) >= 0)
                beauty = true
            if (faceSection.meshIds.indexOf(id) >= 0)
                mesh = true
        }
        return { any: any, beauty: beauty, mesh: mesh }
    }
    readonly property bool usesFaceEffect: faceUse.any

    Connections {
        target: Addons
        function onKindChanged(kind) {
            if (kind !== "face-model" && kind !== "onnxruntime")
                return
            faceSection.runtimeReady = Addons.runtimeAvailable()
            faceSection.faceReady = EditorState.faces.available()
                                    && faceSection.runtimeReady
        }
    }

    Text {
        width: parent.width
        text: qsTr("Face tracking")
        SearchAnchor {
            searchId: "effects-face-tracking"
            title: qsTr("Face tracking")
            keywords: "face"
        }
        color: Theme.c.effectsFaceSection.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    // A standalone adjustment layer covers everything below it, so there is no one clip
    // whose faces could be traced. The effect is inert here and no scan would fix it.
    Text {
        width: parent.width
        wrapMode: Text.WordWrap
        visible: !faceSection.canTrack
        text: qsTr("Face effects follow one clip's faces. Add this to a clip rather than to an adjustment layer.")
        color: Theme.c.effectsFaceSection.warning
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    // The effect is in the stack and doing nothing. Said as a warning, not a hint: the
    // preview looks untouched and there is no other clue why.
    Text {
        width: parent.width
        wrapMode: Text.WordWrap
        visible: faceSection.canTrack && faceSection.faceReady && !faceSection.hasTrack
                 && !EditorState.faces.detecting
        text: qsTr("These effects follow a face, so the clip has to be scanned before they do anything.")
        color: Theme.c.effectsFaceSection.warning
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    // The Beauty effects need the lip and eyelid contours, which tracks baked by older
    // builds do not carry. They pass the frame through untouched in that case, so without
    // this the effect reads as broken rather than as needing one more scan.
    Text {
        width: parent.width
        wrapMode: Text.WordWrap
        visible: faceSection.canTrack && faceSection.faceReady && faceSection.hasTrack
                 && !faceSection.trackHasContours && faceSection.faceUse.beauty
                 && !EditorState.faces.detecting
        text: qsTr("This clip was scanned before makeup was supported. Re-detect faces to enable the Beauty effects.")
        color: Theme.c.effectsFaceSection.warning
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    // 3D Face Mesh and Face Retouch need the 468-vertex blob, which tracks baked by older
    // builds do not carry. They pass through in that case, so without this the effect
    // reads as broken rather than as needing one more scan.
    Text {
        width: parent.width
        wrapMode: Text.WordWrap
        visible: faceSection.canTrack && faceSection.faceReady && faceSection.hasTrack
                 && !faceSection.trackHasMesh && faceSection.faceUse.mesh
                 && !EditorState.faces.detecting
        text: qsTr("This clip was scanned before the face mesh was supported. Re-detect faces to enable 3D Face Mesh and Face Retouch.")
        color: Theme.c.effectsFaceSection.warning
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    ThemedButton {
        visible: faceSection.canTrack && faceSection.faceReady && !EditorState.faces.detecting
        width: parent.width
        text: faceSection.hasTrack ? qsTr("Re-detect faces") : qsTr("Scan for faces…")
        variant: faceSection.hasTrack ? "ghost" : "secondary"
        onClicked: EditorState.faces.detect(
                       EditorState.selectedTrack, EditorState.selectedClip)
    }

    ThemedButton {
        visible: faceSection.canTrack && faceSection.faceReady && faceSection.hasTrack
                 && !EditorState.faces.detecting
        width: parent.width
        text: qsTr("Clear face track")
        variant: "ghost"
        onClicked: EditorState.faces.clearTrack(
                       EditorState.selectedTrack, EditorState.selectedClip)
    }

    Text {
        width: parent.width
        wrapMode: Text.WordWrap
        visible: EditorState.faces.detecting
        text: EditorState.faces.status
        color: Theme.c.effectsFaceSection.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    ThemedProgressBar {
        visible: EditorState.faces.detecting
        width: parent.width
        value: EditorState.faces.progress
    }

    ThemedButton {
        visible: EditorState.faces.detecting
        width: parent.width
        text: qsTr("Cancel")
        variant: "ghost"
        onClicked: EditorState.faces.cancel()
    }

    ThemedButton {
        visible: faceSection.canTrack && !faceSection.faceReady
        width: parent.width
        text: faceSection.runtimeReady
              ? qsTr("Download face detection (about 5 MB)")
              : qsTr("Install AI engine first")
        variant: "primary"
        onClicked: ShellBridge.main.openAddonManager(
            faceSection.runtimeReady ? "face-model" : "onnxruntime")
    }
}
