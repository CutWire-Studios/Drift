import QtQuick
import Drift

// Filmstrip across a video clip body.
//
// Only tiles that intersect the timeline viewport are instantiated. A multi-hour
// clip at default zoom is hundreds of thousands of px wide; creating one Image
// per 120px (or even one huge Image per strip frame) freezes or OOMs the UI,
// especially once play starts auto-scrolling the Flickable.
Item {
    id: root

    property string filmstripPath: ""
    property int frameCount: 8
    property int frameWidth: 120
    property int frameHeight: 68

    // Media file behind the strip. Set for video clips only; when present each tile also
    // requests its real frame on demand and fades it in over the coarse strip frame.
    property string sourcePath: ""
    // The clip's lossless orientation fix (Clip::rotationCorrection), so on-demand tiles come
    // out the way the preview shows the clip.
    property int rotationCorrection: 0

    // Source window this clip covers, in seconds, plus the full source length. When set, each
    // tile maps to the source time it represents so the strip shows correct-timestamp frames and
    // stays anchored to the source when the clip is trimmed. Left at 0 (e.g. the Speed Curve
    // window) falls back to one plain pass of the strip's frames across the width.
    property real inPoint: 0
    property real outPoint: 0
    property real sourceDuration: 0

    // Timeline viewport in the same coordinate space as this item's parent (clip x).
    // When unset (viewW <= 0), fall back to a small tile cap so Speed Curve etc. stay cheap.
    property real worldX: 0
    property real viewX: 0
    property real viewW: 0

    // Bumped by the timeline when a pinch-zoom settles so tile Images rebind after the
    // gesture stops dirtying the scene graph (Android was leaving them blank once idle).
    // Desktop never bumps it, so the bindings below are inert there.
    property int refreshEpoch: 0

    readonly property bool sourceMapped: sourceDuration > 0 && outPoint > inPoint && width > 0

    // Tiles are laid out on a virtual grid spanning the whole source, not the clip body, so
    // trimming the head slides the grid left by exactly as much as the clip's left edge moves
    // right — the frames stay put on the timeline instead of riding along with the cursor.
    readonly property real pxPerSourceSec: sourceMapped ? width / (outPoint - inPoint) : 0
    readonly property real stripOriginX: sourceMapped ? -inPoint * pxPerSourceSec : 0
    // Grid always reaches at least the clip's right edge, so a source shorter than the clip
    // (stale/missing asset duration) still tiles the whole body.
    readonly property real stripWidth: sourceMapped
        ? Math.max(sourceDuration * pxPerSourceSec, width - stripOriginX)
        : width

    readonly property int totalTiles: filmstripPath.length > 0 && width > 0
        ? Math.max(1, Math.ceil(stripWidth / Math.max(1, frameWidth)))
        : 0

    // Inclusive range of tile indices that overlap the viewport (+ 1 tile overscan), clamped to
    // the tiles that actually fall inside the clip body.
    readonly property int firstTileInBody: Math.max(
        0, Math.floor(-stripOriginX / Math.max(1, frameWidth)))
    readonly property int lastTileInBody: Math.min(
        totalTiles - 1, Math.floor((width - stripOriginX) / Math.max(1, frameWidth)))

    readonly property int firstVisibleTile: {
        if (totalTiles <= 0)
            return 0
        if (viewW <= 0)
            return Math.max(0, firstTileInBody)
        var localLeft = viewX - worldX - stripOriginX
        var tile = Math.floor(localLeft / Math.max(1, frameWidth)) - 1
        return Math.max(firstTileInBody, Math.min(tile, Math.max(0, lastTileInBody)))
    }

    readonly property int visibleTileCount: {
        if (totalTiles <= 0 || lastTileInBody < firstVisibleTile)
            return 0
        var span = lastTileInBody - firstVisibleTile + 1
        if (viewW <= 0) {
            // No viewport (Speed Curve): one pass of the strip frames across the body. Trimming
            // slides the grid off the tile boundary, so the pass needs one more tile to reach the
            // right edge — without it the body ends in a blank sliver. Assumes frameWidth is about
            // width/frameCount, which is what a viewport-less caller sets.
            return Math.min(span, Math.max(frameCount, 1) + 1)
        }
        // A clip entirely outside the viewport still clamps to a tile inside its own body, so
        // without this it would instantiate — and queue decodes for — a full screenful. With
        // several clips on the timeline that multiplies every pan by the clip count.
        var pad = frameWidth
        if (worldX + width < viewX - pad || worldX > viewX + viewW + pad)
            return 0
        var count = Math.ceil(viewW / Math.max(1, frameWidth)) + 3
        return Math.min(span, Math.max(1, count))
    }

    function frameForTile(tileIndex) {
        if (sourceMapped) {
            var srcSec = (tileIndex + 0.5) * frameWidth / pxPerSourceSec
            var f = Math.floor(srcSec / sourceDuration * frameCount)
            return Math.max(0, Math.min(frameCount - 1, f))
        }
        return tileIndex % Math.max(1, frameCount)
    }

    // On-demand tiles. The strip only holds `frameCount` frames for the whole source, so a long
    // clip repeats the same image for thousands of px. Each visible tile additionally asks for
    // the frame at its own timestamp, cached at a power-of-two second interval: the level is the
    // largest interval that still fits inside one tile, so zooming in picks a finer level and
    // panning at the same zoom reuses everything already decoded.
    readonly property bool tilesEnabled: sourcePath.length > 0 && sourceMapped && pxPerSourceSec > 0
    readonly property real secondsPerTile: pxPerSourceSec > 0 ? frameWidth / pxPerSourceSec : 0
    readonly property int tileLevel: secondsPerTile > 0
        ? Math.max(-3, Math.min(14, Math.floor(Math.log(secondsPerTile) / Math.LN2)))
        : 0
    // Bumped when a decode lands, to re-run the tile URL bindings.
    property int tileRevision: 0

    function tileUrlForTile(tileIndex) {
        void tileRevision
        void refreshEpoch
        if (!tilesEnabled)
            return ""
        var srcSec = (tileIndex + 0.5) * frameWidth / pxPerSourceSec
        if (srcSec < 0 || srcSec >= sourceDuration)
            return ""
        var interval = Math.pow(2, tileLevel)
        return EditorState.filmstripTileUrl(sourcePath, tileLevel, Math.floor(srcSec / interval),
                                            rotationCorrection)
    }

    function coarseUrlForTile(tileIndex) {
        void refreshEpoch
        return EditorState.filmstripFrameUrl(filmstripPath, frameForTile(tileIndex), frameCount)
    }

    Connections {
        target: EditorState
        function onFilmstripTileReady(path) {
            if (path === root.sourcePath)
                root.tileRevision++
        }
    }

    // True while the first frame is still decoding.
    readonly property bool loading: filmstripPath.length > 0 && !firstFrameReady
    property bool firstFrameReady: false

    clip: true

    onFilmstripPathChanged: firstFrameReady = false
    onRefreshEpochChanged: {
        // Re-request paint after pinch: clear the ready latch so a blank settle cannot
        // leave the strip empty without a skeleton, then bump tile bindings.
        firstFrameReady = false
        tileRevision++
    }

    SkeletonBox {
        anchors.fill: parent
        radius: 0
        animated: root.width < 4000
        visible: root.loading
    }

    Repeater {
        model: root.visibleTileCount
        delegate: Item {
            id: tile
            required property int index
            readonly property int tileIndex: root.firstVisibleTile + index

            x: root.stripOriginX + tileIndex * root.frameWidth
            width: root.frameWidth
            height: root.height

            // Coarse frame from the strip: already on disk, so it draws immediately.
            Image {
                id: frame
                anchors.fill: parent
                source: root.coarseUrlForTile(tile.tileIndex)
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                // Keep GPU textures small regardless of item layout.
                sourceSize.width: root.frameWidth
                sourceSize.height: root.frameHeight

                opacity: status === Image.Ready ? 1 : 0

                Behavior on opacity {
                    NumberAnimation { duration: Theme.durationBase; easing.type: Theme.easing }
                }

                onStatusChanged: {
                    if (tile.index !== 0)
                        return
                    if (status === Image.Ready || status === Image.Error)
                        root.firstFrameReady = true
                }
            }

            // This tile's own timestamp, decoded on demand. Sits on top so it replaces the
            // repeated strip frame without a reload flicker once it arrives.
            Image {
                anchors.fill: parent
                source: root.tileUrlForTile(tile.tileIndex)
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                sourceSize.width: root.frameWidth
                sourceSize.height: root.frameHeight

                opacity: status === Image.Ready ? 1 : 0

                Behavior on opacity {
                    NumberAnimation { duration: Theme.durationBase; easing.type: Theme.easing }
                }
            }
        }
    }
}
