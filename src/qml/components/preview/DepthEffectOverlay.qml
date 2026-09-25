import QtQuick
import Drift
import ".."

// Handles for the depth effects on the selected clip: a knob per enabled 3D Relight light (plus an
// aim ring when it is a spot), and a reticle for Depth of Field's focus point. Geometry is driven
// by the owning PreviewPanel, which mirrors the canvas rect here.
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
        const next = EditorState.depthEffectEditorState()
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
                        color: p[k + "color"] || "#ffffff",
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
                        || k === "height" || k === "rotation") {
                    root.refreshOverlay()
                    return
                }
            }
        }
        function onSelectionChanged() { root.refreshOverlay() }
        function onSelectedClipDataChanged() { root.refreshOverlay() }
        function onPlayheadSecondsChanged() { root.refreshOverlay() }
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
        rotation: root.editorState.rotation || 0

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

                // Spot direction: a line from the light to where it is aimed.
                Rectangle {
                    visible: handle.isLight && handle.values.spot
                    readonly property real dx: (handle.ax - handle.hx) * clipFrame.width
                    readonly property real dy: (handle.ay - handle.hy) * clipFrame.height
                    x: handle.hx * clipFrame.width
                    y: handle.hy * clipFrame.height - height / 2
                    width: Math.sqrt(dx * dx + dy * dy)
                    height: 1.5
                    color: handle.values.color || "#ffffff"
                    opacity: 0.7
                    transformOrigin: Item.Left
                    rotation: Math.atan2(dy, dx) * 180 / Math.PI
                }

                // Spot aim ring.
                Rectangle {
                    visible: handle.isLight && handle.values.spot
                    width: 16
                    height: 16
                    radius: 8
                    x: handle.ax * clipFrame.width - width / 2
                    y: handle.ay * clipFrame.height - height / 2
                    color: "transparent"
                    border.width: 2
                    border.color: aimArea.containsMouse || aimArea.pressed
                                  ? Theme.primary : (handle.values.color || "#ffffff")

                    MouseArea {
                        id: aimArea
                        anchors.fill: parent
                        anchors.margins: -Theme.spacingSm
                        hoverEnabled: true
                        cursorShape: Qt.CrossCursor
                        preventStealing: true

                        onPressed: {
                            root.interacting = true
                            EditorState.beginPreviewDrag(qsTr("Aim light"))
                        }
                        onPositionChanged: (mouse) => {
                            if (!pressed)
                                return
                            const p = root.framePoint(aimArea, mouse)
                            handle.liveAimX = Math.max(0, Math.min(1, p.x))
                            handle.liveAimY = Math.max(0, Math.min(1, p.y))
                            root.writeParam(handle.modelData.effect, handle.prefix + "aimX", handle.liveAimX)
                            root.writeParam(handle.modelData.effect, handle.prefix + "aimY", handle.liveAimY)
                        }
                        onReleased: { handle.clearLive(); root.endInteraction() }
                        onCanceled: { handle.clearLive(); root.endInteraction() }
                    }
                }

                // Light knob: drag to move across the frame, scroll to move nearer or farther.
                Rectangle {
                    visible: handle.isLight
                    width: handle.knobSize
                    height: handle.knobSize
                    radius: width / 2
                    x: handle.hx * clipFrame.width - width / 2
                    y: handle.hy * clipFrame.height - height / 2
                    color: handle.values.color || "#ffffff"
                    border.width: 2
                    border.color: lightArea.containsMouse || lightArea.pressed
                                  ? Theme.primary : Theme.primaryForeground

                    Text {
                        anchors.centerIn: parent
                        text: handle.modelData.light || ""
                        // Dark on the light's own colour: every default is a pale tint.
                        color: "#1a1a1a"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                        font.weight: Font.Medium
                    }

                    MouseArea {
                        id: lightArea
                        anchors.fill: parent
                        anchors.margins: -Theme.spacingSm
                        hoverEnabled: true
                        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                        preventStealing: true

                        onPressed: {
                            root.interacting = true
                            EditorState.beginPreviewDrag(qsTr("Move light"))
                        }
                        onPositionChanged: (mouse) => {
                            if (!pressed)
                                return
                            // Lights may sit off the frame, as far as the parameters reach.
                            const p = root.framePoint(lightArea, mouse)
                            handle.liveX = Math.max(-0.5, Math.min(1.5, p.x))
                            handle.liveY = Math.max(-0.5, Math.min(1.5, p.y))
                            root.writeParam(handle.modelData.effect, handle.prefix + "x", handle.liveX)
                            root.writeParam(handle.modelData.effect, handle.prefix + "y", handle.liveY)
                        }
                        onReleased: { handle.clearLive(); root.endInteraction() }
                        onCanceled: { handle.clearLive(); root.endInteraction() }

                        // Scrolling up brings the light towards the camera; Shift for fine steps.
                        onWheel: (wheel) => {
                            const step = (wheel.modifiers & Qt.ShiftModifier) !== 0 ? 0.01 : 0.05
                            const z = Math.max(-1, Math.min(1, handle.hz - Math.sign(wheel.angleDelta.y) * step))
                            EditorState.beginPreviewDrag(qsTr("Move light"))
                            root.writeParam(handle.modelData.effect, handle.prefix + "z", z)
                            EditorState.commitPreviewDrag()
                            wheel.accepted = true
                        }
                    }
                }

                // Focus reticle. With "Follow focus point" on, focus tracks whatever is under it
                // on every frame; otherwise dropping it here sets the focus distance to the depth
                // at this spot, once.
                Item {
                    visible: !handle.isLight
                    width: 34
                    height: 34
                    x: handle.hx * clipFrame.width - width / 2
                    y: handle.hy * clipFrame.height - height / 2

                    Rectangle {
                        anchors.fill: parent
                        radius: width / 2
                        color: "transparent"
                        border.width: 2
                        border.color: focusArea.containsMouse || focusArea.pressed
                                      ? Theme.primary : Theme.primaryForeground
                    }
                    Rectangle {
                        anchors.centerIn: parent
                        width: 2
                        height: parent.height * 0.5
                        color: Theme.primaryForeground
                    }
                    Rectangle {
                        anchors.centerIn: parent
                        width: parent.width * 0.5
                        height: 2
                        color: Theme.primaryForeground
                    }
                    Text {
                        anchors.top: parent.bottom
                        anchors.topMargin: 2
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: handle.values.follow ? qsTr("Focus") : qsTr("Pick focus")
                        color: Theme.primaryForeground
                        style: Text.Outline
                        styleColor: "#80000000"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                    }

                    MouseArea {
                        id: focusArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.CrossCursor
                        preventStealing: true

                        onPressed: {
                            root.interacting = true
                            EditorState.beginPreviewDrag(qsTr("Move focus"))
                        }
                        onPositionChanged: (mouse) => {
                            if (!pressed)
                                return
                            const p = root.framePoint(focusArea, mouse)
                            handle.liveX = Math.max(0, Math.min(1, p.x))
                            handle.liveY = Math.max(0, Math.min(1, p.y))
                            root.writeParam(handle.modelData.effect, "focusX", handle.liveX)
                            root.writeParam(handle.modelData.effect, "focusY", handle.liveY)
                            if (!handle.values.follow) {
                                const d = EditorState.sampleDepthAt(EditorState.selectedTrack,
                                                                    EditorState.selectedClip,
                                                                    handle.liveX, handle.liveY)
                                if (d >= 0)
                                    root.writeParam(handle.modelData.effect, "focusDepth", d)
                            }
                        }
                        onReleased: { handle.clearLive(); root.endInteraction() }
                        onCanceled: { handle.clearLive(); root.endInteraction() }
                    }
                }
            }
        }
    }
}
