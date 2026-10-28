pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Drift

PanelFrame {
    id: root

    property bool shown: false

    readonly property var focusItem: Window.activeFocusItem
    // Tool keys are pressed with the pointer over the preview, not this panel, so the handler stays
    // registered while the workspace shows the panel; handleAction decides per key whether to claim it.
    readonly property bool wantsActions: shown
    HoverHandler { id: panelHover }

    // What the panel edits and the masks on it. Read from the controller on the selection and
    // playback signals below rather than bound, because each call returns a fresh snapshot.
    property var target: ({ kind: "none" })
    property string hostClipId: ""
    readonly property var stackModel: EditorState.masking.stackModel
    readonly property bool hasTarget: target.kind !== "none"
    readonly property bool onAdjustment: target.kind === "adjustment"

    // Each tool's own options, shown in toolOptions while that tool is active.
    readonly property var toolOptionComponents: ({ "brush": brushOptions, "ai": aiOptions })

    // Object cutout (the AI tool). backends() is a plain call, so the list is rebuilt on addon changes.
    property var aiBackendModel: []
    property bool aiHasSam2: false
    property bool aiRuntimeReady: true
    // Refining reuses the selected cutout by default; unticked it starts a new mask instead.
    property bool aiRefine: true
    readonly property var aiPoints: EditorState.segmentation.sessionPoints
    readonly property int aiIncludeCount: aiPoints.filter(p => p.include).length
    readonly property int aiExcludeCount: aiPoints.length - aiIncludeCount
    readonly property string aiRefineMaskId: stackModel.refineMaskId

    readonly property var tools: [
        { id: "", label: qsTr("Select"), action: "selectTool", glyph: Theme.icons.mousePointer, clipOnly: false },
        { id: "rect", label: qsTr("Rectangle"), action: "gizmoRotate", glyph: Theme.icons.square, clipOnly: false },
        { id: "ellipse", label: qsTr("Ellipse"), action: "gizmoScale", glyph: Theme.icons.shapes, clipOnly: false },
        { id: "polygon", label: qsTr("Polygon"), action: "maskingPolygonTool", glyph: Theme.icons.gem, clipOnly: false },
        { id: "brush", label: qsTr("Brush"), action: "bladeTool", glyph: Theme.icons.pencil, clipOnly: true },
        { id: "ai", label: qsTr("AI object"), action: "maskingAiTool", glyph: Theme.icons.sparkles, clipOnly: true },
        { id: "tracker", label: qsTr("Tracker"), action: "gizmoMove", glyph: Theme.icons.locateFixed, clipOnly: false }
    ]

    function withShortcut(label, actionId) {
        const key = EditorState.shortcutFor(actionId)
        return key.length > 0 ? qsTr("%1 (%2)").arg(label).arg(key) : label
    }

    readonly property var viewModes: [
        { id: "off", label: qsTr("Off") },
        { id: "overlay", label: qsTr("Tint hidden") },
        { id: "matte", label: qsTr("Black & white") }
    ]

    // The auto-set preview needs no mask to exist: painting the first stroke into an empty clip
    // would otherwise hide the whole picture with nothing to show what is being painted.
    property bool viewAutoSet: false
    property bool clipAtPlayhead: false
    readonly property bool hasEnabledMask: stackModel.hasEnabled
    readonly property bool hasBrushMask: {
        stackModel.revision
        return target.kind === "clip" && EditorState.masking.hasSelectedBrushMask(target.track, target.clip)
    }
    readonly property bool paintingTool: EditorState.preview.maskingTool === "brush"
                                         || EditorState.preview.maskingTool === "ai"

    function refresh() {
        target = EditorState.masking.maskTarget()
        hostClipId = target.kind === "clip" ? EditorState.masking.clipIdAt(target.track, target.clip) : ""
        stackModel.refresh()
        clipAtPlayhead = target.kind === "none"
                         && EditorState.preview.clipsAtPlayhead().some(box => box.kind === "video" || box.kind === "image")
        // Matte on a clip with nothing to show would display the unmasked picture and look broken.
        if (!stackModel.hasEnabled && !paintingTool && EditorState.preview.maskViewMode !== "off")
            EditorState.preview.maskViewMode = "off"
    }

    function useClipAtPlayhead() {
        let pick = null
        for (const box of EditorState.preview.clipsAtPlayhead()) {
            if (box.kind === "video" || box.kind === "image")
                pick = box
        }
        if (pick)
            EditorState.selectClip(pick.track, pick.clip)
    }

    // Follows the mask's own centre with a point tracker, reusing the one already driving it.
    function trackMask(entry, direction) {
        const id = EditorState.tracking.ensureMaskTracker(entry.id, -1, -1)
        if (id === "") {
            Toasts.info(qsTr("Trackers need a video clip"))
            return
        }
        ShellBridge.selectedTrackerId = id
        if (direction === "both")
            ShellBridge.trackBoth(hostClipId, id)
        else
            EditorState.tracking.track(hostClipId, id, direction === "forward", false)
    }

    function blurArea(entry, effectId) {
        if (EditorState.masking.moveMaskToEffectLayer(entry.id, effectId) !== "")
            Toasts.info(qsTr("Blur added on an adjustment layer. Adjust the amount in the Effects tab."))
    }

    function refreshIfLive() {
        if (shown && !EditorState.playing)
            refresh()
    }

    onShownChanged: {
        if (shown)
            refresh()
        else {
            EditorState.preview.maskingTool = ""
            EditorState.preview.maskViewMode = "off"
        }
    }

    Component.onCompleted: {
        if (shown)
            refresh()
        if (wantsActions)
            ShellBridge.maskingActionHandler = root
    }

    Connections {
        target: EditorState
        function onTracksChanged() { root.refreshIfLive() }
        function onSelectionChanged() { root.refreshIfLive() }
        function onSelectedClipDataChanged() { root.refreshIfLive() }
        function onPlayheadSecondsChanged() {
            if (!EditorState.scrubbing)
                root.refreshIfLive()
        }
        function onScrubbingChanged() {
            if (!EditorState.scrubbing)
                root.refreshIfLive()
        }
        function onPlayingChanged() {
            if (!EditorState.playing)
                root.refreshIfLive()
        }
    }

    Connections {
        target: EditorState.preview
        function onMaskingToolChanged() {
            const painting = root.paintingTool
            if (painting && EditorState.preview.maskViewMode === "off") {
                EditorState.preview.maskViewMode = "overlay"
                root.viewAutoSet = true
            } else if (!painting && root.viewAutoSet) {
                EditorState.preview.maskViewMode = "off"
                root.viewAutoSet = false
            }
        }
    }

    Connections {
        target: Addons
        function onKindChanged(kind) {
            if (kind === "sam2-model" || kind === "rvm-model" || kind === "onnxruntime")
                root.refreshAiBackends()
        }
    }

    Connections {
        target: EditorState.segmentation
        function onFinished(ok, message) {
            if (!ok && message !== "")
                Toasts.error(message)
        }
    }

    function textFieldFocused() {
        const f = focusItem
        return !!f && f.cursorPosition !== undefined
    }

    Component {
        id: brushOptions

        Column {
            width: parent.width
            spacing: Theme.spacingSm

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: root.stackModel.cutoutFixId !== ""
                      ? qsTr("Fixing this frame of “%1”. Brush fixes don't follow motion.").arg(root.stackModel.cutoutFixName)
                      : qsTr("Brush masks are a still image. For a moving subject use AI object, or track the mask afterwards.")
                color: Theme.c.maskingPanel.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            RowLayout {
                width: parent.width
                spacing: Theme.spacingSm

                Text {
                    Layout.preferredWidth: 56
                    Layout.alignment: Qt.AlignVCenter
                    text: qsTr("Size")
                    color: Theme.c.maskingPanel.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                ThemedSlider {
                    id: brushSizeSlider
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    label: qsTr("Brush size")
                    from: 0.2
                    to: 50
                    valueFormatter: function (v) { return Number(v).toFixed(1) + " %" }
                    Binding on value {
                        when: !brushSizeSlider.pressed
                        value: EditorState.masking.brushSize * 100
                    }
                    onMoved: EditorState.masking.brushSize = value / 100
                }
            }

            RowLayout {
                width: parent.width
                spacing: Theme.spacingSm

                Text {
                    Layout.preferredWidth: 56
                    Layout.alignment: Qt.AlignVCenter
                    text: qsTr("Hardness")
                    color: Theme.c.maskingPanel.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                ThemedSlider {
                    id: brushHardnessSlider
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    label: qsTr("Brush hardness")
                    from: 0
                    to: 100
                    valueFormatter: function (v) { return Math.round(v) + " %" }
                    Binding on value {
                        when: !brushHardnessSlider.pressed
                        value: EditorState.masking.brushHardness * 100
                    }
                    onMoved: EditorState.masking.brushHardness = value / 100
                }
            }

            RowLayout {
                width: parent.width
                spacing: Theme.spacingSm

                Text {
                    Layout.preferredWidth: 56
                    Layout.alignment: Qt.AlignVCenter
                    text: qsTr("Opacity")
                    color: Theme.c.maskingPanel.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                ThemedSlider {
                    id: brushOpacitySlider
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    label: qsTr("Brush opacity")
                    from: 5
                    to: 100
                    valueFormatter: function (v) { return Math.round(v) + " %" }
                    Binding on value {
                        when: !brushOpacitySlider.pressed
                        value: EditorState.masking.brushOpacity * 100
                    }
                    onMoved: EditorState.masking.brushOpacity = value / 100
                }
            }

            RowLayout {
                width: parent.width
                spacing: Theme.spacingSm

                ThemedToggleButton {
                    Layout.alignment: Qt.AlignVCenter
                    text: qsTr("Paint")
                    tooltip: root.withShortcut(qsTr("Left-drag paints"), "maskingBrushToggleErase")
                    checked: !EditorState.masking.brushErase
                    onClicked: EditorState.masking.brushErase = false
                }

                ThemedToggleButton {
                    Layout.alignment: Qt.AlignVCenter
                    text: qsTr("Erase")
                    tooltip: root.withShortcut(qsTr("Left-drag erases"), "maskingBrushToggleErase")
                    checked: EditorState.masking.brushErase
                    onClicked: EditorState.masking.brushErase = true
                }
            }

            ThemedCheckBox {
                text: qsTr("Pressure changes size")
                checked: EditorState.masking.brushPressureSize
                onToggled: EditorState.masking.brushPressureSize = checked
            }

            ThemedButton {
                visible: root.stackModel.cutoutFixId !== ""
                text: qsTr("Clear fix on this frame")
                tooltip: qsTr("Remove the brush fix from the frame at the playhead")
                enabled: {
                    root.stackModel.revision
                    EditorState.playheadSeconds
                    return EditorState.masking.hasCutoutPatchAtPlayhead(root.stackModel.cutoutFixId)
                }
                onClicked: EditorState.masking.clearCutoutPatch(root.stackModel.cutoutFixId)
            }

            RowLayout {
                visible: root.stackModel.cutoutFixId === ""
                width: parent.width
                spacing: Theme.spacingSm

                ThemedButton {
                    Layout.alignment: Qt.AlignVCenter
                    text: qsTr("Fill")
                    tooltip: root.hasBrushMask ? qsTr("Cover the whole clip with the mask")
                                               : qsTr("Start a new brush mask covering the whole clip")
                    enabled: root.target.kind === "clip"
                    onClicked: root.fillBrush(false)
                }

                ThemedButton {
                    Layout.alignment: Qt.AlignVCenter
                    text: qsTr("Clear")
                    tooltip: qsTr("Empty the mask")
                    enabled: root.hasBrushMask
                    onClicked: root.fillBrush(true)
                }
            }
        }
    }

    function refreshAiBackends() {
        const installed = EditorState.segmentation.backends()
        const model = []
        if (installed.indexOf("sam2") >= 0)
            model.push({ label: qsTr("Anything (click to pick)"), value: "sam2" })
        if (installed.indexOf("rvm") >= 0)
            model.push({ label: qsTr("People (automatic)"), value: "rvm" })
        aiBackendModel = model
        aiHasSam2 = installed.indexOf("sam2") >= 0
        aiRuntimeReady = Addons.runtimeAvailable()
    }

    function fillBrush(clear) {
        const host = EditorState.masking.maskTarget()
        if (host.kind === "clip")
            EditorState.masking.brushFill(host.track, host.clip, clear)
    }

    function propagateAi(direction, refineId) {
        const refine = refineId !== undefined ? refineId : (aiRefine ? aiRefineMaskId : "")
        const host = EditorState.masking.maskTarget()
        if (host.kind === "clip")
            EditorState.segmentation.propagate(host.track, host.clip, direction, refine)
    }

    Component {
        id: aiOptions

        Column {
            width: parent.width
            spacing: Theme.spacingSm

            Component.onCompleted: root.refreshAiBackends()

            // No cutout model yet: the download comes first, the controls once one is installed.
            Text {
                visible: root.aiBackendModel.length === 0
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("Object cutout needs a model. The people model is the smaller download.")
                color: Theme.c.maskingPanel.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            ThemedButton {
                visible: root.aiBackendModel.length === 0
                width: parent.width
                variant: "primary"
                text: root.aiRuntimeReady ? qsTr("Download people cutout (about 20 MB)")
                                          : qsTr("Install AI engine first")
                onClicked: ShellBridge.main.openAddonManager(root.aiRuntimeReady ? "rvm-model"
                                                                                 : "onnxruntime")
            }

            Column {
                visible: root.aiBackendModel.length > 1
                width: parent.width
                spacing: Theme.spacingSm

                Text {
                    text: qsTr("Cut out")
                    color: Theme.c.maskingPanel.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                ThemedComboBox {
                    id: backendBox
                    width: parent.width
                    enabled: !EditorState.segmentation.running
                    textRole: "label"
                    valueRole: "value"
                    model: root.aiBackendModel
                    currentIndex: backendBox.indexOfValue(EditorState.segmentation.backend)
                    onActivated: EditorState.segmentation.setBackend(currentValue, "")
                }
            }

            Text {
                visible: EditorState.segmentation.backendUsesPoints && root.aiBackendModel.length > 0
                width: parent.width
                wrapMode: Text.WordWrap
                text: Theme.touchInput
                      ? qsTr("Tap marks the subject, press and hold marks what to exclude. Tap a marker to remove it.")
                      : qsTr("Left-click marks the subject, right-click marks what to exclude. Click a marker to remove it.")
                color: Theme.c.maskingPanel.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            RowLayout {
                visible: EditorState.segmentation.backendUsesPoints && root.aiBackendModel.length > 0
                width: parent.width
                spacing: Theme.spacingSm

                Text {
                    text: qsTr("%n include", "", root.aiIncludeCount)
                    color: Theme.c.maskingPanel.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                Text {
                    text: qsTr("%n exclude", "", root.aiExcludeCount)
                    color: Theme.c.maskingPanel.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                Item { Layout.fillWidth: true }

                ThemedButton {
                    variant: "secondary"
                    text: qsTr("Clear points")
                    enabled: root.aiPoints.length > 0 && !EditorState.segmentation.running
                    onClicked: EditorState.segmentation.clearPoints()
                }
            }

            ThemedButton {
                visible: !EditorState.segmentation.backendUsesPoints && root.aiBackendModel.length > 0
                width: parent.width
                variant: "primary"
                text: qsTr("Cut out people")
                enabled: !EditorState.segmentation.running
                onClicked: root.propagateAi("both", "")
            }

            ThemedCheckBox {
                visible: EditorState.segmentation.backendUsesPoints && root.aiRefineMaskId !== ""
                text: qsTr("Refine selected mask")
                checked: root.aiRefine
                onToggled: root.aiRefine = checked
            }

            Column {
                visible: EditorState.segmentation.backendUsesPoints && root.aiBackendModel.length > 0
                width: parent.width
                spacing: Theme.spacingXs
                enabled: root.aiIncludeCount > 0 && !EditorState.segmentation.running

                Text {
                    visible: root.aiIncludeCount > 0
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: qsTr("This is a preview of this frame only. It becomes a mask when you cut out.")
                    color: Theme.c.maskingPanel.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                ThemedButton {
                    width: parent.width
                    variant: "primary"
                    text: qsTr("Cut out through whole clip")
                    onClicked: root.propagateAi("both")
                }

                RowLayout {
                    width: parent.width
                    spacing: Theme.spacingXs

                    ThemedButton {
                        Layout.fillWidth: true
                        text: qsTr("◀ Up to here")
                        onClicked: root.propagateAi("backward")
                    }

                    ThemedButton {
                        Layout.fillWidth: true
                        text: qsTr("From here ▶")
                        onClicked: root.propagateAi("forward")
                    }
                }
            }

            Column {
                visible: EditorState.segmentation.running
                width: parent.width
                spacing: Theme.spacingSm

                ThemedProgressBar {
                    width: parent.width
                    value: EditorState.segmentation.progress
                }

                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: EditorState.segmentation.status
                    color: Theme.c.maskingPanel.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                ThemedButton {
                    width: parent.width
                    variant: "destructive"
                    text: qsTr("Cancel")
                    onClicked: EditorState.segmentation.cancel()
                }
            }

            // SAM2 is the click-to-pick model, a much larger download than people; offered once
            // people works, and on its own when nothing is installed yet.
            ThemedButton {
                visible: root.aiRuntimeReady && !root.aiHasSam2
                width: parent.width
                variant: "secondary"
                text: qsTr("Add click-to-pick cutout (about 190 MB)")
                onClicked: ShellBridge.main.openAddonManager("sam2-model")
            }
        }
    }

    function handleAction(id) {
        if (!shown || textFieldFocused())
            return false
        // These keys are shared with the timeline tools and the 3D gizmo, so they are only ours
        // while the user is working here. Escape cancels state that exists only mid-edit.
        const engaged = !(EditorState.preview.mode === "3d")
                        && (ShellBridge.previewHovered || root.activeFocus || panelHover.hovered)
        if (!engaged && id !== "clearSelection")
            return false
        switch (id) {
        case "selectTool":
            EditorState.preview.maskingTool = ""
            return true
        case "clearSelection":
            if (EditorState.tracking.busy) {
                ShellBridge.stopTracking()
                return true
            }
            if (EditorState.segmentation.running) {
                EditorState.segmentation.cancel()
                return true
            }
            if (ShellBridge.trackerPlaceMode !== "") {
                ShellBridge.trackerPlaceMode = ""
                return true
            }
            if (ShellBridge.brushStrokeActive)
                return true
            if (ShellBridge.pendingPolygonPoints > 0) {
                ShellBridge.maskingCancelRequested()
                return true
            }
            if (EditorState.segmentation.sessionPoints.length > 0) {
                EditorState.segmentation.clearPoints()
                return true
            }
            if (EditorState.preview.maskingTool === "")
                return false
            EditorState.preview.maskingTool = ""
            return true
        case "maskingBrushSmaller":
            if (EditorState.preview.maskingTool !== "brush")
                return false
            EditorState.masking.brushSize = Math.max(0.002, EditorState.masking.brushSize / 1.25)
            return true
        case "maskingBrushLarger":
            if (EditorState.preview.maskingTool !== "brush")
                return false
            EditorState.masking.brushSize = Math.min(0.5, EditorState.masking.brushSize * 1.25)
            return true
        case "maskingBrushToggleErase":
            if (EditorState.preview.maskingTool !== "brush")
                return false
            EditorState.masking.brushErase = !EditorState.masking.brushErase
            return true
        case "gizmoMove":
            EditorState.preview.maskingTool = "tracker"
            return true
        case "gizmoRotate":
            EditorState.preview.maskingTool = "rect"
            return true
        case "gizmoScale":
            EditorState.preview.maskingTool = "ellipse"
            return true
        case "maskingPolygonTool":
            EditorState.preview.maskingTool = "polygon"
            return true
        case "bladeTool":
            EditorState.preview.maskingTool = "brush"
            return true
        case "maskingAiTool":
            EditorState.preview.maskingTool = "ai"
            return true
        }
        return false
    }

    focus: true
    activeFocusOnTab: true

    onWantsActionsChanged: {
        if (wantsActions)
            ShellBridge.maskingActionHandler = root
        else if (ShellBridge.maskingActionHandler === root)
            ShellBridge.maskingActionHandler = null
    }
    Component.onDestruction: {
        if (ShellBridge.maskingActionHandler === root)
            ShellBridge.maskingActionHandler = null
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: column.height + Theme.spacingLg * 2
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar { }

        Column {
            id: column
            x: Theme.spacingLg
            y: Theme.spacingLg
            width: flick.width - Theme.spacingLg * 2
            spacing: Theme.spacingLg

            EmptyState {
                visible: !root.hasTarget
                compact: true
                title: qsTr("No clip selected")
                hint: qsTr("Select a clip or an adjustment layer on the timeline.")
            }

            ThemedButton {
                visible: !root.hasTarget && root.clipAtPlayhead
                text: qsTr("Use clip at playhead")
                onClicked: root.useClipAtPlayhead()
            }

            Column {
                visible: root.hasTarget
                width: parent.width
                spacing: Theme.spacingXs

                Text {
                    width: parent.width
                    text: root.target.name || ""
                    elide: Text.ElideRight
                    color: Theme.c.maskingPanel.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm
                    font.weight: Font.Medium
                }
                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: root.onAdjustment ? qsTr("This layer's effects apply only inside the mask.")
                                            : qsTr("Masks keep what is inside and hide the rest. Invert to hide the inside instead.")
                    color: Theme.c.maskingPanel.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
            }

            Row {
                enabled: root.hasTarget
                spacing: Theme.spacingXs

                Repeater {
                    model: root.tools

                    delegate: IconButton {
                        required property var modelData
                        visible: !(modelData.clipOnly && root.onAdjustment)
                        glyph: modelData.glyph
                        tooltip: root.withShortcut(modelData.label, modelData.action)
                        active: EditorState.preview.maskingTool === modelData.id
                        onClicked: EditorState.preview.maskingTool = modelData.id
                    }
                }
            }

            RowLayout {
                visible: root.hasTarget && !root.onAdjustment
                spacing: Theme.spacingSm

                Text {
                    Layout.alignment: Qt.AlignVCenter
                    text: qsTr("Show mask")
                    color: Theme.c.maskingPanel.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                Repeater {
                    model: root.viewModes

                    delegate: ThemedToggleButton {
                        required property var modelData
                        text: modelData.label
                        enabled: modelData.id === "off" || root.hasEnabledMask || root.paintingTool
                        checked: EditorState.preview.maskViewMode === modelData.id
                        onClicked: {
                            root.viewAutoSet = false
                            EditorState.preview.maskViewMode = modelData.id
                        }
                    }
                }

                Text {
                    visible: !root.hasEnabledMask && !root.paintingTool
                    Layout.alignment: Qt.AlignVCenter
                    text: qsTr("Add a mask to preview it")
                    color: Theme.c.maskingPanel.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
            }

            // Each tool's own options (brush size, AI model, tracker).
            Loader {
                id: toolOptions
                objectName: "toolOptions"
                width: parent.width
                sourceComponent: root.toolOptionComponents[EditorState.preview.maskingTool] || null
            }

            Rectangle {
                visible: root.hasTarget
                width: parent.width
                height: 1
                color: Theme.c.maskingPanel.divider
            }

            Text {
                visible: root.hasTarget
                text: qsTr("Masks")
                color: Theme.c.maskingPanel.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                font.weight: Font.Medium
            }

            Text {
                visible: root.hasTarget && root.stackModel.count === 0
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("No masks yet. Pick a tool above and draw on the preview.")
                color: Theme.c.maskingPanel.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            Text {
                visible: root.hasTarget && !root.onAdjustment && root.stackModel.count === 0
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("To blur or recolour just part of the picture, put the effect on an adjustment layer above the clip and draw the mask on that layer.")
                color: Theme.c.maskingPanel.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            ThemedButton {
                visible: root.hasTarget && !root.onAdjustment && root.stackModel.count === 0
                text: qsTr("Add adjustment layer")
                onClicked: EditorState.addAdjustmentClip(-1, -1)
            }

            Repeater {
                model: root.stackModel

                delegate: Column {
                    id: maskEntry
                    required property int index
                    required property var model
                    width: column.width
                    spacing: Theme.spacingSm

                    Rectangle {
                        width: parent.width
                        height: 32
                        color: maskEntry.model.selected ? Theme.c.maskingPanel.rowSelected
                                                            : (rowHover.hovered ? Theme.c.maskingPanel.rowHover
                                                                                : "transparent")

                        HoverHandler {
                            id: rowHover
                        }

                        TapHandler {
                            onTapped: EditorState.selectClip(maskEntry.model.track, maskEntry.model.clip)
                        }

                        IconButton {
                            id: visibilityButton
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            glyph: maskEntry.model.enabled ? Theme.icons.eye : Theme.icons.eyeOff
                            tooltip: maskEntry.model.enabled ? qsTr("Hide mask") : qsTr("Show mask")
                            onClicked: EditorState.masking.setMaskProperty(maskEntry.model.track,
                                                                           maskEntry.model.clip,
                                                                           "enabled",
                                                                           !maskEntry.model.enabled)
                        }

                        RowLayout {
                            id: controls
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.rightMargin: Theme.spacingSm
                            spacing: Theme.spacingXs

                            // The first mask seeds the stack, and the compositor ignores its combine op,
                            // so the combo is only offered from the second mask down.
                            ThemedComboBox {
                                id: opBox
                                visible: maskEntry.index > 0
                                Layout.preferredWidth: 96
                                Layout.alignment: Qt.AlignVCenter
                                tooltip: qsTr("How this mask combines with the ones above")
                                model: ["add", "subtract", "intersect"]
                                readonly property var labels: ({
                                    "add": qsTr("Add"),
                                    "subtract": qsTr("Subtract"),
                                    "intersect": qsTr("Intersect")
                                })
                                displayText: labels[model[currentIndex]] || model[currentIndex]
                                currentIndex: Math.max(0, model.indexOf(maskEntry.model.op))
                                onActivated: EditorState.masking.setMaskProperty(maskEntry.model.track,
                                                                                 maskEntry.model.clip,
                                                                                 "op", model[currentIndex])
                            }

                            ThemedToggleButton {
                                visible: !root.onAdjustment
                                Layout.alignment: Qt.AlignVCenter
                                text: qsTr("Invert")
                                tooltip: qsTr("Hide the inside instead of the outside")
                                checked: !!maskEntry.model.invert
                                onClicked: EditorState.masking.setMaskProperty(maskEntry.model.track,
                                                                               maskEntry.model.clip,
                                                                               "invert",
                                                                               !maskEntry.model.invert)
                            }

                            // An adjustment's mask scopes its effects, so inverting means applying them outside.
                            ThemedComboBox {
                                id: applyBox
                                visible: root.onAdjustment
                                Layout.preferredWidth: 120
                                Layout.alignment: Qt.AlignVCenter
                                tooltip: qsTr("Apply effects: Inside / Outside")
                                model: ["inside", "outside"]
                                displayText: currentIndex === 1 ? qsTr("Outside") : qsTr("Inside")
                                currentIndex: maskEntry.model.invert ? 1 : 0
                                onActivated: EditorState.masking.setMaskProperty(maskEntry.model.track,
                                                                                 maskEntry.model.clip,
                                                                                 "invert", currentIndex === 1)
                            }

                            IconButton {
                                Layout.alignment: Qt.AlignVCenter
                                glyph: Theme.icons.trash
                                tooltip: qsTr("Delete mask")
                                onClicked: EditorState.masking.removeMask(maskEntry.model.track,
                                                                          maskEntry.model.clip)
                            }
                        }

                        Text {
                            anchors.left: visibilityButton.right
                            anchors.right: controls.left
                            anchors.leftMargin: Theme.spacingXs
                            anchors.rightMargin: Theme.spacingSm
                            anchors.verticalCenter: parent.verticalCenter
                            elide: Text.ElideRight
                            text: maskEntry.model.name
                            color: maskEntry.model.enabled ? Theme.c.maskingPanel.foreground
                                                               : Theme.c.maskingPanel.foregroundMuted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs
                        }
                    }

                    Item {
                        visible: maskEntry.model.selected
                        width: parent.width
                        height: detail.height

                        Column {
                            id: detail
                            x: Theme.spacingLg
                            width: parent.width - Theme.spacingLg
                            spacing: Theme.spacingSm

                            RowLayout {
                                width: parent.width
                                spacing: Theme.spacingSm

                                Text {
                                    Layout.preferredWidth: 56
                                    Layout.alignment: Qt.AlignVCenter
                                    text: qsTr("Feather")
                                    color: Theme.c.maskingPanel.foregroundMuted
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeXs
                                }

                                ThemedSlider {
                                    id: featherSlider
                                    Layout.fillWidth: true
                                    Layout.alignment: Qt.AlignVCenter
                                    label: qsTr("Feather")
                                    from: 0
                                    to: 64
                                    valueFormatter: function (v) { return Math.round(v) + " px" }
                                    Binding on value {
                                        when: !featherSlider.pressed
                                        value: maskEntry.model.feather
                                    }
                                    onMoved: EditorState.masking.previewSetMaskProperty(maskEntry.model.track,
                                                                                        maskEntry.model.clip,
                                                                                        "feather", value)
                                    onPressedChanged: {
                                        if (!pressed)
                                            EditorState.commitPreviewDrag()
                                    }
                                }
                            }

                            Column {
                                id: trackRow
                                visible: !maskEntry.model.isMedia && maskEntry.model.shape !== "bars"
                                         && root.target.kind === "clip"
                                width: parent.width
                                spacing: Theme.spacingXs

                                readonly property bool tracked: maskEntry.model.trackerId !== ""
                                readonly property var solveModes: ["translate", "translateRotate", "translateRotateScale", "perspective"]
                                readonly property var solveLabels: ({
                                    "translate": qsTr("Position"),
                                    "translateRotate": qsTr("Position + rotation"),
                                    "translateRotateScale": qsTr("Position, rotation + size"),
                                    "perspective": qsTr("Perspective")
                                })

                                RowLayout {
                                    visible: !EditorState.tracking.busy
                                    width: parent.width
                                    spacing: Theme.spacingXs

                                    ThemedButton {
                                        Layout.fillWidth: true
                                        text: qsTr("Track ◀")
                                        tooltip: qsTr("Follow the object backward from here")
                                        onClicked: root.trackMask(maskEntry.model, "backward")
                                    }

                                    ThemedButton {
                                        Layout.fillWidth: true
                                        variant: "primary"
                                        text: qsTr("Track ⇄")
                                        tooltip: qsTr("Follow the object through the whole clip")
                                        onClicked: root.trackMask(maskEntry.model, "both")
                                    }

                                    ThemedButton {
                                        Layout.fillWidth: true
                                        text: qsTr("Track ▶")
                                        tooltip: qsTr("Follow the object forward from here")
                                        onClicked: root.trackMask(maskEntry.model, "forward")
                                    }
                                }

                                RowLayout {
                                    visible: EditorState.tracking.busy
                                    width: parent.width
                                    spacing: Theme.spacingSm

                                    ThemedProgressBar {
                                        Layout.fillWidth: true
                                        Layout.alignment: Qt.AlignVCenter
                                        value: EditorState.tracking.progress
                                    }

                                    ThemedButton {
                                        Layout.alignment: Qt.AlignVCenter
                                        variant: "destructive"
                                        text: qsTr("Stop")
                                        tooltip: qsTr("Stop tracking (Esc)")
                                        onClicked: ShellBridge.stopTracking()
                                    }
                                }

                                ConfidenceStripItem {
                                    visible: trackRow.tracked
                                    width: parent.width
                                    height: 14
                                    tracking: EditorState.tracking
                                    clipId: root.hostClipId
                                    trackerId: maskEntry.model.trackerId ?? ""
                                    emptyColor: Theme.c.trackingPanel.stripEmpty
                                    goodColor: Theme.c.trackingPanel.stripGood
                                    warnColor: Theme.c.trackingPanel.stripWarn
                                    badColor: Theme.c.trackingPanel.stripBad
                                }

                                RowLayout {
                                    visible: trackRow.tracked
                                    width: parent.width
                                    spacing: Theme.spacingSm

                                    Text {
                                        Layout.preferredWidth: 56
                                        Layout.alignment: Qt.AlignVCenter
                                        text: qsTr("Follow")
                                        color: Theme.c.maskingPanel.foregroundMuted
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fontSizeXs
                                    }

                                    ThemedComboBox {
                                        Layout.fillWidth: true
                                        Layout.alignment: Qt.AlignVCenter
                                        tooltip: qsTr("What the mask copies from the tracked object")
                                        model: trackRow.solveModes
                                        displayText: trackRow.solveLabels[model[currentIndex]] || ""
                                        currentIndex: Math.max(0, model.indexOf(maskEntry.model.solve))
                                        onActivated: (index) => {
                                            if (model[index] !== "translate" && maskEntry.model.trackerPoints < 2) {
                                                Toasts.info(qsTr("Add more points to this tracker in the Tracking panel for rotation, scale and perspective"))
                                                currentIndex = Math.max(0, model.indexOf(maskEntry.model.solve))
                                                return
                                            }
                                            EditorState.tracking.setTrackerProperty(
                                                root.hostClipId, maskEntry.model.trackerId, "solve", model[index])
                                        }
                                    }
                                }

                                Text {
                                    visible: trackRow.tracked
                                    width: parent.width
                                    wrapMode: Text.WordWrap
                                    text: qsTr("Drag the mask on the preview to correct it, then track again.")
                                    color: Theme.c.maskingPanel.foregroundMuted
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeXs
                                }
                            }

                            ThemedButton {
                                id: blurButton
                                visible: !maskEntry.model.isMedia && maskEntry.model.pinned && root.target.kind === "clip"
                                text: qsTr("Blur this area ▾")
                                tooltip: qsTr("Apply a blur or pixelate effect inside this shape")
                                onClicked: blurMenu.popup(blurButton, 0, blurButton.height)

                                ThemedContextMenu {
                                    id: blurMenu

                                    ThemedMenuItem {
                                        text: qsTr("Blur")
                                        onTriggered: root.blurArea(maskEntry.model, "builtin.effects.gaussian_blur")
                                    }

                                    ThemedMenuItem {
                                        text: qsTr("Pixelate")
                                        onTriggered: root.blurArea(maskEntry.model, "stylize.pixelate")
                                    }
                                }
                            }

                            RowLayout {
                                visible: maskEntry.model.shape === "freeform"
                                width: parent.width
                                spacing: Theme.spacingSm

                                ThemedToggleButton {
                                    Layout.alignment: Qt.AlignVCenter
                                    text: qsTr("Animate shape")
                                    tooltip: qsTr("Keep a shape key per moment; the shape interpolates between them")
                                    checked: maskEntry.model.pathKeyCount > 0
                                    onClicked: EditorState.masking.setMaskPathAnimated(maskEntry.model.track,
                                                                                       maskEntry.model.clip,
                                                                                       !(maskEntry.model.pathKeyCount > 0))
                                }

                                IconButton {
                                    visible: maskEntry.model.pathKeyCount > 0
                                    Layout.alignment: Qt.AlignVCenter
                                    glyph: Theme.icons.chevronLeft
                                    tooltip: qsTr("Previous shape key")
                                    enabled: root.stackModel.earlierShapeKey
                                    onClicked: root.stackModel.stepShapeKey(true)
                                }

                                IconButton {
                                    visible: maskEntry.model.pathKeyCount > 0
                                    Layout.alignment: Qt.AlignVCenter
                                    glyph: root.stackModel.playheadOnShapeKey ? Theme.icons.diamondMinus : Theme.icons.diamondPlus
                                    tooltip: root.stackModel.playheadOnShapeKey ? qsTr("Remove the shape key at the playhead")
                                                                     : qsTr("Add a shape key at the playhead")
                                    onClicked: root.stackModel.toggleShapeKey(maskEntry.model.id)
                                }

                                IconButton {
                                    visible: maskEntry.model.pathKeyCount > 0
                                    Layout.alignment: Qt.AlignVCenter
                                    glyph: Theme.icons.chevronRight
                                    tooltip: qsTr("Next shape key")
                                    enabled: root.stackModel.laterShapeKey
                                    onClicked: root.stackModel.stepShapeKey(false)
                                }

                                Text {
                                    visible: maskEntry.model.pathKeyCount > 0
                                    Layout.alignment: Qt.AlignVCenter
                                    text: qsTr("%n shape keys", "", root.stackModel.shapeKeyCount)
                                    color: Theme.c.maskingPanel.foregroundMuted
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeXs
                                }
                            }

                            RowLayout {
                                visible: maskEntry.model.mediaSource === "file"
                                width: parent.width
                                spacing: Theme.spacingSm

                                Text {
                                    Layout.preferredWidth: 56
                                    Layout.alignment: Qt.AlignVCenter
                                    text: qsTr("Use")
                                    color: Theme.c.maskingPanel.foregroundMuted
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeXs
                                }

                                ThemedComboBox {
                                    id: channelBox
                                    Layout.preferredWidth: 120
                                    model: ["luma", "alpha"]
                                    readonly property var labels: ({ "luma": qsTr("Brightness"), "alpha": qsTr("Transparency") })
                                    displayText: labels[model[currentIndex]] || model[currentIndex]
                                    currentIndex: Math.max(0, model.indexOf(maskEntry.model.mediaChannel))
                                    onActivated: EditorState.masking.setMaskProperty(maskEntry.model.track,
                                                                                     maskEntry.model.clip,
                                                                                     "mediaChannel", model[currentIndex])
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
