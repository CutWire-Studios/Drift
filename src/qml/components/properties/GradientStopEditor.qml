import QtQuick
import QtQuick.Dialogs
import Drift
import ".."

// Gradient paint editor for one text layer: a live preview bar, draggable stop chips, a preset
// strip and the mapping controls. Stop drags stream through previewSetTextLayer and are closed
// as a single undo step; everything else commits directly.
Column {
    id: root

    property string layerId: ""
    property var gradient: ({})
    // The whole style, for the keyframe rows (their points live in textStyle.keyframes).
    property var textStyle: ({})

    readonly property var stops: (gradient && gradient.stops) || []
    readonly property var presets: EditorState.textGradientPresets()

    // A working copy of the stops while one is being dragged, so the preview and the chips
    // follow the finger without waiting for the project round-trip.
    property int dragIndex: -1
    property var dragStops: []
    readonly property var shownStops: dragIndex >= 0 ? dragStops : stops

    spacing: Theme.spacingMd

    function keyframes(field) {
        const keys = root.textStyle && root.textStyle.keyframes
        const entry = keys && keys["layer." + root.layerId + ".gradient." + field]
        return (entry && entry.points) || []
    }
    function prop(field, label, decimals) {
        return { "key": "text.layer." + root.layerId + ".gradient." + field, "label": label,
                 "def": Number(root.gradient[field]) || 0, "decimals": decimals }
    }
    function cloneStops(list) {
        return list.map(s => ({ "pos": Number(s.pos), "color": String(s.color) }))
    }
    function commitGradient(patch) {
        EditorState.setTextLayer(EditorState.selectedTrack, EditorState.selectedClip, root.layerId,
                                 { "paint": { "gradient": patch } })
    }
    function previewGradient(patch) {
        EditorState.previewSetTextLayer(EditorState.selectedTrack, EditorState.selectedClip, root.layerId,
                                        { "paint": { "gradient": patch } })
    }
    function toQtColor(hex) {
        const h = String(hex).replace("#", "")
        if (h.length === 8)
            return Qt.rgba(parseInt(h.substr(2, 2), 16) / 255, parseInt(h.substr(4, 2), 16) / 255,
                           parseInt(h.substr(6, 2), 16) / 255, parseInt(h.substr(0, 2), 16) / 255)
        if (h.length === 6)
            return Qt.rgba(parseInt(h.substr(0, 2), 16) / 255, parseInt(h.substr(2, 2), 16) / 255,
                           parseInt(h.substr(4, 2), 16) / 255, 1)
        return Qt.rgba(1, 1, 1, 1)
    }
    function paintBar(ctx, w, h, list) {
        ctx.clearRect(0, 0, w, h)
        const sorted = root.cloneStops(list).sort((a, b) => a.pos - b.pos)
        if (sorted.length === 0)
            return
        const grad = ctx.createLinearGradient(0, 0, w, 0)
        for (let i = 0; i < sorted.length; ++i)
            grad.addColorStop(Math.max(0, Math.min(1, sorted[i].pos)), root.toQtColor(sorted[i].color))
        ctx.fillStyle = grad
        ctx.fillRect(0, 0, w, h)
    }
    function addStop() {
        const sorted = root.cloneStops(root.stops).sort((a, b) => a.pos - b.pos)
        if (sorted.length >= 5)
            return
        let bestGap = -1
        let at = 0.5
        let color = sorted.length > 0 ? sorted[0].color : "#ffffffff"
        for (let i = 0; i + 1 < sorted.length; ++i) {
            const gap = sorted[i + 1].pos - sorted[i].pos
            if (gap > bestGap) {
                bestGap = gap
                at = (sorted[i].pos + sorted[i + 1].pos) / 2
                color = sorted[i].color
            }
        }
        sorted.push({ "pos": at, "color": color })
        root.commitGradient({ "stops": sorted })
    }
    function removeStop(index) {
        if (root.stops.length <= 2)
            return
        const next = root.cloneStops(root.stops)
        next.splice(index, 1)
        root.commitGradient({ "stops": next })
    }

    Canvas {
        id: previewBar
        width: parent.width
        height: 28
        onPaint: {
            const ctx = getContext("2d")
            root.paintBar(ctx, width, height, root.shownStops)
        }
        onWidthChanged: requestPaint()
        Connections {
            target: root
            function onShownStopsChanged() { previewBar.requestPaint() }
        }

        Rectangle {
            anchors.fill: parent
            color: "transparent"
            radius: Theme.radiusSm
            border.width: Theme.borderWidth
            border.color: Theme.panelBorder
        }
    }

    // Stop chips ride a strip under the bar; each hit area is 32 px so a finger can land on it.
    Item {
        id: stopStrip
        width: parent.width
        height: 32

        Repeater {
            model: root.shownStops.length
            delegate: Item {
                id: stopChip
                required property int index
                readonly property var stop: root.shownStops[index] || ({ "pos": 0, "color": "#ffffffff" })
                readonly property bool dragging: root.dragIndex === index
                width: 32
                height: 32
                x: Math.round(Math.max(0, Math.min(1, Number(stop.pos))) * stopStrip.width) - width / 2
                z: dragging ? 2 : 1

                Rectangle {
                    anchors.centerIn: parent
                    width: 16
                    height: 16
                    radius: 8
                    color: stopChip.stop.color
                    border.width: stopChip.dragging || chipMouse.containsMouse ? Theme.borderWidthFocus : Theme.borderWidth
                    border.color: stopChip.dragging || chipMouse.containsMouse ? Theme.primary : Theme.panelForeground
                    scale: stopChip.dragging ? 1.15 : 1
                    Behavior on scale {
                        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                    }
                }

                ThemedToolTip {
                    text: qsTr("Drag to move, tap for colour, hold or right-click to remove")
                    visible: chipMouse.containsMouse && !stopChip.dragging
                }

                MouseArea {
                    id: chipMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    cursorShape: Qt.PointingHandCursor
                    preventStealing: true
                    property real pressX: 0
                    property bool moved: false
                    property bool removed: false

                    onPressed: mouse => {
                        moved = false
                        removed = false
                        if (mouse.button === Qt.RightButton) {
                            removed = true
                            root.removeStop(stopChip.index)
                            return
                        }
                        pressX = mapToItem(stopStrip, mouse.x, 0).x
                    }
                    onPositionChanged: mouse => {
                        if (!pressed || removed || !(mouse.buttons & Qt.LeftButton))
                            return
                        const sx = mapToItem(stopStrip, mouse.x, 0).x
                        if (!moved && Math.abs(sx - pressX) < 3)
                            return
                        if (!moved) {
                            moved = true
                            root.dragStops = root.cloneStops(root.stops)
                            root.dragIndex = stopChip.index
                            EditorState.beginPreviewDrag(qsTr("Move gradient stop"))
                        }
                        const next = root.cloneStops(root.dragStops)
                        next[stopChip.index].pos = Math.max(0, Math.min(1, sx / stopStrip.width))
                        root.dragStops = next
                        root.previewGradient({ "stops": next })
                    }
                    onReleased: mouse => {
                        if (removed)
                            return
                        if (moved) {
                            root.previewGradient({ "stops": root.dragStops })
                            root.dragIndex = -1
                            EditorState.commitPreviewDrag()
                            return
                        }
                        if (mouse.button === Qt.LeftButton) {
                            stopColorDialog.targetIndex = stopChip.index
                            stopColorDialog.selectedColor = stopChip.stop.color
                            stopColorDialog.open()
                        }
                    }
                    onCanceled: {
                        if (moved) {
                            root.dragIndex = -1
                            EditorState.cancelPreviewDrag()
                        }
                    }
                    onPressAndHold: {
                        if (moved || removed)
                            return
                        removed = true
                        root.removeStop(stopChip.index)
                    }
                }
            }
        }

        IconButton {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            glyph: Theme.icons.plus
            variant: "ghost"
            buttonSize: Theme.controlHeightSm
            iconSize: 12
            visible: root.stops.length < 5
            tooltip: qsTr("Add a colour stop")
            onClicked: root.addStop()
        }
    }

    Text {
        text: qsTr("Presets")
        color: Theme.mutedForeground
        font.pixelSize: Theme.fontSizeXs
        font.family: Theme.fontFamily
    }

    Flickable {
        width: parent.width
        height: 26
        contentWidth: presetRow.width
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        interactive: contentWidth > width

        Row {
            id: presetRow
            spacing: Theme.spacingSm

            Repeater {
                model: root.presets
                delegate: Canvas {
                    id: presetChip
                    required property var modelData
                    width: 44
                    height: 24
                    onPaint: {
                        const ctx = getContext("2d")
                        root.paintBar(ctx, width, height, presetChip.modelData.stops || [])
                    }

                    Rectangle {
                        anchors.fill: parent
                        color: "transparent"
                        radius: Theme.radiusXs
                        border.width: presetHover.hovered ? Theme.borderWidthFocus : Theme.borderWidth
                        border.color: presetHover.hovered ? Theme.primary : Theme.panelBorder
                    }
                    HoverHandler { id: presetHover }
                    ThemedToolTip {
                        text: presetChip.modelData.label
                        visible: presetHover.hovered
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.commitGradient({ "kind": presetChip.modelData.kind,
                                                         "angle": presetChip.modelData.angle,
                                                         "stops": root.cloneStops(presetChip.modelData.stops || []) })
                    }
                }
            }
        }
    }

    Row {
        width: parent.width
        spacing: 8

        Column {
            width: (parent.width - parent.spacing) / 2
            spacing: 4
            Text {
                text: qsTr("Type")
                color: Theme.mutedForeground
                font.pixelSize: Theme.fontSizeXs
                font.family: Theme.fontFamily
            }
            ThemedComboBox {
                width: parent.width
                readonly property var kinds: ["linear", "radial", "sweep"]
                model: [qsTr("Linear"), qsTr("Radial"), qsTr("Sweep")]
                currentIndex: Math.max(0, kinds.indexOf(root.gradient.kind || "linear"))
                onActivated: root.commitGradient({ "kind": kinds[currentIndex] })
            }
        }

        PropertyKeyframeRow {
            width: (parent.width - parent.spacing) / 2
            propDef: root.prop("angle", qsTr("Angle"), 0)
            keyframeList: root.keyframes("angle")
            useSlider: true
            sliderFrom: 0
            sliderTo: 360
            unit: "°"
        }
    }

    Row {
        width: parent.width
        spacing: 8

        Column {
            width: (parent.width - parent.spacing) / 2
            spacing: 4
            Text {
                text: qsTr("Map to")
                HoverHandler { id: mapHover }
                ThemedToolTip {
                    text: qsTr("What one run of the gradient spans: the whole block, each line, word or glyph")
                    visible: mapHover.hovered
                }
                color: Theme.mutedForeground
                font.pixelSize: Theme.fontSizeXs
                font.family: Theme.fontFamily
            }
            ThemedComboBox {
                width: parent.width
                readonly property var spaces: ["block", "line", "word", "glyph", "accentRun"]
                model: [qsTr("Block"), qsTr("Line"), qsTr("Word"), qsTr("Glyph"), qsTr("Accent run")]
                currentIndex: Math.max(0, spaces.indexOf(root.gradient.space || "block"))
                onActivated: root.commitGradient({ "space": spaces[currentIndex] })
            }
        }

        PropertyKeyframeRow {
            width: (parent.width - parent.spacing) / 2
            propDef: root.prop("offset", qsTr("Offset"), 2)
            keyframeList: root.keyframes("offset")
            useSlider: true
            sliderFrom: -1
            sliderTo: 1
        }
    }

    Row {
        width: parent.width
        spacing: 8

        Column {
            width: (parent.width - parent.spacing) / 2
            spacing: 4
            Text {
                text: qsTr("Speed")
                HoverHandler { id: speedHover }
                ThemedToolTip {
                    text: qsTr("Slides the gradient along its axis, in cycles per second")
                    visible: speedHover.hovered
                }
                color: Theme.mutedForeground
                font.pixelSize: Theme.fontSizeXs
                font.family: Theme.fontFamily
            }
            ThemedNumberField {
                id: speedField
                width: parent.width
                decimals: 2
                step: 0.05
                from: -10
                to: 10
                unit: "/s"
                Binding on value {
                    when: !speedField.activeFocus
                    value: Number(root.gradient.offsetSpeed) || 0
                }
                onEdited: v => root.commitGradient({ "offsetSpeed": v })
            }
        }

        Column {
            width: (parent.width - parent.spacing) / 2
            spacing: 4
            Text {
                text: qsTr("Options")
                color: Theme.mutedForeground
                font.pixelSize: Theme.fontSizeXs
                font.family: Theme.fontFamily
            }
            Row {
                spacing: Theme.spacingSm
                ThemedToggleButton {
                    text: qsTr("Repeat")
                    checked: root.gradient.repeat === true
                    tooltip: qsTr("Tile the gradient past its ends instead of clamping")
                    onClicked: root.commitGradient({ "repeat": !(root.gradient.repeat === true) })
                }
                ThemedToggleButton {
                    text: qsTr("OKLab")
                    checked: root.gradient.oklab === true
                    tooltip: qsTr("Blend stops in OKLab for even, muddy-free transitions")
                    onClicked: root.commitGradient({ "oklab": !(root.gradient.oklab === true) })
                }
            }
        }
    }

    ColorDialog {
        id: stopColorDialog
        title: qsTr("Stop colour")
        property int targetIndex: -1

        function toHex(c) {
            const pad = function (v) {
                const h = Math.round(v * 255).toString(16)
                return h.length === 1 ? "0" + h : h
            }
            return "#" + pad(c.a) + pad(c.r) + pad(c.g) + pad(c.b)
        }

        onAccepted: {
            if (targetIndex < 0 || targetIndex >= root.stops.length)
                return
            const next = root.cloneStops(root.stops)
            next[targetIndex].color = toHex(selectedColor)
            root.commitGradient({ "stops": next })
        }
    }
}
