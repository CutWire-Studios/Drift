import QtQuick
import Drift

// Drops of every asset kind except media (which keeps its asset-index path) onto a timeline panel.
// Both the desktop and the phone panel own one, and both describe a drop the same way — a kind, a
// payload, and a point in content x / track-column y — so the two cannot drift apart: what a kind
// does where it lands is decided once, in AppController::planAssetDrop.
QtObject {
    id: router

    // TimelinePanel or AndroidTimeline. Reads its track geometry and drives its landing preview.
    required property var panel

    readonly property var placeableClipType: ({
        "shape": "shape", "sticker": "image", "emoji": "image",
        "textStyle": "text", "adjustment": "adjustment"
    })

    // Placeable kinds pick a track the way a media drop does: a row that takes the kind, or the
    // band at a row's edge (or any row that cannot take it) for a new track there. Everything
    // else acts on whatever is under the point.
    function targetAt(kind, columnY) {
        if (!EditorState.isPlaceableDropKind(kind))
            return { "track": panel.trackIndexAtY(columnY), "newTrackIndex": -1 }

        const tracks = panel.tracks
        if (tracks.length === 0)
            return { "track": -1, "newTrackIndex": 0 }
        var cursor = 0
        for (var i = 0; i < tracks.length; i++) {
            if (tracks[i].isAdjustmentLane)
                continue
            const h = panel.trackHeight(i)
            const rowEnd = cursor + h
            if (columnY < rowEnd + Theme.trackGap / 2) {
                if (!EditorState.trackAcceptsDropKind(i, kind))
                    return { "track": -1, "newTrackIndex": columnY < cursor + h / 2 ? i : i + 1 }
                const edge = Math.min(Theme.newTrackHitSlop, h / 4)
                if (i === 0 && columnY < cursor + edge)
                    return { "track": -1, "newTrackIndex": 0 }
                if (columnY >= rowEnd - edge)
                    return { "track": -1, "newTrackIndex": i + 1 }
                return { "track": i, "newTrackIndex": -1 }
            }
            cursor = rowEnd + Theme.trackGap
        }
        return { "track": -1, "newTrackIndex": tracks.length }
    }

    function planAt(kind, payload, contentX, columnY) {
        const target = targetAt(kind, columnY)
        const seconds = Math.max(0, contentX / panel.pxPerSecond)
        return EditorState.planAssetDrop(kind, String(payload), target.track, seconds,
                                         target.newTrackIndex)
    }

    // Shows where the drop would land and returns whether it would be taken at all.
    function hover(kind, payload, contentX, columnY) {
        const plan = planAt(kind, payload, contentX, columnY)
        if (!plan.accepted) {
            clear()
            return false
        }
        if (plan.mode === "clip" || plan.mode === "junction") {
            panel.clearLandingPreview()
            panel.effectDropTrackIndex = plan.track
            panel.effectDropClipIndex = plan.clip
        } else if (plan.mode === "gap") {
            panel.clearEffectDropHighlight()
            panel.dropCreatesNewTrack = false
            panel.showLandingPreview(plan.track, plan.landingStart, plan.landingDuration)
        } else if (plan.mode === "newTrack") {
            panel.clearEffectDropHighlight()
            panel.pendingDropKind = placeableClipType[kind] || ""
            panel.dropCreatesNewTrack = true
            panel.dropNewTrackIndex = plan.newTrackIndex
            panel.dropTrackIndex = -1
            panel.dropStartSeconds = plan.landingStart
            panel.dropDurationSeconds = plan.landingDuration
            panel.snapGuideSeconds = -1
        }
        return true
    }

    function drop(kind, payload, label, contentX, columnY) {
        const target = targetAt(kind, columnY)
        const seconds = Math.max(0, contentX / panel.pxPerSecond)
        clear()
        const plan = EditorState.dropAsset(kind, String(payload), label || "", target.track,
                                           seconds, target.newTrackIndex)
        if (!plan.accepted && plan.message)
            Toasts.info(plan.message)
        return plan.accepted
    }

    function clear() {
        panel.clearLandingPreview()
        panel.clearEffectDropHighlight()
        panel.pendingDropKind = ""
    }
}
