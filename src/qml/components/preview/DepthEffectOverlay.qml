import QtQuick
import Drift

// Handles for the depth effects on the selected clip: a knob per enabled 3D Relight light (plus an
// aim ring when it is a spot), and a reticle for Depth of Field's focus point. The desktop preview's
// transform companion, laid over the canvas rect.
//
// Like MaskOverlay, everything hangs off `clipFrame`: effect coordinates are 0..1 across the
// clip's own frame, so the handles have to travel with the clip's transform.
//
// Only the handles take the pointer. Everything else falls through to the transform gizmo below,
// so the clip can still be moved while its lights are shown.
Item {
    id: root

    property var editorState: ({})
    readonly property bool hasFrame: !!editorState.hasFrame

    // True while a handle is being dragged: rebuilding the delegates mid-drag would destroy the
    // one holding the grab.
    property bool interacting: false

    readonly property real canvasW: Math.max(1, editorState.canvasWidth || 1)
    readonly property real canvasH: Math.max(1, editorState.canvasHeight || 1)
    readonly property real sx: root.width / canvasW
    readonly property real sy: root.height / canvasH

    // Current values per handle, read by the delegates by index. The Repeater's model only
    // changes when the set of handles does, so a value edit moves the knobs without recreating
    // them.
    property var handleValues: []
    property string handleSignature: ""

    function refreshOverlay() {
        // Hidden: onVisibleChanged catches up when shown.
        if (!visible || interacting || EditorState.playing)
            return
        const next = EditorState.preview.depthEffectEditorState()
        editorState = next
        const handles = handlesFor(next)
        const signature = handles.map(h => h.kind + ":" + h.effect + ":" + (h.light || 0)).join("|")
        handleValues = handles
        if (signature === handleSignature)
            return
        handleSignature = signature
        // Set imperatively, as MaskOverlay does: binding the model re-enters when tracksChanged
        // fires during delegate setup.
        handleRepeater.model = handles
    }

    function endInteraction() {
        EditorState.commitPreviewDrag()
        interacting = false
        Qt.callLater(refreshOverlay)
    }

    // One flat list of handles across every depth effect on the stack.
    function handlesFor(state) {
        const out = []
        for (const effect of state.effects || []) {
            const p = effect.params || {}
            if (effect.catalogId === "depth.relight") {
                for (let n = 1; n <= 4; n++) {
                    const k = "light" + n + "_"
                    if (!p[k + "enabled"])
                        continue
                    out.push({
                        kind: "light", effect: effect.index, light: n,
                        x: Number(p[k + "x"]), y: Number(p[k + "y"]), z: Number(p[k + "z"]),
                        color: p[k + "color"] || Theme.c.depthEffectOverlay.light,
                        spot: Number(p[k + "cone"]) < 179.5,
                        aimX: Number(p[k + "aimX"]), aimY: Number(p[k + "aimY"])
                    })
                }
            } else if (effect.catalogId === "depth.focus") {
                out.push({
                    kind: "focus", effect: effect.index,
                    x: Number(p.focusX), y: Number(p.focusY), follow: !!p.autoFocus
                })
            }
        }
        return out
    }

    // Through the keyframe path the inspector's sliders use: a static value when the parameter is
    // not animated, a key at the playhead when it is (or Auto keyframes is on).
    function writeParam(effectIndex, key, value) {
        EditorState.previewSetClipKeyframe(EditorState.selectedTrack, EditorState.selectedClip,
                                           "fx." + effectIndex + "." + key,
                                           EditorState.playheadSeconds, value)
    }

    // The pointer position in clip-frame units, through the frame's rotation.
    function framePoint(item, mouse) {
        const p = item.mapToItem(clipFrame, mouse.x, mouse.y)
        return { x: p.x / Math.max(1, clipFrame.width), y: p.y / Math.max(1, clipFrame.height) }
    }

    onVisibleChanged: if (visible) refreshOverlay()
    Component.onCompleted: refreshOverlay()

    Connections {
        target: EditorState
        function onTracksChanged() { root.refreshOverlay() }
        // A slider in the inspector dragging a light, or the clip being moved under them.
        function onClipPropertiesPreviewed(trackIndex, clipIndex, keys) {
            for (const k of keys) {
                if (k.startsWith("fx.") || k === "x" || k === "y" || k === "width"
                        || k === "height" || k === "rotation" || k === "rotationX"
                        || k === "rotationY" || k === "z" || k === "perspective") {
                    root.refreshOverlay()
                    return
                }
            }
        }
        function onSelectionChanged() { root.refreshOverlay() }
        function onSelectedClipDataChanged() { root.refreshOverlay() }
        // A scrub moves the playhead per scroll event; catch up once when it ends.
        function onPlayheadSecondsChanged() {
            if (!EditorState.scrubbing)
                root.refreshOverlay()
        }
        function onScrubbingChanged() {
            if (!EditorState.scrubbing)
                root.refreshOverlay()
        }
        function onPlayingChanged() {
            if (!EditorState.playing)
                root.refreshOverlay()
        }
    }

    Item {
        id: clipFrame
        visible: root.hasFrame
        x: (root.editorState.x || 0) * root.sx
        y: (root.editorState.y || 0) * root.sy
        width: Math.max(1, (root.editorState.width || root.canvasW) * root.sx)
        height: Math.max(1, (root.editorState.height || root.canvasH) * root.sy)
        transformOrigin: Item.Center
        // A tilted clip frame goes through the clip's perspective instead of the flat spin.
        readonly property bool is3d: (root.editorState.rotationX || 0) !== 0
                                     || (root.editorState.rotationY || 0) !== 0
                                     || (root.editorState.z || 0) !== 0
                                     || root.editorState.anchorX !== undefined || root.editorState.anchorY !== undefined
        rotation: is3d ? 0 : (root.editorState.rotation || 0)
        transform: Matrix4x4 {
            matrix: clipFrame.is3d
                    ? EditorState.preview.clipPoseMatrix(root.editorState, root.editorState.x || 0,
                                                        root.editorState.y || 0,
                                                        root.editorState.width || root.canvasW,
                                                        root.editorState.height || root.canvasH,
                                                        root.editorState.rotation || 0,
                                                        root.sx, root.sy)
                    : Qt.matrix4x4()
        }

        Repeater {
            id: handleRepeater

            delegate: Item {
                id: handle
                required property int index
                required property var modelData
                // Values from the latest refresh; modelData only fixes which handle this is.
                readonly property var values: root.handleValues[index] || modelData

                readonly property bool isLight: modelData.kind === "light"
                readonly property string prefix: "light" + modelData.light + "_"

                // Live values during a drag, so the handle follows the cursor without waiting
                // for the (deliberately frozen) model.
                property real liveX: NaN
                property real liveY: NaN
                property real liveZ: NaN
                property real liveAimX: NaN
                property real liveAimY: NaN
                readonly property real hx: isNaN(liveX) ? values.x : liveX
                readonly property real hy: isNaN(liveY) ? values.y : liveY
                readonly property real hz: isNaN(liveZ) ? (values.z || 0) : liveZ
                readonly property real ax: isNaN(liveAimX) ? (values.aimX || 0.5) : liveAimX
                readonly property real ay: isNaN(liveAimY) ? (values.aimY || 0.5) : liveAimY

                // Nearer lights draw larger, so depth reads at a glance.
                readonly property real knobSize: 26 - hz * 8

                function clearLive() {
                    liveX = NaN
                    liveY = NaN
                    liveZ = NaN
                    liveAimX = NaN
                    liveAimY = NaN
                }

                width: clipFrame.width
                height: clipFrame.height

                DepthEffectAimLine {
                    source: handle
                    frame: clipFrame
                }

                DepthEffectAimRing {
                    source: handle
                    frame: clipFrame
                    overlay: root
                }

                DepthEffectLightKnob {
                    source: handle
                    frame: clipFrame
                    overlay: root
                }

                DepthEffectFocusReticle {
                    source: handle
                    frame: clipFrame
                    overlay: root
                }
            }
        }
    }
}
