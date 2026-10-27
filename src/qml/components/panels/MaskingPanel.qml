pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Drift

PanelFrame {
    id: root

    property bool shown: false

    readonly property var focusItem: Window.activeFocusItem
    // Tool keys are pressed with the pointer over the preview, not this panel, so the handler is
    // claimed for as long as the workspace shows the panel.
    readonly property bool wantsActions: shown

    // What the panel edits and the masks on it. Read from the controller on the selection and
    // playback signals below rather than bound, because each call returns a fresh snapshot.
    property var target: ({ kind: "none" })
    property var stack: []
    // Shape key times (timeline seconds) of the selected freeform mask, refreshed with the stack.
    property var shapeKeyTimes: []
    readonly property real shapeKeyHalfFrame: 0.5 / Math.max(1, EditorState.projectFile.projectFps())
    readonly property real shapeKeyEpsilon: 0.001
    readonly property bool earlierShapeKey: shapeKeyTimes.some(k => k < EditorState.playheadSeconds - shapeKeyEpsilon)
    readonly property bool laterShapeKey: shapeKeyTimes.some(k => k > EditorState.playheadSeconds + shapeKeyEpsilon)
    readonly property bool playheadOnShapeKey: shapeKeyTimes.some(k => Math.abs(k - EditorState.playheadSeconds) <= shapeKeyHalfFrame)
    readonly property bool hasTarget: target.kind !== "none"
    readonly property bool onAdjustment: target.kind === "adjustment"

    // Each tool's own options, shown in toolOptions while that tool is active.
    readonly property var toolOptionComponents: ({ "brush": brushOptions })

    readonly property var tools: [
        { id: "", label: qsTr("Select"), key: "V", glyph: Theme.icons.mousePointer, clipOnly: false },
        { id: "rect", label: qsTr("Rectangle"), key: "R", glyph: Theme.icons.square, clipOnly: false },
        { id: "ellipse", label: qsTr("Ellipse"), key: "E", glyph: Theme.icons.shapes, clipOnly: false },
        { id: "polygon", label: qsTr("Polygon"), key: "P", glyph: Theme.icons.gem, clipOnly: false },
        { id: "brush", label: qsTr("Brush"), key: "B", glyph: Theme.icons.pencil, clipOnly: true },
        { id: "ai", label: qsTr("AI object"), key: "A", glyph: Theme.icons.sparkles, clipOnly: true }
    ]

    readonly property var viewModes: [
        { id: "off", label: qsTr("Off") },
        { id: "overlay", label: qsTr("Overlay") },
        { id: "matte", label: qsTr("Matte") }
    ]

    function refresh() {
        target = EditorState.masking.maskTarget()
        stack = target.kind === "none" ? [] : EditorState.masking.maskStack(target.track, target.clip)
        const selected = stack.find(e => e.selected && e.shape === "freeform")
        shapeKeyTimes = selected ? EditorState.masking.maskPathKeyTimes(selected.track, selected.clip) : []
    }

    function stepShapeKey(previous) {
        const t = EditorState.playheadSeconds
        const candidates = shapeKeyTimes.filter(k => previous ? k < t - shapeKeyEpsilon : k > t + shapeKeyEpsilon)
        if (candidates.length > 0)
            EditorState.playheadSeconds = previous ? candidates[candidates.length - 1] : candidates[0]
    }

    // Adds a key holding the shape on screen at the playhead, or removes the key already there.
    function toggleShapeKey(track, clip) {
        if (playheadOnShapeKey) {
            EditorState.masking.removeMaskPathKey(track, clip, EditorState.playheadSeconds)
            return
        }
        const layer = EditorState.preview.maskEditorState().layers.find(l => l.track === track && l.clip === clip)
        if (layer)
            EditorState.masking.setMaskPathKey(track, clip, EditorState.playheadSeconds, layer.mask.points)
    }

    function refreshIfLive() {
        if (shown && !EditorState.playing)
            refresh()
    }

    onShownChanged: {
        if (shown)
            refresh()
        else
            EditorState.preview.maskingTool = ""
    }

    Component.onCompleted: if (shown) refresh()

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

    function textFieldFocused() {
        const f = focusItem
        return !!f && f.cursorPosition !== undefined
    }

    Component {
        id: brushOptions

        Column {
            width: parent.width
            spacing: Theme.spacingSm

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
                    tooltip: qsTr("Left-drag paints")
                    checked: !EditorState.masking.brushErase
                    onClicked: EditorState.masking.brushErase = false
                }

                ThemedToggleButton {
                    Layout.alignment: Qt.AlignVCenter
                    text: qsTr("Erase")
                    tooltip: qsTr("Left-drag erases")
                    checked: EditorState.masking.brushErase
                    onClicked: EditorState.masking.brushErase = true
                }
            }

            RowLayout {
                width: parent.width
                spacing: Theme.spacingSm

                ThemedButton {
                    Layout.alignment: Qt.AlignVCenter
                    text: qsTr("Fill")
                    tooltip: qsTr("Cover the whole clip with the mask")
                    enabled: root.target.kind === "clip"
                    onClicked: EditorState.masking.brushFill(root.target.track, root.target.clip, false)
                }

                ThemedButton {
                    Layout.alignment: Qt.AlignVCenter
                    text: qsTr("Clear")
                    tooltip: qsTr("Empty the mask")
                    enabled: root.target.kind === "clip"
                    onClicked: EditorState.masking.brushFill(root.target.track, root.target.clip, true)
                }
            }
        }
    }

    function handleAction(id) {
        if (!shown || textFieldFocused())
            return false
        switch (id) {
        case "selectTool":
            EditorState.preview.maskingTool = ""
            return true
        case "clearSelection":
            if (ShellBridge.pendingPolygonPoints > 0) {
                ShellBridge.maskingCancelRequested()
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
                    text: root.onAdjustment ? qsTr("Adjustment layer mask") : qsTr("Masks on this clip")
                    color: Theme.c.maskingPanel.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
            }

            Row {
                visible: root.hasTarget
                spacing: Theme.spacingXs

                Repeater {
                    model: root.tools

                    delegate: IconButton {
                        required property var modelData
                        visible: !(modelData.clipOnly && root.onAdjustment)
                        glyph: modelData.glyph
                        tooltip: modelData.label + " (" + modelData.key + ")"
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
                    text: qsTr("Preview")
                    color: Theme.c.maskingPanel.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                Repeater {
                    model: root.viewModes

                    delegate: ThemedToggleButton {
                        required property var modelData
                        text: modelData.label
                        checked: EditorState.preview.maskViewMode === modelData.id
                        onClicked: EditorState.preview.maskViewMode = modelData.id
                    }
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
                visible: root.hasTarget && root.stack.length === 0
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("No masks yet. Pick a tool above and draw on the preview.")
                color: Theme.c.maskingPanel.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            Repeater {
                model: root.stack

                delegate: Column {
                    id: maskEntry
                    required property int index
                    required property var modelData
                    width: column.width
                    spacing: Theme.spacingSm

                    Rectangle {
                        width: parent.width
                        height: 32
                        color: maskEntry.modelData.selected ? Theme.c.maskingPanel.rowSelected
                                                            : (rowHover.hovered ? Theme.c.maskingPanel.rowHover
                                                                                : "transparent")

                        HoverHandler {
                            id: rowHover
                        }

                        TapHandler {
                            onTapped: EditorState.selectClip(maskEntry.modelData.track, maskEntry.modelData.clip)
                        }

                        IconButton {
                            id: visibilityButton
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            glyph: maskEntry.modelData.enabled ? Theme.icons.eye : Theme.icons.eyeOff
                            tooltip: maskEntry.modelData.enabled ? qsTr("Hide mask") : qsTr("Show mask")
                            onClicked: EditorState.masking.setMaskProperty(maskEntry.modelData.track,
                                                                           maskEntry.modelData.clip,
                                                                           "enabled",
                                                                           !maskEntry.modelData.enabled)
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
                                model: ["add", "subtract", "intersect"]
                                readonly property var labels: ({
                                    "add": qsTr("Add"),
                                    "subtract": qsTr("Subtract"),
                                    "intersect": qsTr("Intersect")
                                })
                                displayText: labels[model[currentIndex]] || model[currentIndex]
                                currentIndex: Math.max(0, model.indexOf(maskEntry.modelData.op))
                                onActivated: EditorState.masking.setMaskProperty(maskEntry.modelData.track,
                                                                                 maskEntry.modelData.clip,
                                                                                 "op", model[currentIndex])
                            }

                            ThemedToggleButton {
                                visible: !root.onAdjustment
                                Layout.alignment: Qt.AlignVCenter
                                text: qsTr("Inv")
                                tooltip: qsTr("Invert this mask")
                                checked: !!maskEntry.modelData.invert
                                onClicked: EditorState.masking.setMaskProperty(maskEntry.modelData.track,
                                                                               maskEntry.modelData.clip,
                                                                               "invert",
                                                                               !maskEntry.modelData.invert)
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
                                currentIndex: maskEntry.modelData.invert ? 1 : 0
                                onActivated: EditorState.masking.setMaskProperty(maskEntry.modelData.track,
                                                                                 maskEntry.modelData.clip,
                                                                                 "invert", currentIndex === 1)
                            }

                            IconButton {
                                Layout.alignment: Qt.AlignVCenter
                                glyph: Theme.icons.trash
                                tooltip: qsTr("Delete mask")
                                onClicked: EditorState.masking.removeMask(maskEntry.modelData.track,
                                                                          maskEntry.modelData.clip)
                            }
                        }

                        Text {
                            anchors.left: visibilityButton.right
                            anchors.right: controls.left
                            anchors.leftMargin: Theme.spacingXs
                            anchors.rightMargin: Theme.spacingSm
                            anchors.verticalCenter: parent.verticalCenter
                            elide: Text.ElideRight
                            text: maskEntry.modelData.name
                            color: maskEntry.modelData.enabled ? Theme.c.maskingPanel.foreground
                                                               : Theme.c.maskingPanel.foregroundMuted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs
                        }
                    }

                    Item {
                        visible: maskEntry.modelData.selected
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
                                        value: maskEntry.modelData.feather
                                    }
                                    onMoved: EditorState.masking.previewSetMaskProperty(maskEntry.modelData.track,
                                                                                        maskEntry.modelData.clip,
                                                                                        "feather", value)
                                    onPressedChanged: {
                                        if (!pressed)
                                            EditorState.commitPreviewDrag()
                                    }
                                }
                            }

                            RowLayout {
                                visible: maskEntry.modelData.shape === "freeform"
                                width: parent.width
                                spacing: Theme.spacingSm

                                ThemedToggleButton {
                                    Layout.alignment: Qt.AlignVCenter
                                    text: qsTr("Animate shape")
                                    tooltip: qsTr("Keep a shape key per moment; the shape interpolates between them")
                                    checked: maskEntry.modelData.pathKeyCount > 0
                                    onClicked: EditorState.masking.setMaskPathAnimated(maskEntry.modelData.track,
                                                                                       maskEntry.modelData.clip,
                                                                                       !(maskEntry.modelData.pathKeyCount > 0))
                                }

                                IconButton {
                                    visible: maskEntry.modelData.pathKeyCount > 0
                                    Layout.alignment: Qt.AlignVCenter
                                    glyph: Theme.icons.chevronLeft
                                    tooltip: qsTr("Previous shape key")
                                    enabled: root.earlierShapeKey
                                    onClicked: root.stepShapeKey(true)
                                }

                                IconButton {
                                    visible: maskEntry.modelData.pathKeyCount > 0
                                    Layout.alignment: Qt.AlignVCenter
                                    glyph: root.playheadOnShapeKey ? Theme.icons.diamondMinus : Theme.icons.diamondPlus
                                    tooltip: root.playheadOnShapeKey ? qsTr("Remove the shape key at the playhead")
                                                                     : qsTr("Add a shape key at the playhead")
                                    onClicked: root.toggleShapeKey(maskEntry.modelData.track, maskEntry.modelData.clip)
                                }

                                IconButton {
                                    visible: maskEntry.modelData.pathKeyCount > 0
                                    Layout.alignment: Qt.AlignVCenter
                                    glyph: Theme.icons.chevronRight
                                    tooltip: qsTr("Next shape key")
                                    enabled: root.laterShapeKey
                                    onClicked: root.stepShapeKey(false)
                                }

                                Text {
                                    visible: maskEntry.modelData.pathKeyCount > 0
                                    Layout.alignment: Qt.AlignVCenter
                                    text: qsTr("%n shape keys", "", root.shapeKeyTimes.length)
                                    color: Theme.c.maskingPanel.foregroundMuted
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeXs
                                }
                            }

                            RowLayout {
                                visible: !!maskEntry.modelData.isMedia
                                width: parent.width
                                spacing: Theme.spacingSm

                                Text {
                                    Layout.preferredWidth: 56
                                    Layout.alignment: Qt.AlignVCenter
                                    text: qsTr("Channel")
                                    color: Theme.c.maskingPanel.foregroundMuted
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeXs
                                }

                                ThemedComboBox {
                                    id: channelBox
                                    Layout.preferredWidth: 120
                                    model: ["luma", "alpha"]
                                    readonly property var labels: ({ "luma": qsTr("Luma"), "alpha": qsTr("Alpha") })
                                    displayText: labels[model[currentIndex]] || model[currentIndex]
                                    currentIndex: Math.max(0, model.indexOf(maskEntry.modelData.mediaChannel))
                                    onActivated: EditorState.masking.setMaskProperty(maskEntry.modelData.track,
                                                                                     maskEntry.modelData.clip,
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
