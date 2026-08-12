import QtQuick
import Drift
import ".."

// One cutout on a track's mask lane. It behaves like a clip — drag to move, grab an edge to trim,
// click to select — but it lives on the sub-lane above the track's clips and masks whichever of
// them it overlaps. It is not owned by any clip: splitting or deleting a clip leaves it alone.
Item {
    id: maskItem

    // Owning TimelinePanel, for pxPerSecond and the drag/snap helpers.
    property var panel
    property int trackIndex: 0
    required property int index
    required property var modelData

    readonly property int maskIndex: index
    readonly property bool selected: EditorState.selectedMaskTrack === trackIndex
                                     && EditorState.selectedMaskIndex === maskIndex
    readonly property real laneHeight: panel.maskLaneRowHeight
    readonly property bool enabled_: modelData.enabled !== false

    // Live values during a drag, so the bar tracks the cursor without waiting for the model.
    property real liveStart: -1
    property real liveDuration: -1
    property int liveLane: -1
    readonly property real barStart: liveStart >= 0 ? liveStart : modelData.start
    readonly property real barDuration: liveDuration >= 0 ? liveDuration : modelData.duration
    readonly property int barLane: liveLane >= 0 ? liveLane : (modelData.lane || 0)

    // Subtract cuts into what came before it and Intersect trims it, so they read as taking away;
    // colouring them apart from Add is what makes an overlapping stack legible at a glance.
    readonly property color barColor: {
        if (!enabled_)
            return Theme.mutedForeground
        switch (modelData.op) {
        case "subtract": return Theme.destructive
        case "intersect": return Theme.warning
        }
        return Theme.primary
    }

    readonly property string label: {
        if (modelData.name)
            return modelData.name
        if (modelData.shape === "media") {
            const path = modelData.mediaPath || ""
            const slash = path.lastIndexOf("/")
            return slash >= 0 ? path.substring(slash + 1) : path
        }
        const names = {
            "rectangle": qsTr("Rectangle"), "ellipse": qsTr("Ellipse"), "star": qsTr("Star"),
            "heart": qsTr("Heart"), "bars": qsTr("Bars"), "freeform": qsTr("Polygon")
        }
        return names[modelData.shape] || modelData.shape
    }

    x: barStart * panel.pxPerSecond
    y: barLane * laneHeight
    width: Math.max(Theme.clipMinInteractiveWidth, barDuration * panel.pxPerSecond)
    height: laneHeight - 1
    z: selected ? 10 : 1

    function clearLive() {
        liveStart = -1
        liveDuration = -1
        liveLane = -1
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusXs
        color: Qt.rgba(maskItem.barColor.r, maskItem.barColor.g, maskItem.barColor.b,
                       maskItem.enabled_ ? 0.35 : 0.15)
        border.width: maskItem.selected ? 2 : 1
        border.color: maskItem.selected ? Theme.primary : maskItem.barColor

        Text {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: Theme.spacingSm
            anchors.rightMargin: Theme.spacingSm
            elide: Text.ElideRight
            text: maskItem.label
            color: maskItem.enabled_ ? Theme.foreground : Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }

    ThemedToolTip {
        text: qsTr("%1 · %2. Drag to move, drag an edge to retime.")
                  .arg(maskItem.label)
                  .arg(maskItem.modelData.op === "subtract" ? qsTr("cuts out")
                       : maskItem.modelData.op === "intersect" ? qsTr("trims")
                       : qsTr("reveals"))
        visible: bodyHover.hovered && !bodyDrag.pressed
    }

    HoverHandler { id: bodyHover }

    // Body drag: move along the lane, and across rows when the pointer leaves this one.
    MouseArea {
        id: bodyDrag
        anchors.fill: parent
        // Leaves room for the trim handles at either edge.
        anchors.leftMargin: Theme.clipTrimHandleWidth
        anchors.rightMargin: Theme.clipTrimHandleWidth
        cursorShape: Qt.OpenHandCursor
        acceptedButtons: Qt.LeftButton | Qt.RightButton

        property real grabOffsetSeconds: 0
        property bool dragging: false

        onPressed: (mouse) => {
            EditorState.selectMask(maskItem.trackIndex, maskItem.maskIndex)
            if (mouse.button === Qt.RightButton) {
                maskMenu.popup()
                return
            }
            const p = mapToItem(maskItem.parent, mouse.x, mouse.y)
            bodyDrag.grabOffsetSeconds = p.x / maskItem.panel.pxPerSecond - maskItem.modelData.start
            bodyDrag.dragging = true
            EditorState.beginPreviewDrag(qsTr("Move cutout"))
        }

        onPositionChanged: (mouse) => {
            if (!bodyDrag.dragging)
                return
            const p = mapToItem(maskItem.parent, mouse.x, mouse.y)
            const start = Math.max(0, p.x / maskItem.panel.pxPerSecond - bodyDrag.grabOffsetSeconds)
            const lane = Math.max(0, Math.floor(p.y / maskItem.laneHeight))
            maskItem.liveStart = start
            maskItem.liveLane = lane
            EditorState.previewMoveTrackMask(maskItem.trackIndex, maskItem.maskIndex, start, lane)
        }

        function finish() {
            if (!bodyDrag.dragging)
                return
            bodyDrag.dragging = false
            maskItem.clearLive()
            EditorState.commitPreviewDrag()
        }

        onReleased: finish()
        onCanceled: finish()
    }

    ThemedContextMenu {
        id: maskMenu

        ThemedMenuItem {
            text: maskItem.enabled_ ? qsTr("Disable cutout") : qsTr("Enable cutout")
            icon.name: maskItem.enabled_ ? Theme.icons.eyeOff : Theme.icons.eye
            onTriggered: {
                const mask = Object.assign({}, maskItem.modelData)
                mask.enabled = !maskItem.enabled_
                EditorState.setTrackMaskAt(maskItem.trackIndex, maskItem.maskIndex, mask)
            }
        }
        ThemedMenuSeparator {}
        ThemedMenuItem {
            text: qsTr("Delete cutout")
            icon.name: Theme.icons.trash
            onTriggered: EditorState.removeTrackMask(maskItem.trackIndex, maskItem.maskIndex)
        }
    }

    // Trim handles. Dragging the left edge moves the start and shortens by the same amount so the
    // right edge stays put, which is what "retime this end" has to mean.
    Repeater {
        model: [{ left: true }, { left: false }]

        delegate: MouseArea {
            id: trimArea
            required property var modelData

            width: Theme.clipTrimHandleWidth
            height: parent.height
            x: modelData.left ? 0 : parent.width - width
            cursorShape: Qt.SizeHorCursor
            preventStealing: true

            property bool trimming: false
            property real startSeconds: 0
            property real durationSeconds: 0
            property real pressSeconds: 0

            onPressed: (mouse) => {
                EditorState.selectMask(maskItem.trackIndex, maskItem.maskIndex)
                const p = mapToItem(maskItem.parent, mouse.x, mouse.y)
                trimArea.pressSeconds = p.x / maskItem.panel.pxPerSecond
                trimArea.startSeconds = maskItem.modelData.start
                trimArea.durationSeconds = maskItem.modelData.duration
                trimArea.trimming = true
                EditorState.beginPreviewDrag(qsTr("Trim cutout"))
            }

            onPositionChanged: (mouse) => {
                if (!trimArea.trimming)
                    return
                const p = mapToItem(maskItem.parent, mouse.x, mouse.y)
                const delta = p.x / maskItem.panel.pxPerSecond - trimArea.pressSeconds
                // A bar shorter than a frame is not grabbable again, so both ends stop there.
                const minDuration = 1.0 / Math.max(1, EditorState.projectFps())

                let start = trimArea.startSeconds
                let duration = trimArea.durationSeconds
                if (trimArea.modelData.left) {
                    start = Math.max(0, Math.min(trimArea.startSeconds + delta,
                                                 trimArea.startSeconds
                                                 + trimArea.durationSeconds - minDuration))
                    duration = trimArea.startSeconds + trimArea.durationSeconds - start
                } else {
                    duration = Math.max(minDuration, trimArea.durationSeconds + delta)
                }

                maskItem.liveStart = start
                maskItem.liveDuration = duration
                EditorState.previewTrimTrackMask(maskItem.trackIndex, maskItem.maskIndex,
                                                 start, duration)
            }

            function finish() {
                if (!trimArea.trimming)
                    return
                trimArea.trimming = false
                maskItem.clearLive()
                EditorState.commitPreviewDrag()
            }

            onReleased: finish()
            onCanceled: finish()

            Rectangle {
                anchors.fill: parent
                color: trimArea.containsMouse ? Theme.primary : "transparent"
                opacity: 0.4
            }
        }
    }
}
