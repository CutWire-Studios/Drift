import QtQuick
import Drift

// The slip tool's drag on one track: the clip stays where it is and as long as it is, and the
// media slides under it. The drag is previewed (nothing in the project changes) and applied once
// on release, the way a trim is. The track item reports the pointer; this turns it into the
// live filmstrip, the in/out chip and the program monitor's new first and last frames.
Item {
    id: gesture

    property var panel
    property var renderer
    property int trackIndex: -1

    property bool active: false
    // The pointer has moved past the drag threshold; a press that never gets there is a click.
    property bool dragStarted: false
    property int clipIndex: -1
    property string clipId: ""
    property real start: 0
    property real duration: 0
    property real lastPx: NaN
    property real lastDelta: 0
    property var preview: null

    readonly property real fps: {
        void EditorState.tracksRevision
        const f = EditorState.projectFile.projectFps()
        return f > 0 ? f : 30
    }

    function tc(seconds) {
        const total = Math.round(Math.max(0, seconds) * gesture.fps)
        const f = total % Math.round(gesture.fps)
        const s = Math.floor(total / gesture.fps) % 60
        const m = Math.floor(total / (gesture.fps * 60)) % 60
        const h = Math.floor(total / (gesture.fps * 3600))
        function pad(n) { return n.toString().padStart(2, "0") }
        return pad(h) + ":" + pad(m) + ":" + pad(s) + ":" + pad(f)
    }

    function signedTc(seconds) {
        return (seconds < 0 ? "-" : "+") + tc(Math.abs(seconds))
    }

    function begin(index) {
        const info = renderer.clipInfo(index)
        clipIndex = index
        clipId = info.id
        start = info.start
        duration = info.duration
        Haptics.reset()
        lastPx = NaN
        lastDelta = 0
        preview = null
        dragStarted = false
        active = true
        ShellBridge.slipDragActive = true
    }

    // The clip's place now: the project can change under a drag (undo, an agent edit), so the
    // press-time indices are not trusted.
    function locate() {
        const at = EditorState.locateClip(clipId)
        return at.length === 2 ? at : null
    }

    function update(dxPx) {
        if (!active)
            return
        const px = Math.round(dxPx)
        if (px === lastPx)
            return
        const at = locate()
        if (!at) {
            cancel()
            return
        }
        if (!dragStarted) {
            dragStarted = true
            // Timeline playback shares the decoders the two frames come from. Only a real drag
            // stops it; a click leaves it running.
            if (EditorState.playing)
                EditorState.playing = false
        }
        lastPx = px
        lastDelta = EditorState.slipDeltaForDrag(at[0], at[1], dxPx / panel.pxPerSecond)
        const p = EditorState.previewSlipClip(at[0], at[1], lastDelta)
        preview = p
        if (!p.ok) {
            renderer.setLivePreview(clipId, false, 0, 0, 0, 0)
            panel.clearTrimFollow()
            ShellBridge.slipFrames = null
            return
        }
        renderer.setLivePreview(clipId, true, start, duration, p.inPoint, p.outPoint)
        if (p.follow)
            panel.setTrimFollow(p.linkId, clipId, start, duration, p.inPoint, p.outPoint, [], 0)
        ShellBridge.slipFrames = {
            first: p.firstFrameUrl,
            last: p.lastFrameUrl,
            inPoint: p.inPoint,
            outPoint: p.outPoint,
            firstAt: p.firstFrameSourceSeconds,
            lastAt: p.lastFrameSourceSeconds
        }
        Haptics.trimStep(p.limit !== 0 ? 3 : 1)
    }

    function cleanup() {
        renderer.setLivePreview(clipId, false, 0, 0, 0, 0)
        panel.clearTrimFollow()
        ShellBridge.slipFrames = null
        ShellBridge.slipDragActive = false
        active = false
        dragStarted = false
        preview = null
    }

    function finish(moved) {
        // A drag that was canceled (Escape, a tool key, the clip gone) has nothing to commit.
        if (!active)
            return
        const at = locate()
        const delta = lastDelta
        // The live preview goes first, so the geometry reads the project again when the edit lands.
        cleanup()
        // A real drag is committed even when the slip is refused (locked, no media, ...): the
        // release is where the reason is shown. A click (not moved) commits nothing.
        if (moved && at)
            EditorState.commitSlipClip(at[0], at[1], delta)
    }

    function cancel() {
        if (active)
            cleanup()
    }

    readonly property string readout: {
        const p = gesture.preview
        if (!p)
            return ""
        if (!p.ok)
            return p.reason === "locked" ? qsTr("Track is locked")
                   : p.reason === "no_media" ? qsTr("This clip has no media to slip")
                   : qsTr("This clip can't be slipped")
        let text = qsTr("In %1 · Out %2 (%3)").arg(tc(p.inPoint)).arg(tc(p.outPoint)).arg(signedTc(p.applied))
        if (p.looped)
            text += " · " + qsTr("Repeat starts at %1").arg(tc(p.loopOffset))
        if (p.limit < 0)
            text += " · " + qsTr("Start of the media")
        else if (p.limit > 0)
            text += " · " + qsTr("End of the media")
        return text
    }

    // A track torn down mid-drag (project closed, track removed) must not leave slipDragActive set.
    Component.onDestruction: cancel()

    // Escape or a tool key mid-drag drops the slip; the release that follows commits nothing.
    Connections {
        target: ShellBridge
        function onSlipCancelRequested() { gesture.cancel() }
    }

    Rectangle {
        visible: gesture.active && gesture.preview !== null
        x: gesture.start * gesture.panel.pxPerSecond + 4
        y: 2
        z: 30
        width: chipText.implicitWidth + Theme.spacingLg * 2
        height: chipText.implicitHeight + Theme.spacingSm * 2
        color: Theme.c.themedToolTip.background
        border.width: Theme.c.themedToolTip.borderWidth
        border.color: Theme.c.themedToolTip.border
        radius: Theme.c.themedToolTip.radius

        Text {
            id: chipText
            anchors.centerIn: parent
            text: gesture.readout
            color: Theme.c.themedToolTip.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }
}
