import QtQuick
import Drift

// Mask editor: direct manipulation of the selected clip's mask layers. Geometry is driven by
// PreviewToolHost, which mirrors the canvas rect here so grips on a mask that runs past a canvas
// edge stay drawn and grabbable.
//
// Mask coordinates are normalized to the *host clip's* frame, not the canvas: the coverage map is
// rasterized at the layer's size and sampled at the layer's UV, so a mask travels with the clip's
// transform. That is why everything below hangs off `clipFrame`, an Item placed and rotated to
// match the clip, rather than off this root.
Item {
    id: root

    // One resolved snapshot from AppController::maskEditorState(), so the host rect, the layer
    // list and which layer is selected can never be out of step with each other.
    property var editorState: ({})
    readonly property var layers: editorState.layers || []
    readonly property bool hasFrame: !!editorState.hasFrame

    // True while a grip or body drag is running. Refreshing mid-drag would destroy the delegate
    // that owns the active grab, exactly as in TransformOverlay.
    property bool interacting: false

    readonly property real canvasW: Math.max(1, editorState.canvasWidth || 1)
    readonly property real canvasH: Math.max(1, editorState.canvasHeight || 1)
    readonly property real sx: root.width / canvasW
    readonly property real sy: root.height / canvasH

    // Stickiness in normalized mask units. The tolerance is a screen distance so the pull feels
    // the same whatever the project resolution or preview zoom.
    readonly property real snapTolPx: 8

    function refreshOverlay() {
        // Hidden: onVisibleChanged catches up when shown.
        if (!visible || interacting)
            return
        // Playhead ticks ~60 Hz; rebuilding grips every tick during playback is wasted work.
        if (EditorState.playing)
            return
        const next = EditorState.preview.maskEditorState()
        editorState = next
        const layers = next.layers || []
        // Delegates read their values from here by index; the models below are only swapped
        // when the set of layers changes, so a value edit does not recreate the grips.
        layerValues = layers
        const signature = layers.map(l => l.track + ":" + l.clip + ":" + ((l.mask || {}).shape || ""))
                                .join("|")
        if (signature === layerSignature)
            return
        layerSignature = signature
        // Set imperatively for the same reason TransformOverlay does: binding the model
        // re-enters when tracksChanged fires during delegate setup.
        layerModel = layers
        maskRepeater.model = layers
    }

    property var layerValues: []
    property var layerModel: []
    property string layerSignature: ""

    function endInteraction() {
        EditorState.masking.editSession.commit()
        interacting = false
        Qt.callLater(refreshOverlay)
    }

    onVisibleChanged: if (visible) refreshOverlay()
    Component.onCompleted: refreshOverlay()

    Connections {
        target: EditorState
        function onTracksChanged() { root.refreshOverlay() }
        function onClipPropertiesPreviewed(trackIndex, clipIndex, keys) {
            for (const k of keys) {
                if (k.startsWith("mask.") || k === "x" || k === "y" || k === "width"
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

    // The clip's destination rect on the canvas, rotated with it. Mask boxes are children so QML
    // composes the two rotations instead of this file doing it by hand.
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
        // Moved by a transform layer: composed through that parent as well, flat or not.
        readonly property bool hasParent: root.editorState.parentActive === true
        rotation: is3d || hasParent ? 0 : (root.editorState.rotation || 0)
        transform: Matrix4x4 {
            matrix: clipFrame.is3d || clipFrame.hasParent
                    ? EditorState.preview.clipPoseMatrix(root.editorState, root.editorState.x || 0,
                                                        root.editorState.y || 0,
                                                        root.editorState.width || root.canvasW,
                                                        root.editorState.height || root.canvasH,
                                                        root.editorState.rotation || 0,
                                                        root.sx, root.sy)
                    : Qt.matrix4x4()
        }

        // The clip's own bounds, so it is obvious what the mask coordinates are relative to.
        MaskOverlayClipBounds { }

        Repeater {
            id: maskRepeater

            delegate: Item {
                id: maskBox
                required property int index
                required property var modelData
                readonly property var layerEntry: root.layerValues[index] || modelData

                readonly property bool isSelected: !!layerEntry.selected
                readonly property var maskData: layerEntry.mask || ({})
                readonly property int layerTrack: layerEntry.track
                readonly property int layerClip: layerEntry.clip

                // Bars ignores x/y/w entirely — it is two full-width bands whose height comes from
                // h — so a centred box with corner grips would be a lie. It draws its real bands
                // below and is edited from the inspector.
                readonly property bool isBars: maskData.shape === "bars"
                // Freeform ignores x/y/w/h too: the points are the shape. Its box is their
                // bounding rect, shown for reference, and it is edited vertex by vertex — bbox
                // grips would appear to do something and do nothing.
                readonly property bool isFreeform: maskData.shape === "freeform"
                readonly property bool editable: !isBars && !isFreeform && maskData.enabled !== false
                readonly property bool movable: editable
                                                || (isFreeform && maskData.enabled !== false)

                readonly property string layerId: layerEntry.id
                readonly property var session: EditorState.masking.editSession
                // True while a handle drag on this mask is running: the session then holds the
                // geometry, since the refreshed model is skipped until the drag ends.
                readonly property bool live: session.active && session.clipId === layerId

                // Bounding rect of the polygon's vertices, in normalized clip-frame units.
                readonly property var pointBounds: maskBox.isFreeform ? (layerEntry.bounds || null) : null

                readonly property real mx: live ? session.liveX
                        : (pointBounds ? pointBounds.x : (maskData.x || 0))
                readonly property real my: live ? session.liveY
                        : (pointBounds ? pointBounds.y : (maskData.y || 0))
                readonly property real mw: live ? session.liveW
                        : (pointBounds ? pointBounds.w : (maskData.w || 0))
                readonly property real mh: live ? session.liveH
                        : (pointBounds ? pointBounds.h : (maskData.h || 0))
                readonly property var displayPoints: live && isFreeform ? session.livePoints
                                                                        : (maskData.points || [])

                visible: !maskBox.isBars
                x: (mx - mw / 2) * clipFrame.width
                y: (my - mh / 2) * clipFrame.height
                width: Math.max(1, mw * clipFrame.width)
                height: Math.max(1, mh * clipFrame.height)
                transformOrigin: Item.Center
                // A freeform's box is derived from its (already absolute) points, so rotating it
                // here would double-apply the rotation the rasterizer does.
                rotation: maskBox.isFreeform
                          ? 0 : (maskBox.live ? maskBox.session.liveRotation : (maskData.rotation || 0))
                // The selected entry sits on top so its grips win hit-testing over the outlines of
                // entries behind it.
                z: maskBox.isSelected ? 100 : 0

                // Arrow keys nudge the selected mask; Shift makes the step coarse. Focus is taken
                // explicitly on click, never bound to selection — a rebuild would otherwise steal
                // it from the properties panel.
                Keys.onPressed: function(event) {
                    if (!maskBox.isSelected || !maskBox.movable)
                        return
                    let dx = 0
                    let dy = 0
                    switch (event.key) {
                    case Qt.Key_Left:  dx = -1; break
                    case Qt.Key_Right: dx = 1; break
                    case Qt.Key_Up:    dy = -1; break
                    case Qt.Key_Down:  dy = 1; break
                    default: return
                    }
                    maskBox.session.nudge(maskBox.layerId, dx, dy,
                                          (event.modifiers & Qt.ShiftModifier) !== 0,
                                          clipFrame.width, clipFrame.height)
                    event.accepted = true
                }

                // The mask outline. Ellipse and the ornamental shapes are drawn as their bounding
                // box plus a hint, rather than reproducing the rasterizer's geometry in QML — the
                // point of the box is the handles, and the real shape is visible in the composite
                // behind it.
                MaskOverlayOutline {
                    selected: maskBox.isSelected
                    enabledMask: maskBox.maskData.enabled !== false
                    shape: maskBox.maskData.shape || ""
                }

                // Feather reaches beyond the shape's edge, so show how far. Drawn outside so it
                // reads as "the edge fades out to here".
                MaskOverlayFeatherRing {
                    selected: maskBox.isSelected
                    feather: maskBox.maskData.feather || 0
                    scaleX: root.sx
                    scaleY: root.sy
                    shape: maskBox.maskData.shape || ""
                }

                TapHandler {
                    onTapped: {
                        EditorState.selectClip(maskBox.layerTrack, maskBox.layerClip)
                        maskBox.forceActiveFocus()
                    }
                }

                // Body move. The pointer is read in scene coordinates and mapped into clipFrame,
                // which is the frame mask coordinates are normalized to. Going through the
                // transform chain is what makes this rotation-proof: there are two rotations
                // stacked here (the host clip's on clipFrame, the mask's own on this box), and
                // mapFromItem unwinds both. DragHandler.translation would arrive in the rotated
                // box's axes instead, and undoing that by hand sent a rotated mask sideways.
                //
                // The mask's own rotation must not enter the delta at all: dragging a rotated box
                // still means "follow the cursor".
                DragHandler {
                    id: bodyDrag
                    target: null
                    enabled: maskBox.movable && maskBox.isSelected

                    onActiveChanged: {
                        if (active) {
                            // Taken at activation rather than at the press, so the drag threshold
                            // is not replayed as a jump.
                            const p = clipFrame.mapFromItem(null,
                                                            bodyDrag.centroid.scenePosition.x,
                                                            bodyDrag.centroid.scenePosition.y)
                            if (!maskBox.session.begin(maskBox.layerId, clipFrame.width,
                                                       clipFrame.height))
                                return
                            maskBox.session.beginBody(p.x / Math.max(1, clipFrame.width),
                                                      p.y / Math.max(1, clipFrame.height))
                            root.interacting = true
                            maskBox.forceActiveFocus()
                        } else {
                            root.endInteraction()
                        }
                    }

                    onCentroidChanged: {
                        if (!active)
                            return
                        const p = clipFrame.mapFromItem(null,
                                                        bodyDrag.centroid.scenePosition.x,
                                                        bodyDrag.centroid.scenePosition.y)
                        // Ctrl bypasses snapping. Alt is unusable here — the window manager takes
                        // it for window drags on Linux.
                        maskBox.session.moveBody(p.x / Math.max(1, clipFrame.width),
                                                 p.y / Math.max(1, clipFrame.height),
                                                 (bodyDrag.centroid.modifiers & Qt.ControlModifier) !== 0)
                    }
                }

                // Rotation handle, 28px above the box.
                MaskOverlayRotateHandle {
                    mask: maskBox
                    frame: clipFrame
                    overlay: root
                }

                // Polygon vertices. Drawn in the clip frame rather than the mask box, because the
                // box is only their bounding rect — dragging a vertex changes that rect, which
                // would otherwise move every other vertex under the cursor.
                // The Repeaters sit in holders parented to the clip frame, so their delegates stay
                // the Repeater's siblings; delegates reparenting themselves broke Repeater stacking.
                Item {
                    parent: clipFrame
                    anchors.fill: parent
                    z: 200

                    Repeater {
                        model: (maskBox.isSelected && maskBox.isFreeform
                                && maskBox.maskData.enabled !== false)
                               ? maskBox.displayPoints.length : 0

                        delegate: MaskOverlayVertexHandle {
                            mask: maskBox
                            frame: clipFrame
                            overlay: root
                        }
                    }
                }

                // Edge midpoints: click one to split that edge.
                Item {
                    parent: clipFrame
                    anchors.fill: parent
                    z: 199

                    Repeater {
                        model: (maskBox.isSelected && maskBox.isFreeform
                                && maskBox.maskData.enabled !== false)
                               ? maskBox.displayPoints.length : 0

                        delegate: MaskOverlayEdgeMidpoint {
                            mask: maskBox
                            frame: clipFrame
                        }
                    }
                }

                // Resize grips — the same frame the canvas crop tool and TransformOverlay use.
                Repeater {
                    model: (maskBox.editable && maskBox.isSelected)
                           ? [
                               { dx: -1, dy:  0, cursor: Qt.SizeHorCursor },
                               { dx:  1, dy:  0, cursor: Qt.SizeHorCursor },
                               { dx:  0, dy: -1, cursor: Qt.SizeVerCursor },
                               { dx:  0, dy:  1, cursor: Qt.SizeVerCursor },
                               { dx: -1, dy: -1, cursor: Qt.SizeFDiagCursor },
                               { dx:  1, dy:  1, cursor: Qt.SizeFDiagCursor },
                               { dx:  1, dy: -1, cursor: Qt.SizeBDiagCursor },
                               { dx: -1, dy:  1, cursor: Qt.SizeBDiagCursor }
                           ]
                           : []

                    delegate: MaskOverlayResizeGrip {
                        mask: maskBox
                        frame: clipFrame
                        overlay: root
                    }
                }
            }
        }

        // Bars, drawn as the two full-width bands it actually is. It has no bounding box to
        // grip: the rasterizer ignores x, y, w and rotation outright and derives both bands from
        // `h` alone, so a centred box with corner grips would be a lie. The one thing that does
        // mean something is the band edge, so that is what is draggable.
        Repeater {
            model: root.layerModel

            delegate: Item {
                id: barsEntry
                required property int index
                required property var modelData
                readonly property var layerEntry: root.layerValues[index] || modelData
                readonly property var maskData: layerEntry.mask || ({})
                readonly property bool isSelected: !!layerEntry.selected
                readonly property string layerId: layerEntry.id
                readonly property var session: EditorState.masking.editSession
                readonly property bool live: session.active && session.clipId === layerId
                anchors.fill: parent
                visible: maskData.shape === "bars" && maskData.enabled !== false

                // Live override during a drag, so the bands track the cursor without waiting for
                // the model to come back.
                readonly property real mh: live ? session.liveH : (maskData.h || 0)
                // barH in the rasterizer is h * canvasHeight * 0.5, applied at both edges.
                readonly property real bandH: Math.max(1, mh * 0.5 * clipFrame.height)

                TapHandler {
                    onTapped: EditorState.selectClip(modelData.track, modelData.clip)
                }

                Repeater {
                    model: 2
                    delegate: Item {
                        id: band
                        required property int index
                        readonly property bool atTop: band.index === 0
                        width: clipFrame.width
                        height: barsEntry.bandH
                        y: atTop ? 0 : clipFrame.height - barsEntry.bandH

                        MaskOverlayBarBand {
                            selected: barsEntry.isSelected
                        }

                        // The inner edge — the one `h` actually moves. Drawn as a bar rather than
                        // a dot because it spans the whole width, like the thing it resizes.
                        MaskOverlayBarEdge {
                            atTop: band.atTop
                            bars: barsEntry
                            frame: clipFrame
                            overlay: root
                        }
                    }
                }
            }
        }
    }

    // Nothing to manipulate: no clip under the playhead, or the clip has no mask layers.
    MaskOverlayEmptyHint {
        visible: !root.hasFrame || root.layers.length === 0
        hasFrame: root.hasFrame
    }

    MaskOverlayEmptyHint {
        visible: root.hasFrame && root.layers.some(l => l.selected && l.mask && l.mask.shape === "freeform")
        hasFrame: root.hasFrame
        message: Theme.touchInput
                 ? qsTr("Drag points to reshape · Tap ○ on an edge to add a point · Hold a point to delete")
                 : qsTr("Drag points to reshape · Click ○ on an edge to add a point · Right-click a point to delete")
    }
}
