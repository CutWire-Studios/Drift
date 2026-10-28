pragma ComponentBehavior: Bound
import QtQuick
import Drift

// Inline value editor for a Keyframes tree row. A horizontal press-drag scrubs a number, a double
// click types one; bool rows get a checkbox and colour, vec2, region and enum rows are read-only.
Item {
    id: root

    required property var row
    required property int track
    required property int clip
    required property real playhead

    readonly property bool isScale: row.type === "scale"
    readonly property string kind: row.kind ?? ""
    readonly property bool scrubbable: isScale || kind === "scalar" || kind === "int"
    readonly property bool locked: EditorState.playing
    readonly property string label: row.label ?? ""

    readonly property int canvasW: {
        void EditorState.tracksRevision
        return Math.max(1, EditorState.projectFile.projectWidth())
    }
    readonly property int canvasH: {
        void EditorState.tracksRevision
        return Math.max(1, EditorState.projectFile.projectHeight())
    }
    readonly property real scaleFrom: 0.05
    readonly property real scaleTo: 4

    property int valueRevision: 0
    property bool dragging: false
    property bool typing: false
    readonly property bool editing: typing || dragging
    property real liveValue: 0
    property var scaleStart: null
    property real pressX: 0
    property real startValue: 0

    readonly property real currentValue: {
        void valueRevision
        void EditorState.tracksRevision
        void playhead
        return valueAt()
    }
    readonly property real shownValue: dragging ? liveValue : currentValue

    function trackValue(prop, def) {
        return EditorState.propertyValueAt(track, clip, prop, playhead, def)
    }

    function valueAt() {
        if (isScale)
            return EditorState.keyframes.scaleOf(trackValue("width", canvasW), trackValue("height", canvasH),
                                                 canvasW, canvasH)
        return trackValue(row.keyProp, row.base)
    }

    function formatValue(v) {
        if (isScale)
            return Math.round(v * 100) + "%"
        return Number(v).toFixed(row.decimals)
    }

    function readOnlyText() {
        void valueRevision
        void EditorState.tracksRevision
        void playhead
        const parts = []
        for (let i = 0; i < row.tracks.length; ++i)
            parts.push(Number(trackValue(row.tracks[i], 0)).toFixed(2))
        return parts.join(", ")
    }

    function swatchColor() {
        void valueRevision
        void EditorState.tracksRevision
        void playhead
        return Qt.rgba(trackValue(row.tracks[0], 0), trackValue(row.tracks[1], 0),
                       trackValue(row.tracks[2], 0), trackValue(row.tracks[3], 1))
    }

    function quantize(v) {
        let out = v
        if (!isScale && row.step > 0)
            out = Math.round(out / row.step) * row.step
        if (isScale) {
            out = Math.max(scaleFrom, Math.min(scaleTo, out))
        } else {
            out = Number(out.toFixed(Math.max(0, row.decimals)))
            if (row.hasRange)
                out = Math.max(row.min, Math.min(row.max, out))
        }
        return out
    }

    function sensitivity() {
        if (isScale)
            return 0.005
        if (row.hasRange)
            return Math.max((row.max - row.min) / 400, Math.pow(10, -row.decimals))
        return Math.max(Math.pow(10, -row.decimals), Math.abs(startValue) * 0.01)
    }

    function beginScale() {
        const cd = EditorState.selectedClipData
        scaleStart = {
            "x": trackValue("x", 0),
            "y": trackValue("y", 0),
            "w": trackValue("width", canvasW),
            "h": trackValue("height", canvasH),
            "scale": currentValue,
            "rot": trackValue("rotation", 0) * Math.PI / 180,
            "pixelSize": (cd.textStyle && cd.textStyle.pixelSize) || 64
        }
    }

    function previewScale(value) {
        EditorState.keyframes.previewScale(track, clip, scaleStart, value)
    }

    function preview(value) {
        liveValue = value
        if (isScale)
            previewScale(value)
        else
            EditorState.previewSetClipKeyframe(track, clip, row.keyProp, EditorState.playheadSeconds, value)
    }

    function beginDrag() {
        dragging = true
        liveValue = currentValue
        if (isScale)
            beginScale()
        EditorState.beginPreviewDrag(qsTr("Change %1").arg(label))
    }

    function endDrag() {
        if (!dragging)
            return
        EditorState.commitPreviewDrag()
        dragging = false
    }

    function commitValue(v) {
        if (isNaN(v))
            return
        if (isScale) {
            beginScale()
            EditorState.beginPreviewDrag(qsTr("Change %1").arg(label))
            previewScale(quantize(v))
            EditorState.commitPreviewDrag()
            return
        }
        EditorState.setClipKeyframe(track, clip, row.keyProp, EditorState.playheadSeconds, quantize(v))
    }

    function openEditor() {
        if (!scrubbable || locked)
            return
        typing = true
        field.text = isScale ? String(Math.round(currentValue * 100)) : Number(currentValue).toFixed(row.decimals)
        field.forceActiveFocus()
        field.selectAll()
    }

    function finishTyping(apply) {
        if (!typing)
            return
        typing = false
        if (apply) {
            const v = parseFloat(field.text)
            commitValue(isScale ? v / 100 : v)
        }
    }

    implicitWidth: 88
    implicitHeight: Theme.controlHeightSm

    Component.onDestruction: endDrag()
    onClipChanged: {
        if (dragging) {
            EditorState.cancelPreviewDrag()
            dragging = false
        }
    }

    Connections {
        target: EditorState
        function onClipPropertiesPreviewed() {
            if (!root.dragging)
                root.valueRevision++
        }
    }

    Rectangle {
        visible: root.scrubbable
        anchors.fill: parent
        radius: Theme.c.keyframesScrubber.radius
        color: area.containsMouse || root.dragging ? Theme.c.keyframesScrubber.backgroundHover
                                                    : Theme.c.keyframesScrubber.background
        opacity: root.locked ? 0.5 : 1

        Rectangle {
            visible: root.dragging
            anchors.fill: parent
            radius: parent.radius
            color: "transparent"
            border.width: Theme.c.keyframesScrubber.borderWidth
            border.color: Theme.c.keyframesScrubber.border
        }

        Text {
            anchors.centerIn: parent
            width: parent.width - Theme.spacingMd
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            text: root.formatValue(root.shownValue)
            color: Theme.c.keyframesScrubber.foreground
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        MouseArea {
            id: area
            anchors.fill: parent
            enabled: !root.locked && !root.typing
            hoverEnabled: true
            preventStealing: true
            cursorShape: Qt.SizeHorCursor
            onPressed: (mouse) => {
                root.pressX = mouse.x
                root.startValue = root.currentValue
            }
            onPositionChanged: (mouse) => {
                if (!pressed)
                    return
                const dx = mouse.x - root.pressX
                if (!root.dragging && Math.abs(dx) < 3)
                    return
                if (!root.dragging)
                    root.beginDrag()
                root.preview(root.quantize(root.startValue + dx * root.sensitivity()))
            }
            onReleased: root.endDrag()
            onCanceled: root.endDrag()
            onDoubleClicked: root.openEditor()
            onWheel: (wheel) => { wheel.accepted = false }
        }

        ThemedTextField {
            id: field
            visible: root.typing
            anchors.fill: parent
            topPadding: 0
            bottomPadding: 0
            horizontalAlignment: TextInput.AlignHCenter
            verticalAlignment: TextInput.AlignVCenter
            font.pixelSize: Theme.fontSizeXs
            implicitHeight: parent.height
            Keys.onEscapePressed: root.finishTyping(false)
            onAccepted: root.finishTyping(true)
            onActiveFocusChanged: {
                if (!activeFocus)
                    root.finishTyping(true)
            }
        }
    }

    ThemedCheckBox {
        visible: root.kind === "bool"
        anchors.verticalCenter: parent.verticalCenter
        checked: root.currentValue > 0.5
        enabled: !root.locked
        onToggled: EditorState.setClipKeyframe(root.track, root.clip, root.row.keyProp,
                                               EditorState.playheadSeconds, checked ? 1 : 0)
    }

    Rectangle {
        visible: root.kind === "color"
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        height: parent.height - Theme.spacingXs
        radius: Theme.c.keyframesScrubber.radius
        color: root.kind === "color" ? root.swatchColor() : "transparent"
        border.width: Theme.c.panelFrame.borderWidth
        border.color: Theme.c.keyframesTreeRow.border
    }

    Text {
        visible: root.kind === "vec2" || root.kind === "region" || root.kind === "enum"
        anchors.fill: parent
        verticalAlignment: Text.AlignVCenter
        horizontalAlignment: Text.AlignRight
        elide: Text.ElideRight
        text: visible ? root.readOnlyText() : ""
        color: Theme.c.keyframesScrubber.foregroundMuted
        font.family: Theme.monoFontFamily
        font.pixelSize: Theme.fontSizeXs
    }
}
