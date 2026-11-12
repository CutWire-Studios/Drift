pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes
import QtQuick.Controls.Basic
import Drift

// Shapes the motion of the segment that starts at one keyframe and runs to the next.
// Holds no easing state of its own: every read goes through the EditorState easing API
// and every edit writes back through it.
Column {
    id: root

    property int trackIndex: -1
    property int clipIndex: -1
    property string propKey: ""
    property string propLabel: ""
    property real keySeconds: 0

    // "curve" picks a plain curve for a host that owns the value (fade, transition, text ease)
    // instead of shaping a keyframe segment; the track/clip/key properties are then unused.
    property string mode: "keyframe"
    property var currentSpec: null
    property string currentStyle: ""
    property var styles: []
    property bool allowFunctions: false
    property bool unitRange: false
    readonly property bool curveMode: mode === "curve"
    // The curve being edited before the host has adopted it: a fresh "New curve" seed.
    property var draft: null

    signal picked(var spec)
    signal stylePicked(string id)

    // Nothing emits this yet; the popup closes on it so a later caller can dismiss the picker.
    signal closeRequested()

    spacing: Theme.spacingMd

    // Bumped on every project change, so the reads below re-run against the engine.
    property int revision: 0
    // Set while a bezier handle is dragged: the drawn shape is the drag's own, not the
    // engine's, which only catches up on commit.
    property var dragSpec: null
    // Bumped when a favourite is toggled, so the grids re-order.
    property int favoritesTick: 0

    readonly property var current: {
        void revision
        if (root.curveMode)
            return { hasSegment: true, spec: root.currentSpec ? root.currentSpec : null,
                     presetId: root.currentStyle ? "" : root.matchPreset(root.currentSpec) }
        return EditorState.keyframeSegmentEasing(root.trackIndex, root.clipIndex, root.propKey,
                                                 root.keySeconds)
    }
    readonly property bool hasSegment: !!current.hasSegment
    readonly property var spec: current.spec ? current.spec : null
    readonly property var baseSpec: draft ? draft : spec
    readonly property var shownSpec: dragSpec ? dragSpec : baseSpec
    readonly property string kind: baseSpec ? baseSpec.kind : ""

    readonly property var presets: {
        void revision
        return EditorState.easingPresets()
    }
    readonly property var pickable: curveMode ? presets.filter(p => root.pickableSpec(p.spec)) : presets
    readonly property var userPresets: {
        void favoritesTick
        return favoritesFirst(pickable.filter(p => p.user))
    }

    // Handle positions for the kinds drawn with a unit-square editor; null for the rest.
    readonly property var handles: {
        const s = shownSpec
        if (!s)
            return null
        if (s.kind === "bezier")
            return { x1: s.x1, y1: s.y1, x2: s.x2, y2: s.y2 }
        if (s.kind === "linear")
            return { x1: 0, y1: 0, x2: 1, y2: 1 }
        return null
    }
    readonly property var curveSamples: shownSpec ? EditorState.easingSamples(shownSpec, 48) : []

    readonly property int columns: Theme.touchInput ? 3 : 4
    readonly property real tileSpacing: Theme.spacingXs
    readonly property real tileWidth: (width - tileSpacing * (columns - 1)) / columns

    // The variant decides which of the In / Out / In-Out presets the family grid shows.
    // It follows the segment's current preset, and stays put for a custom curve.
    property string variant: "Out"
    readonly property var familyPresets: {
        void variant
        void favoritesTick
        const out = []
        for (const p of pickable) {
            if (p.family === "basic" || (!p.user && p.id === p.family + variant))
                out.push(p)
        }
        return favoritesFirst(out)
    }

    onCurrentSpecChanged: draft = null

    onCurrentChanged: {
        const v = variantOf(current.presetId)
        if (v.length > 0)
            variant = v
    }

    property var menuPreset: null
    property string renameId: ""

    function variantOf(id) {
        if (!id)
            return ""
        // InOut has to be tested before Out, which it also ends with.
        if (id.endsWith("InOut"))
            return "InOut"
        if (id.endsWith("In"))
            return "In"
        if (id.endsWith("Out"))
            return "Out"
        return ""
    }

    function favoritesFirst(list) {
        const fav = list.filter(p => EditorState.isAssetFavorite("easings", p.id))
        return fav.concat(list.filter(p => !EditorState.isAssetFavorite("easings", p.id)))
    }

    function pickableSpec(s) {
        if (!s || s.kind === "hold")
            return false
        if (s.kind === "function")
            return root.allowFunctions
        if (s.kind === "bezier" && root.unitRange)
            return s.y1 >= 0 && s.y1 <= 1 && s.y2 >= 0 && s.y2 <= 1
        return true
    }

    function sameSpec(a, b) {
        if (!a || !b || a.kind !== b.kind)
            return false
        const near = (u, v) => Math.abs(u - v) < 1e-3
        if (a.kind === "bezier")
            return near(a.x1, b.x1) && near(a.y1, b.y1) && near(a.x2, b.x2) && near(a.y2, b.y2)
        if (a.kind === "function")
            return a.fn === b.fn && near(a.amplitude, b.amplitude) && near(a.period, b.period)
        return true
    }

    function matchPreset(s) {
        if (!s)
            return ""
        for (const p of root.presets) {
            if (root.sameSpec(p.spec, s))
                return p.id
        }
        return ""
    }

    function startNewCurve() {
        const s = root.baseSpec
        root.draft = (s && s.kind === "bezier")
                ? Object.assign({}, s)
                : { kind: "bezier", x1: 0.42, y1: 0, x2: 0.58, y2: 1 }
    }

    function apply(spec) {
        if (!spec)
            return
        if (root.curveMode) {
            root.draft = null
            root.picked(spec)
            return
        }
        EditorState.setKeyframeSegmentEasing(root.trackIndex, root.clipIndex, root.propKey,
                                             root.keySeconds, spec)
    }

    function setFunctionParam(key, v, dragging) {
        const s = Object.assign({}, root.baseSpec)
        s[key] = v
        if (root.curveMode) {
            if (dragging)
                root.dragSpec = s
            else
                root.picked(s)
            return
        }
        if (dragging)
            EditorState.previewSetKeyframeSegmentEasing(root.trackIndex, root.clipIndex, root.propKey,
                                                        root.keySeconds, s)
        else
            EditorState.setKeyframeSegmentEasing(root.trackIndex, root.clipIndex, root.propKey,
                                                 root.keySeconds, s)
    }

    function openUserMenu(preset, x, y) {
        root.menuPreset = preset
        userMenu.popup(root, x, y)
    }

    function beginSave() {
        saveField.text = qsTr("My easing %1").arg(root.userPresets.length + 1)
        saving = true
        saveField.forceActiveFocus()
        saveField.selectAll()
    }

    function finishSave() {
        const name = saveField.text.trim()
        if (name.length > 0 && root.baseSpec)
            EditorState.saveEasingPreset(name, root.baseSpec)
        saving = false
    }

    property bool saving: false

    Connections {
        target: EditorState
        function onTracksChanged() { root.revision++ }
        function onSelectedClipDataChanged() { root.revision++ }
        function onEasingPresetsChanged() { root.revision++ }
        function onAssetFavoritesChanged() { root.favoritesTick++ }
    }

    Text {
        visible: !root.hasSegment
        width: root.width
        wrapMode: Text.WordWrap
        text: qsTr("Add another keyframe to shape the motion between them")
        color: Theme.c.easingPicker.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    Column {
        id: body
        visible: root.hasSegment
        width: root.width
        spacing: Theme.spacingMd

        // --- Shape editor -------------------------------------------------------
        Item {
            id: editorBox
            visible: root.handles !== null
            width: body.width
            height: 120

            readonly property real pad: 8
            readonly property real handleSize: Theme.touchInput ? 14 : 10
            readonly property var h: root.handles ? root.handles : { x1: 0, y1: 0, x2: 1, y2: 1 }

            // Unit square with y widened to [-0.5, 1.5] so overshoot can be dragged out.
            function px(x) { return pad + x * (width - 2 * pad) }
            function py(y) { return height - pad - (y + 0.5) / 2 * (height - 2 * pad) }
            function toX(x) { return Math.max(0, Math.min(1, (x - pad) / (width - 2 * pad))) }
            function toY(y) {
                const v = (height - pad - y) / (height - 2 * pad) * 2 - 0.5
                return Math.max(root.unitRange ? 0 : -0.5, Math.min(root.unitRange ? 1 : 1.5, v))
            }

            readonly property var curvePath: {
                void width
                void height
                const out = []
                const samples = root.curveSamples
                for (let i = 0; i < samples.length; ++i)
                    out.push(Qt.point(editorBox.px(samples[i].x), editorBox.py(samples[i].y)))
                return out
            }

            Rectangle {
                anchors.fill: parent
                radius: Theme.c.easingPicker.radius
                color: Theme.c.easingPicker.background
                border.width: Theme.c.easingPicker.borderWidth
                border.color: Theme.c.easingPicker.border
            }

            Shape {
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer

                ShapePath {
                    strokeColor: Theme.c.easingPicker.gridLine
                    strokeWidth: Theme.c.easingPicker.borderWidth
                    fillColor: "transparent"
                    PathPolyline { path: [Qt.point(editorBox.px(0), editorBox.py(0)), Qt.point(editorBox.px(1), editorBox.py(0))] }
                }
                ShapePath {
                    strokeColor: Theme.c.easingPicker.gridLine
                    strokeWidth: Theme.c.easingPicker.borderWidth
                    fillColor: "transparent"
                    PathPolyline { path: [Qt.point(editorBox.px(0), editorBox.py(1)), Qt.point(editorBox.px(1), editorBox.py(1))] }
                }
                ShapePath {
                    strokeColor: Theme.c.easingPicker.handleLine
                    strokeWidth: Theme.c.easingPicker.borderWidth
                    fillColor: "transparent"
                    PathPolyline { path: [Qt.point(editorBox.px(0), editorBox.py(0)), Qt.point(editorBox.px(editorBox.h.x1), editorBox.py(editorBox.h.y1))] }
                }
                ShapePath {
                    strokeColor: Theme.c.easingPicker.handleLine
                    strokeWidth: Theme.c.easingPicker.borderWidth
                    fillColor: "transparent"
                    PathPolyline { path: [Qt.point(editorBox.px(1), editorBox.py(1)), Qt.point(editorBox.px(editorBox.h.x2), editorBox.py(editorBox.h.y2))] }
                }
                ShapePath {
                    strokeColor: Theme.c.easingPicker.curve
                    strokeWidth: Theme.c.easingPicker.curveWidth
                    fillColor: "transparent"
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    PathPolyline { path: editorBox.curvePath }
                }
            }

            Repeater {
                model: [
                    { x: editorBox.h.x1, y: editorBox.h.y1 },
                    { x: editorBox.h.x2, y: editorBox.h.y2 }
                ]
                delegate: Rectangle {
                    required property var modelData
                    width: editorBox.handleSize
                    height: editorBox.handleSize
                    radius: width / 2
                    x: editorBox.px(modelData.x) - width / 2
                    y: editorBox.py(modelData.y) - height / 2
                    color: Theme.c.easingPicker.handle
                }
            }

            MouseArea {
                id: dragArea
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor

                // 0 = P1, 1 = P2, -1 = nothing grabbed.
                property int grabbed: -1
                // Latched per press: a touch that lands far from both handles is ignored, a
                // mouse press always grabs the nearer one.
                property bool pressedByTouch: false

                function nearest(mx, my) {
                    const d1 = Math.hypot(mx - editorBox.px(editorBox.h.x1), my - editorBox.py(editorBox.h.y1))
                    const d2 = Math.hypot(mx - editorBox.px(editorBox.h.x2), my - editorBox.py(editorBox.h.y2))
                    return { index: d1 <= d2 ? 0 : 1, distance: Math.min(d1, d2) }
                }

                onPressed: (mouse) => {
                    pressedByTouch = mouse.source !== Qt.MouseEventNotSynthesized
                    const hit = nearest(mouse.x, mouse.y)
                    if (pressedByTouch && hit.distance > 28) {
                        grabbed = -1
                        return
                    }
                    grabbed = hit.index
                    root.dragSpec = { kind: "bezier", x1: editorBox.h.x1, y1: editorBox.h.y1,
                                      x2: editorBox.h.x2, y2: editorBox.h.y2 }
                    if (!root.curveMode)
                        EditorState.beginPreviewDrag(qsTr("Edit easing"))
                }
                onPositionChanged: (mouse) => {
                    if (grabbed < 0)
                        return
                    const s = Object.assign({}, root.dragSpec)
                    if (grabbed === 0) {
                        s.x1 = editorBox.toX(mouse.x)
                        s.y1 = editorBox.toY(mouse.y)
                    } else {
                        s.x2 = editorBox.toX(mouse.x)
                        s.y2 = editorBox.toY(mouse.y)
                    }
                    root.dragSpec = s
                    if (!root.curveMode)
                        EditorState.previewSetKeyframeSegmentEasing(root.trackIndex, root.clipIndex,
                                                                    root.propKey, root.keySeconds, s)
                }
                onReleased: endDrag()
                onCanceled: endDrag()

                function endDrag() {
                    if (grabbed < 0)
                        return
                    grabbed = -1
                    if (root.curveMode) {
                        const s = root.dragSpec
                        root.draft = s
                        root.picked(s)
                    } else {
                        EditorState.commitPreviewDrag()
                    }
                    root.dragSpec = null
                }
            }
        }

        // --- Function curves: the curve plus its two parameters -----------------
        Column {
            id: functionBox
            visible: root.kind === "function"
            width: body.width
            spacing: Theme.spacingSm

            EasingThumb {
                width: functionBox.width
                height: 120
                samples: root.curveSamples
                curveColor: Theme.c.easingPicker.curve
            }

            Repeater {
                model: [
                    { key: "amplitude", label: qsTr("Amplitude"), from: 1, to: 3 },
                    { key: "period", label: qsTr("Period"), from: 0.1, to: 1 }
                ]
                delegate: Row {
                    id: paramRow
                    required property var modelData
                    width: functionBox.width
                    spacing: Theme.spacingSm

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 64
                        text: paramRow.modelData.label
                        color: Theme.c.easingPicker.foregroundMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                        elide: Text.ElideRight
                    }

                    ThemedSlider {
                        id: paramSlider
                        anchors.verticalCenter: parent.verticalCenter
                        width: paramRow.width - 64 - readout.width - paramRow.spacing * 2
                        label: paramRow.modelData.label
                        from: paramRow.modelData.from
                        to: paramRow.modelData.to
                        Binding on value {
                            when: !paramSlider.pressed
                            value: root.baseSpec ? root.baseSpec[paramRow.modelData.key] : paramRow.modelData.from
                        }
                        onPressedChanged: {
                            if (root.curveMode) {
                                if (!pressed && root.dragSpec) {
                                    const s = root.dragSpec
                                    root.dragSpec = null
                                    root.picked(s)
                                }
                                return
                            }
                            if (pressed)
                                EditorState.beginPreviewDrag(qsTr("Edit easing"))
                            else
                                EditorState.commitPreviewDrag()
                        }
                        onMoved: root.setFunctionParam(paramRow.modelData.key, value, pressed)
                    }

                    Text {
                        id: readout
                        anchors.verticalCenter: parent.verticalCenter
                        width: 36
                        horizontalAlignment: Text.AlignRight
                        text: paramSlider.value.toFixed(2)
                        color: Theme.c.easingPicker.foreground
                        font.family: Theme.monoFontFamily
                        font.pixelSize: Theme.fontSizeSm
                    }
                }
            }
        }

        // --- Hold: the step shape and a one-line explanation --------------------
        Column {
            visible: root.kind === "hold"
            width: body.width
            spacing: Theme.spacingSm

            EasingThumb {
                width: body.width
                height: 120
                samples: root.curveSamples
                curveColor: Theme.c.easingPicker.curve
            }

            Text {
                width: body.width
                wrapMode: Text.WordWrap
                text: qsTr("Holds until the next keyframe")
                color: Theme.c.easingPicker.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
        }

        // --- Named styles, for hosts that offer curves beyond the cubic presets ----
        Text {
            visible: root.curveMode && root.styles.length > 0
            text: qsTr("Styles")
            color: Theme.c.easingPicker.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeTiny
        }

        Flow {
            visible: root.curveMode && root.styles.length > 0
            width: body.width
            spacing: root.tileSpacing

            Repeater {
                model: root.styles
                delegate: EasingTile {
                    id: styleTile
                    required property var modelData
                    preset: styleTile.modelData
                    width: root.tileWidth
                    selected: root.currentStyle === styleTile.modelData.id && !root.draft
                    onActivated: {
                        Haptics.select()
                        root.draft = null
                        root.stylePicked(styleTile.modelData.id)
                    }
                }
            }
        }

        // --- Variant and the preset grid -----------------------------------------
        Row {
            id: variantRow
            width: body.width
            spacing: Theme.spacingXs

            Repeater {
                model: [
                    { value: "In", label: qsTr("In") },
                    { value: "Out", label: qsTr("Out") },
                    { value: "InOut", label: qsTr("In-Out") }
                ]
                delegate: ThemedChip {
                    id: variantChip
                    required property var modelData
                    width: (variantRow.width - variantRow.spacing * 2) / 3
                    text: variantChip.modelData.label
                    selected: root.variant === variantChip.modelData.value
                    onClicked: root.variant = variantChip.modelData.value
                }
            }
        }

        Flow {
            width: body.width
            spacing: root.tileSpacing

            Repeater {
                model: root.familyPresets
                delegate: EasingTile {
                    id: familyTile
                    required property var modelData
                    preset: familyTile.modelData
                    favoriteTab: "easings"
                    width: root.tileWidth
                    selected: root.current.presetId === familyTile.modelData.id
                    onActivated: {
                        Haptics.select()
                        root.apply(familyTile.modelData.spec)
                    }
                }
            }
        }

        // --- My curves: saved presets, with rename and delete on right-click / hold
        Text {
            visible: root.curveMode || root.userPresets.length > 0
            text: qsTr("My curves")
            color: Theme.c.easingPicker.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeTiny
        }

        Flow {
            visible: root.curveMode || root.userPresets.length > 0
            width: body.width
            spacing: root.tileSpacing

            EasingTile {
                visible: root.curveMode
                preset: ({ label: qsTr("+ New curve"), samples: [] })
                width: root.tileWidth
                selected: root.draft !== null
                onActivated: {
                    Haptics.select()
                    root.startNewCurve()
                }
            }

            Repeater {
                model: root.userPresets
                delegate: EasingTile {
                    id: userTile
                    required property var modelData
                    preset: userTile.modelData
                    favoriteTab: "easings"
                    width: root.tileWidth
                    selected: root.current.presetId === userTile.modelData.id
                    onActivated: {
                        Haptics.select()
                        root.apply(userTile.modelData.spec)
                    }
                    onContextRequested: (x, y) => {
                        const p = userTile.mapToItem(root, x, y)
                        root.openUserMenu(userTile.modelData, p.x, p.y)
                    }
                }
            }
        }

        // --- Footer: save, copy / paste, apply to every segment ----------------
        Row {
            visible: root.saving
            width: body.width
            spacing: Theme.spacingXs

            ThemedTextField {
                id: saveField
                width: parent.width - saveButton.width - parent.spacing
                placeholderText: qsTr("My easing")
                onAccepted: root.finishSave()
                Keys.onEscapePressed: (event) => {
                    root.saving = false
                    event.accepted = true
                }
            }

            ThemedChip {
                id: saveButton
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Save")
                onClicked: root.finishSave()
            }
        }

        Flow {
            visible: !root.saving
            width: body.width
            spacing: Theme.spacingXs

            ThemedChip {
                text: qsTr("Save…")
                tooltip: qsTr("Keep this curve as one of your easings")
                variant: "outline"
                enabled: root.kind === "bezier" || root.kind === "function"
                onClicked: root.beginSave()
            }

            IconButton {
                visible: !root.curveMode
                glyph: Theme.icons.copy
                tooltip: qsTr("Copy easing")
                enabled: root.spec !== null
                onClicked: EditorState.copyEasingToClipboard(root.spec)
            }

            IconButton {
                visible: !root.curveMode
                glyph: Theme.icons.clipboardPaste
                tooltip: qsTr("Paste easing")
                onClicked: {
                    const pasted = EditorState.easingFromClipboard()
                    if (Object.keys(pasted).length > 0)
                        root.apply(pasted)
                }
            }

            ThemedChip {
                visible: !root.curveMode
                text: qsTr("Apply to all")
                tooltip: qsTr("Use this easing between every keyframe of %1").arg(root.propLabel)
                variant: "outline"
                enabled: root.spec !== null
                onClicked: EditorState.applyEasingToAllSegments(root.trackIndex, root.clipIndex,
                                                                root.propKey, root.spec)
            }
        }
    }

    ThemedContextMenu {
        id: userMenu
        implicitWidth: 160

        ThemedMenuItem {
            text: qsTr("Rename…")
            onTriggered: {
                root.renameId = root.menuPreset.id
                renameDialog.openWith(qsTr("Rename easing"), root.menuPreset.label)
            }
        }

        ThemedMenuItem {
            text: qsTr("Delete")
            icon.name: Theme.icons.trash
            onTriggered: EditorState.deleteEasingPreset(root.menuPreset.id)
        }
    }

    NameDialog {
        id: renameDialog
        onSubmitted: (name) => EditorState.renameEasingPreset(root.renameId, name)
    }
}
