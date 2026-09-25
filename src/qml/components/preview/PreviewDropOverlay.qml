import QtQuick
import QtQuick.Window
import Drift

// The preview as a drop target. Sits exactly over the canvas rect, turns a point on it into canvas
// pixels, and shows what a drop there would do: a frame around the whole canvas for something that
// lands as a new overlay, or the outline of the clip an effect or mask would go onto. The decision
// itself is AppController::planPreviewDrop, the same one the drop makes.
Item {
    id: root

    property var plan: null
    readonly property bool showsCanvas: !!plan && plan.accepted && plan.mode === "canvas"
    readonly property bool showsClip: !!plan && plan.accepted && plan.mode === "clip"

    function toCanvas(localX, localY) {
        return Qt.point(localX / Math.max(1, width) * EditorState.projectWidth(),
                        localY / Math.max(1, height) * EditorState.projectHeight())
    }

    function containsLocal(localX, localY) {
        return localX >= 0 && localY >= 0 && localX <= width && localY <= height
    }

    function hover(kind, payload, localX, localY) {
        if (!containsLocal(localX, localY)) {
            clear()
            return false
        }
        const p = toCanvas(localX, localY)
        plan = EditorState.planPreviewDrop(kind, String(payload), p.x, p.y)
        return plan.accepted
    }

    function drop(kind, payload, label, localX, localY) {
        clear()
        if (!containsLocal(localX, localY))
            return false
        const p = toCanvas(localX, localY)
        const run = function() {
            const result = EditorState.dropAssetOnPreview(kind, String(payload), label || "", p.x, p.y)
            if (!result.accepted && result.message)
                Toasts.info(result.message)
        }
        // Media goes through the same import checks a timeline drop does.
        if (kind === "media" && Window.window && Window.window.configureAndAddAsset)
            Window.window.configureAndAddAsset(Number(payload), run)
        else
            run()
        return true
    }

    function clear() {
        plan = null
    }

    Rectangle {
        anchors.fill: parent
        visible: root.showsCanvas
        color: Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.08)
        border.width: 2
        border.color: Theme.primary
    }

    Rectangle {
        readonly property real sx: root.width / Math.max(1, EditorState.projectWidth())
        readonly property real sy: root.height / Math.max(1, EditorState.projectHeight())
        visible: root.showsClip
        x: root.showsClip ? root.plan.x * sx : 0
        y: root.showsClip ? root.plan.y * sy : 0
        width: root.showsClip ? root.plan.width * sx : 0
        height: root.showsClip ? root.plan.height * sy : 0
        rotation: root.showsClip ? root.plan.rotation : 0
        color: Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.12)
        border.width: 2
        border.color: Theme.primary
    }
}
