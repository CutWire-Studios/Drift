import QtQuick
import QtQuick.Controls.Basic
import Drift

// Keyframe strip aligned with the timeline: left gutter matches track labels,
// graph scroll + playhead share the timeline's contentX / pxPerSecond.
//
// Every animated property of the selected clip earns a chip here — the strip is a view of the
// clip, not of the inspector's selection. A chip toggles whether that curve is drawn: it is a
// display filter and nothing more, so a hidden curve keeps animating and keeps its chip. Switching
// an animation *off* is the inspector row's job, not this strip's.
Item {
    id: root

    property real pxPerSecond: 50
    property real contentX: 0
    // The canvases below cover the viewport plus a chunk either side, parked on a chunk boundary,
    // so scrolling (and playback's auto-scroll) only repaints them when a boundary is crossed
    // rather than on every frame.
    readonly property real paintChunk: 256
    readonly property real paintOriginX: Math.floor(contentX / paintChunk) * paintChunk - paintChunk
    property real contentWidth: 800
    property real labelsWidth: Theme.trackLabelsWidth
    // Whether the open properties tab edits keyframeable properties (PropertiesPanel `keyframes`).
    property bool keyframesTab: false

    readonly property bool hasClip: EditorState.selectedTrack >= 0 && EditorState.selectedClip >= 0
    readonly property var clipData: {
        void EditorState.selectedClipData
        return EditorState.selectedClipData
    }

    // Effect parameters are addressed as "fx.<effectIndex>.<paramKey>", and audio effect ones as
    // "afx.<effectIndex>.<paramKey>"; the label, range and color all come from the effect's own
    // param spec on the selected clip.
    function effectParam(id) {
        if (!clipData)
            return null
        const audio = id.substring(0, 4) === "afx."
        if (!audio && id.substring(0, 3) !== "fx.")
            return null
        const stack = audio ? clipData.audioEffects : clipData.effects
        const start = audio ? 4 : 3
        if (!stack)
            return null
        const dot = id.indexOf(".", start)
        if (dot < 0)
            return null
        const effectIndex = parseInt(id.substring(start, dot))
        const key = id.substring(dot + 1)
        if (isNaN(effectIndex) || effectIndex < 0 || effectIndex >= stack.length)
            return null
        const effect = stack[effectIndex]
        const params = effect.params || []
        for (let i = 0; i < params.length; ++i) {
            if (params[i].key === key)
                return { effect: effect, param: params[i] }
        }
        return null
    }

    // Spelled-out names for the one-to-three character property chips.
    readonly property bool cameraClip: !!clipData && clipData.kind === "adjustment" && clipData.adjustmentKind === "camera"

    function propertyLabel(id) {
        if (cameraClip) {
            switch (id) {
            case "positionX": return qsTr("Position X")
            case "positionY": return qsTr("Position Y")
            case "rotation": return qsTr("Rotation Z")
            case "anchorX": return qsTr("Point of Interest X")
            case "anchorY": return qsTr("Point of Interest Y")
            case "anchorZ": return qsTr("Point of Interest Z")
            case "perspective": return qsTr("Zoom")
            }
        }
        switch (id) {
        case "positionX": return qsTr("X position")
        case "positionY": return qsTr("Y position")
        case "scaleX": return qsTr("Scale X")
        case "scaleY": return qsTr("Scale Y")
        case "rotation": return qsTr("Rotation")
        case "rotationX": return qsTr("Rotation X")
        case "rotationY": return qsTr("Rotation Y")
        case "z": return qsTr("Position Z")
        case "anchorX": return qsTr("Anchor X")
        case "anchorY": return qsTr("Anchor Y")
        case "anchorZ": return qsTr("Anchor Z")
        case "perspective": return qsTr("Perspective")
        case "opacity": return qsTr("Opacity")
        case "volume": return qsTr("Volume")
        case "mask.x": return qsTr("Mask X")
        case "mask.y": return qsTr("Mask Y")
        case "mask.w": return qsTr("Mask width")
        case "mask.h": return qsTr("Mask height")
        case "mask.rotation": return qsTr("Mask rotation")
        case "mask.feather": return qsTr("Mask feather")
        }
        // Text and shape properties are named by the engine ("Shadow · Blur"): the layer stack
        // is per clip, so no static table could spell them.
        if (id.substring(0, 5) === "text." || id.substring(0, 6) === "shape.") {
            const label = EditorState.keyframePropertyLabel(
                            EditorState.selectedTrack, EditorState.selectedClip, id)
            if (label.length > 0)
                return label
        }
        const fx = effectParam(id)
        if (fx)
            return fx.effect.label + " · " + fx.param.label
        return id
    }
    function chipLabel(id) {
        if (cameraClip && id === "perspective")
            return "Zm"
        switch (id) {
        case "positionX": return "X"
        case "positionY": return "Y"
        case "scaleX": return "SX"
        case "scaleY": return "SY"
        case "rotation": return "°"
        case "rotationX": return "X°"
        case "rotationY": return "Y°"
        case "z": return "Z"
        case "anchorX": return "AX"
        case "anchorY": return "AY"
        case "anchorZ": return "AZ"
        case "perspective": return "Per"
        case "opacity": return "Op"
        case "volume": return "Vol"
        case "mask.x": return "MX"
        case "mask.y": return "MY"
        case "mask.w": return "MW"
        case "mask.h": return "MH"
        case "mask.rotation": return "M°"
        case "mask.feather": return "MFe"
        }
        const fx = effectParam(id)
        if (fx)
            return fx.param.label.substring(0, 3)
        return id
    }
    // Volume and audio effects only exist on clips that carry audio; the rest are visual.
    function supportsProperty(id) {
        if (!clipData)
            return false
        if (cameraClip && (id === "scaleX" || id === "scaleY"))
            return false
        if (id === "volume")
            return clipData.kind === "audio" || clipData.kind === "video"
        if (id.substring(0, 3) === "fx.")
            return clipData.kind !== "audio" && effectParam(id) !== null
        if (id.substring(0, 4) === "afx.")
            return effectParam(id) !== null
        // A mask lives on the clip's lane, so the strip offers it for the same clips the Masks
        // tab does. There is no per-id validation to do: the id set is fixed and closed.
        if (id.substring(0, 5) === "mask.")
            return clipData.kind !== "audio"
        return clipData.kind !== "audio"
    }

    // Each series is normalized against its own value range, so curves in wildly
    // different units (px vs. 0–1 opacity) stay comparable when overlaid.
    function valueRangeFor(id, pts, base) {
        const fx = effectParam(id)
        return EditorState.keyframes.curveRange(id, pts, base, fx !== null, fx ? fx.param.min : 0,
                                                fx ? fx.param.max : 1)
    }

    // Bezier handles are opt-in; without them a curve is shaped by dragging the curve itself.
    property bool showHandles: false

    // One entry per animated property of the clip — the chip row's model, hidden curves included.
    // [{ prop, label, color, points, enabled, shown, valueMin, valueMax }, ...]
    readonly property var allSeries: {
        void EditorState.selectedClipData
        void EditorState.tracksRevision
        void EditorState.keyframeGraphHiddenProperties
        if (!hasClip)
            return []
        const out = []
        const animated = EditorState.clipAnimatedProperties(
                           EditorState.selectedTrack, EditorState.selectedClip)
        const hidden = EditorState.keyframeGraphHiddenProperties
        for (let i = 0; i < animated.length; ++i) {
            const id = animated[i]
            if (!supportsProperty(id))
                continue
            const pts = EditorState.clipKeyframes(
                          EditorState.selectedTrack, EditorState.selectedClip, id) || []
            // What the curve reads as when the property has no keys at all — the level of the
            // flat line drawn across the clip in that case.
            const base = EditorState.propertyBaseValue(
                           EditorState.selectedTrack, EditorState.selectedClip, id, 0)
            const range = root.valueRangeFor(id, pts, base)
            out.push({
                fixedRange: effectParam(id) !== null,
                prop: id,
                label: root.chipLabel(id),
                color: Theme.keyframeCurveColor(id),
                points: pts,
                baseValue: base,
                enabled: EditorState.clipPropertyKeyframesEnabled(
                             EditorState.selectedTrack, EditorState.selectedClip, id),
                shown: hidden.indexOf(id) < 0,
                valueMin: range.min,
                valueMax: range.max
            })
        }
        return out
    }

    // The curves actually drawn: everything the chips have left visible.
    readonly property var series: {
        const out = []
        for (let i = 0; i < allSeries.length; ++i) {
            if (allSeries[i].shown)
                out.push(allSeries[i])
        }
        return out
    }

    readonly property int keyCount: {
        let n = 0
        for (let i = 0; i < series.length; ++i)
            n += series[i].points.length
        return n
    }

    readonly property real clipStart: clipData.start || 0
    readonly property real clipDuration: Math.max(0.1, clipData.duration || 1)

    // Beat detection results for whatever range was last analyzed. Analysis is explicit
    // (the gutter buttons) because it renders the timeline mix — never on selection change.
    // The grid and the transients are separate layers over that one result.
    readonly property var beats: EditorState.beatAnalysis
    readonly property bool analyzed: !!beats && beats.rangeDuration > 0
    readonly property bool hasGrid: EditorState.beatGridVisible && !!beats && !!beats.beats
                                    && beats.beats.length > 0
    readonly property bool hasOnsets: EditorState.onsetsVisible && !!beats && !!beats.onsets
                                      && beats.onsets.length > 0
    readonly property real bpm: (beats && beats.bpm) ? beats.bpm : 0

    // Turning a layer on analyzes if this range has not been analyzed yet; analyzeBeats
    // no-ops when the result is already current.
    function showBeatLayer(gridLayer) {
        if (gridLayer)
            EditorState.beatGridVisible = true
        else
            EditorState.onsetsVisible = true
        EditorState.analyzeBeats(root.clipStart, root.clipDuration)
    }

    function lowerBound(list, value, key) {
        let lo = 0
        let hi = list.length
        while (lo < hi) {
            const mid = (lo + hi) >> 1
            if ((key ? list[mid][key] : list[mid]) < value)
                lo = mid + 1
            else
                hi = mid
        }
        return lo
    }

    // Drag the bottom edge or wheel over the header to grow the lane.
    property real laneHeight: 88
    readonly property real minLaneHeight: 60
    readonly property real maxLaneHeight: 460
    readonly property real compactHeight: 88
    readonly property real expandedHeight: 180

    Behavior on laneHeight {
        enabled: !resizeGrip.dragging
        NumberAnimation { duration: 160; easing.type: Easing.OutCubic }
    }

    function toggleExpanded() {
        if (curveEditing)
            laneHeight = compactHeight
        else
            laneHeight = expandedHeight
    }

    height: visible ? laneHeight : 0
    // Open whenever the clip has an animation to show, even if every curve is currently folded
    // away — otherwise hiding the last one would take the chips with it.
    visible: keyframesTab && hasClip && clipData && allSeries.length > 0

    // Curve editing focuses one series: it gets tangent handles and owns the value axis,
    // while the others stay drawn as dimmed context. Handles from two series normalized to
    // different ranges are not comparable on screen, so only one can be live at a time.
    property string focusedProp: ""
    readonly property int focusedIndex: {
        for (let i = 0; i < series.length; ++i) {
            if (series[i].prop === focusedProp)
                return i
        }
        return series.length > 0 ? 0 : -1
    }
    // Tangent handles need room; below this the lane is an overview and shows keys only.
    readonly property bool curveEditing: laneHeight >= 140

    function autoSmooth() {
        const entries = focusedIndex >= 0 ? [series[focusedIndex]] : series
        EditorState.beginEditGroup()
        for (const entry of entries)
            EditorState.setKeyframeSmooth(EditorState.selectedTrack, EditorState.selectedClip,
                                          entry.prop, 0, true)
        EditorState.endEditGroup(qsTr("Auto-smooth keyframes"))
    }

    function openKeyMenu(hit, x, y) {
        const ref = hit.refs[0]
        keyMenu.keyProp = ref.prop
        keyMenu.keySeconds = ref.seconds
        keyMenu.segment = EditorState.keyframeSegmentEasing(EditorState.selectedTrack,
                                                            EditorState.selectedClip,
                                                            keyMenu.keyProp, keyMenu.keySeconds)
        keyMenu.popup(input, x, y)
    }

    // Absolute timeline X — same mapping as clips on the track.
    function xForSeconds(seconds) {
        return seconds * pxPerSecond
    }
    function secondsForX(x) {
        return x / pxPerSecond
    }

    KeyframeGraphFrame { anchors.fill: parent }

    Row {
        anchors.fill: parent

        // Left gutter — same width as track labels so the graph lines up.
        Item {
            id: leftGutter
            width: root.labelsWidth
            height: parent.height
            clip: true

            // DAW-style lane zoom: wheel over this header grows/shrinks the keyframe lane
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.NoButton
                z: 30
                onWheel: (wheel) => {
                    // Modified wheel belongs to the timeline's zoom/pan.
                    if (wheel.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) {
                        wheel.accepted = false
                        return
                    }
                    const dy = wheel.angleDelta.y !== 0 ? wheel.angleDelta.y
                                                        : wheel.pixelDelta.y
                    if (dy === 0)
                        return
                    const step = 20
                    const next = root.laneHeight + (dy > 0 ? step : -step)
                    root.laneHeight = Math.max(root.minLaneHeight,
                                               Math.min(root.maxLaneHeight, next))
                }
            }

            PinchHandler {
                target: null
                acceptedDevices: PointerDevice.TouchScreen
                property real startHeight: 0
                onActiveChanged: if (active) startHeight = root.laneHeight
                onActiveScaleChanged: {
                    if (active)
                        root.laneHeight = Math.max(root.minLaneHeight,
                                                   Math.min(root.maxLaneHeight, startHeight * activeScale))
                }
            }

            TapHandler {
                onDoubleTapped: root.toggleExpanded()
            }

            Column {
                anchors.fill: parent
                anchors.margins: 6
                spacing: 4

                KeyframeGraphHeaderBar {
                    width: parent.width
                    host: root
                }

                // One chip per animated property of the clip, tinted to its curve. Clicking folds
                // that curve away or brings it back; the chip itself always stays put.
                Flow {
                    width: parent.width
                    spacing: 3
                    Repeater {
                        model: root.allSeries
                        delegate: ThemedChip {
                            required property var modelData
                            text: modelData.label
                            chipHeight: 18
                            horizontalPadding: 3
                            accentColor: modelData.color
                            // A property whose keyframes are switched off in the inspector still
                            // has a curve to look at; it just is not driving anything.
                            opacity: modelData.enabled ? 1.0 : 0.5
                            tooltip: {
                                const name = root.propertyLabel(modelData.prop)
                                const state = modelData.enabled ? name
                                                                : qsTr("%1 (keyframes off)").arg(name)
                                return modelData.shown ? qsTr("%1 — click to hide this curve").arg(state)
                                                       : qsTr("%1 — click to show this curve").arg(state)
                            }
                            selected: modelData.shown
                            onClicked: {
                                EditorState.toggleKeyframeGraphPropertyVisible(modelData.prop)
                                // Showing a curve also makes it the one the tangent handles belong
                                // to, so the chip you just clicked is the curve you can shape.
                                if (!modelData.shown)
                                    root.focusedProp = modelData.prop
                                else if (root.focusedProp === modelData.prop)
                                    root.focusedProp = ""
                            }
                        }
                    }
                }

                KeyframeGraphLaneStatus { width: parent.width
                    host: root }
            }

            KeyframeGraphGutterRule { }
        }

        // Timeline-aligned viewport (scrolls with the tracks below).
        Item {
            id: viewport
            width: parent.width - root.labelsWidth
            height: parent.height
            clip: true

            Item {
                id: content
                x: -root.contentX
                width: Math.max(viewport.width + root.contentX, root.contentWidth)
                height: parent.height

                Item {
                    id: graph
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.topMargin: 6
                    anchors.bottomMargin: 6

                    // Selected clip's time span — sits under the same region as the clip.
                    KeyframeGraphClipSpan { host: root }

                    KeyframeGraphValueAxis { host: root }

                    // Playhead — same absolute X as the timeline playhead.
                    KeyframeGraphPlayhead { host: root }

                    // Detected beat grid + onset ticks, under the curves and key dots.
                    KeyframeGraphBeatCanvas {
                        host: root
                        viewportWidth: viewport.width
                    }

                    KeyframeCurvesItem {
                        x: root.contentX
                        width: viewport.width
                        height: parent.height
                        model: curvesModel
                        selection: selectionModel
                        controller: EditorState.keyframes
                        curveEditing: root.curveEditing
                        pxPerSecond: root.pxPerSecond
                        originX: root.contentX
                        clipStart: root.clipStart
                        clipDuration: root.clipDuration
                        lineWidth: Theme.c.keyframeGraphCurveCanvas.lineWidth
                        lineWidthMuted: Theme.c.keyframeGraphCurveCanvas.lineWidthMuted
                        opacityContext: Theme.c.keyframeGraphCurveCanvas.opacityContext
                        opacityDisabled: Theme.c.keyframeGraphCurveCanvas.opacityDisabled
                        showKeys: true
                        hoverId: interaction.hoverId
                        keyOpacityContext: Theme.c.keyframeGraphKeyDot.opacityUnfocused
                        animationDuration: Theme.durationFast
                        dotStyle: Theme.plainMap(Theme.c.keyframesKeyDot)
                        handleStyle: Theme.plainMap(Theme.c.keyframeGraphTangentHandle)
                    }

                    // Everything the pointer does on keys, handles and the curve goes through the
                    // interaction; the strip behind scrolls with the timeline, so this sits on the viewport.
                    KeyframeInputArea {
                        id: input
                        x: root.contentX
                        width: viewport.width
                        height: parent.height
                        interaction: interaction
                        focusTarget: root
                        handlesWheel: false
                        buttons: Qt.LeftButton | Qt.RightButton
                    }
                }
            }
        }

    }

    // Bottom edge resize grip. A sibling of the Row rather than a child, so it anchors to the
    // lane instead of being laid out horizontally beside the graph, and sits above the lane's
    // content so a drag near the boundary resizes rather than grabbing what is underneath.
    KeyframeGraphResizeGrip {
        id: resizeGrip
        host: root
    }

    KeyframeSelection {
        id: selectionModel
    }

    KeyframeTimeView {
        id: timeView
        pxPerSecond: root.pxPerSecond
        contentX: root.contentX
        clipStart: root.clipStart
        clipDuration: root.clipDuration
    }

    KeyframeCurvesModel {
        id: curvesModel
        selection: selectionModel
        externalSeries: root.series
        focusedProp: root.focusedIndex >= 0 && root.focusedIndex < root.series.length
                     ? root.series[root.focusedIndex].prop : ""
        showHandles: root.showHandles && root.curveEditing
        graphMargin: 0
        viewHeight: graph.height
    }

    KeyframeInteraction {
        id: interaction

        mode: "curves"
        selection: selectionModel
        track: EditorState.selectedTrack
        clip: EditorState.selectedClip
        viewWidth: viewport.width
        viewHeight: graph.height
        playhead: EditorState.playheadSeconds
        fps: Math.max(1, EditorState.projectFile.projectFps())
        curveEditing: root.curveEditing
        doubleClickKey: "remove"
        timelineSnap: true
        entryTarget: "value"

        Component.onCompleted: attach(null, curvesModel, timeView, EditorState.keyframes)
        onFocusRequested: (track, rowProp) => root.focusedProp = track
        onKeysAdded: (props) => {
            EditorState.showKeyframeGraphProperty(props[0])
            root.focusedProp = props[0]
        }
        onContextRequested: (x, y, hit) => {
            if (hit.dot)
                root.openKeyMenu(hit, x, y)
        }
    }

    // Selection changes also fire for edits to the same clip, so only a different clip resets it.
    readonly property string clipKey: EditorState.selectedTrack + ":" + EditorState.selectedClip
    onClipKeyChanged: {
        interaction.cancel()
        selectionModel.clear()
    }

    Keys.onShortcutOverride: (event) => {
        if (selectionModel.count === 0)
            return
        const arrows = [Qt.Key_Delete, Qt.Key_Backspace, Qt.Key_Left, Qt.Key_Right, Qt.Key_Up, Qt.Key_Down]
        if (arrows.indexOf(event.key) >= 0)
            event.accepted = true
    }

    Keys.onPressed: (event) => {
        if (input.handleKey(event)) {
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Escape) {
            event.accepted = interaction.escape()
            return
        }
        if (event.key === Qt.Key_A && (event.modifiers & Qt.ControlModifier)) {
            interaction.selectAll()
            event.accepted = true
            return
        }
        if (selectionModel.count === 0)
            return
        const shift = !!(event.modifiers & Qt.ShiftModifier)
        switch (event.key) {
        case Qt.Key_Delete:
        case Qt.Key_Backspace:
            event.accepted = interaction.deleteSelected()
            break
        case Qt.Key_Left:
            event.accepted = interaction.nudge(-1, 0, shift)
            break
        case Qt.Key_Right:
            event.accepted = interaction.nudge(1, 0, shift)
            break
        case Qt.Key_Up:
            event.accepted = interaction.nudge(0, 1, shift)
            break
        case Qt.Key_Down:
            event.accepted = interaction.nudge(0, -1, shift)
            break
        }
    }

    // Right-click on a key: how the segment leaving it moves. Checks the segment's current choice.
    ThemedContextMenu {
        id: keyMenu

        property string keyProp: ""
        property real keySeconds: 0
        property var segment: ({})
        readonly property string segmentKind: segment.spec ? segment.spec.kind : ""

        function isPreset(id) {
            return segment.presetId === id
        }
        function setSegment(spec) {
            EditorState.setKeyframeSegmentEasing(EditorState.selectedTrack, EditorState.selectedClip,
                                                 keyProp, keySeconds, spec)
        }
        function openEasing() {
            if (ShellBridge.phoneShell) {
                ShellBridge.easingSheetRequested(EditorState.selectedTrack, EditorState.selectedClip,
                                                 keyProp, root.propertyLabel(keyProp), keySeconds)
                return
            }
            easingPopupLoader.active = true
            easingPopupLoader.item.anchorItem = input
            easingPopupLoader.item.propLabel = root.propertyLabel(keyProp)
            easingPopupLoader.item.openFor(EditorState.selectedTrack, EditorState.selectedClip,
                                           keyProp, keySeconds)
        }
        function saveCurrentAsPreset() {
            const n = EditorState.easingPresets().filter(p => p.user).length + 1
            EditorState.saveEasingPreset(qsTr("My easing %1").arg(n), segment.spec)
        }

        ThemedContextMenu {
            title: qsTr("Interpolation")

            ThemedMenuItem {
                text: qsTr("Smooth")
                onTriggered: EditorState.setKeyframeSmooth(EditorState.selectedTrack, EditorState.selectedClip,
                                                           keyMenu.keyProp, keyMenu.keySeconds, false)
            }
            ThemedMenuItem {
                text: qsTr("Straight")
                icon.name: keyMenu.isPreset("linear") ? Theme.icons.check : ""
                onTriggered: keyMenu.setSegment({ kind: "linear" })
            }
            ThemedMenuItem {
                text: qsTr("Hold")
                icon.name: keyMenu.isPreset("hold") ? Theme.icons.check : ""
                onTriggered: keyMenu.setSegment({ kind: "hold" })
            }
        }
        ThemedMenuItem {
            text: qsTr("Easing…")
            onTriggered: keyMenu.openEasing()
        }
        ThemedMenuItem {
            text: qsTr("Save curve as preset…")
            enabled: keyMenu.segmentKind === "bezier" || keyMenu.segmentKind === "function"
            onTriggered: keyMenu.saveCurrentAsPreset()
        }
    }

    Loader {
        id: easingPopupLoader
        active: false
        sourceComponent: EasingPickerPopup { }
    }
}
