pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Drift

PanelFrame {
    id: root

    property bool shown: false
    readonly property bool wantsActions: shown

    // The media clip the trackers live on. Assigned on the signals below, not bound, because each
    // controller call returns a fresh snapshot.
    property var target: ({ kind: "none" })
    property var trackerList: []
    // Timeline span of the target clip, for the confidence strip's playhead tick.
    property real clipStart: 0
    property real clipDuration: 0
    property string selectedId: ""

    readonly property bool hasTarget: target.kind === "clip"
    readonly property string placeMode: ShellBridge.trackerPlaceMode

    readonly property var placeHints: ({
        "point": qsTr("Click the feature to track on the preview."),
        "planar": qsTr("Click the corners of a flat surface, Enter to finish."),
        "addPoint": qsTr("Click the feature to add to this tracker.")
    })
    readonly property var solveModes: ["translate", "translateRotate", "translateRotateScale", "perspective"]
    readonly property var solveLabels: ({
        "translate": qsTr("Position"),
        "translateRotate": qsTr("Position + Rotation"),
        "translateRotateScale": qsTr("Position + Rotation + Scale"),
        "perspective": qsTr("Perspective")
    })

    function refresh() {
        target = EditorState.masking.maskTarget()
        const clip = target.kind === "clip" ? EditorState.clipAt(target.track, target.clip) : ({})
        clipStart = clip.start || 0
        clipDuration = clip.duration || 0
        trackerList = target.kind === "clip" ? EditorState.tracking.trackers(target.track, target.clip) : []
        if (!trackerList.some(t => t.id === selectedId))
            selectedId = trackerList.length > 0 ? trackerList[0].id : ""
    }

    function refreshIfLive() {
        if (shown && !EditorState.playing)
            refresh()
    }

    function startPlacing(mode, trackerId) {
        EditorState.preview.maskingTool = "tracker"
        ShellBridge.trackerPlaceId = trackerId
        ShellBridge.trackerPlaceMode = mode
    }

    function runTrack(id, forward, singleFrame) {
        EditorState.tracking.track(target.track, target.clip, id, forward, singleFrame)
    }

    function seekStrip(fraction) {
        if (clipDuration <= 0)
            return
        EditorState.playheadSeconds = clipStart + Math.max(0, Math.min(1, fraction)) * clipDuration
    }

    function bucketColor(value, minConfidence) {
        if (value < 0)
            return Theme.c.trackingPanel.stripEmpty
        if (value < minConfidence)
            return Theme.c.trackingPanel.stripBad
        if (value >= 0.8)
            return Theme.c.trackingPanel.stripGood
        return Theme.c.trackingPanel.stripWarn
    }

    // Escape cancels an armed placement even when the Masking panel is not in the layout.
    function handleAction(id) {
        if (id !== "clearSelection" || ShellBridge.trackerPlaceMode === "")
            return false
        ShellBridge.trackerPlaceMode = ""
        return true
    }

    onWantsActionsChanged: {
        if (wantsActions)
            ShellBridge.trackingActionHandler = root
        else if (ShellBridge.trackingActionHandler === root)
            ShellBridge.trackingActionHandler = null
    }
    Component.onDestruction: {
        if (ShellBridge.trackingActionHandler === root)
            ShellBridge.trackingActionHandler = null
    }

    onShownChanged: {
        if (shown) {
            refresh()
        } else {
            ShellBridge.trackerPlaceMode = ""
            if (EditorState.preview.maskingTool === "tracker")
                EditorState.preview.maskingTool = ""
        }
    }

    onSelectedIdChanged: ShellBridge.selectedTrackerId = selectedId

    Component.onCompleted: {
        if (wantsActions)
            ShellBridge.trackingActionHandler = root
        selectedId = ShellBridge.selectedTrackerId
        if (shown)
            refresh()
    }

    Connections {
        target: ShellBridge
        // The overlay selects a tracker it has just placed.
        function onSelectedTrackerIdChanged() { root.selectedId = ShellBridge.selectedTrackerId }
    }

    Connections {
        target: EditorState
        function onTracksChanged() { root.refreshIfLive() }
        function onSelectionChanged() { root.refreshIfLive() }
        function onSelectedClipDataChanged() { root.refreshIfLive() }
        function onPlayingChanged() {
            if (!EditorState.playing)
                root.refreshIfLive()
        }
    }

    Connections {
        target: EditorState.tracking
        // Samples stream in while a run goes; the list is rebuilt once it ends, not per batch.
        function onTrackersChanged() {
            if (!EditorState.tracking.busy)
                root.refreshIfLive()
        }
        function onBusyChanged() {
            if (!EditorState.tracking.busy)
                root.refreshIfLive()
        }
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
                hint: qsTr("Select a video clip to track motion in it.")
            }

            RowLayout {
                visible: root.hasTarget
                width: parent.width
                spacing: Theme.spacingSm

                Text {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    text: root.target.name || ""
                    elide: Text.ElideRight
                    color: Theme.c.maskingPanel.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm
                    font.weight: Font.Medium
                }

                ThemedButton {
                    Layout.alignment: Qt.AlignVCenter
                    glyph: Theme.icons.locateFixed
                    text: qsTr("Point")
                    tooltip: qsTr("Track a feature with one point")
                    onClicked: root.startPlacing("point", "")
                }

                ThemedButton {
                    Layout.alignment: Qt.AlignVCenter
                    glyph: Theme.icons.square
                    text: qsTr("Planar")
                    tooltip: qsTr("Track a flat surface outlined by its corners")
                    onClicked: root.startPlacing("planar", "")
                }
            }

            Text {
                visible: root.hasTarget && root.placeMode !== ""
                width: parent.width
                wrapMode: Text.WordWrap
                text: root.placeHints[root.placeMode] || ""
                color: Theme.c.maskingPanel.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            Rectangle {
                visible: root.hasTarget
                width: parent.width
                height: 1
                color: Theme.c.maskingPanel.divider
            }

            Text {
                visible: root.hasTarget
                text: qsTr("Trackers")
                color: Theme.c.maskingPanel.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                font.weight: Font.Medium
            }

            Text {
                visible: root.hasTarget && root.trackerList.length === 0
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("No trackers yet. Use Point or Planar above to start one.")
                color: Theme.c.maskingPanel.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            Repeater {
                model: root.trackerList

                delegate: Column {
                    id: trackerRow
                    required property var modelData
                    readonly property bool selected: modelData.id === root.selectedId
                    readonly property bool isPoint: modelData.kind === "point"
                    // Read from the controller when the row is selected; the strip and the pattern
                    // size slider need the frame, so they are not bound.
                    property real patternPercent: 0
                    width: column.width
                    spacing: Theme.spacingSm

                    function refreshDetail() {
                        if (!selected)
                            return
                        strip.values = EditorState.tracking.confidenceStrip(root.target.track, root.target.clip,
                                                                            modelData.id,
                                                                            Math.max(20, Math.floor(strip.width / 3)))
                        const frame = EditorState.tracking.trackerFrame(root.target.track, root.target.clip,
                                                                        modelData.id, EditorState.playheadSeconds)
                        patternPercent = isPoint && frame.points && frame.points.length > 0
                                         ? frame.points[0].patternW * 100 : 0
                    }

                    onSelectedChanged: refreshDetail()
                    onModelDataChanged: refreshDetail()
                    Component.onCompleted: refreshDetail()

                    Connections {
                        target: EditorState.tracking
                        function onSamplesUpdated(trackerId) {
                            if (trackerId === trackerRow.modelData.id)
                                trackerRow.refreshDetail()
                        }
                    }

                    Rectangle {
                        width: parent.width
                        height: 32
                        color: trackerRow.selected ? Theme.c.maskingPanel.rowSelected
                                                   : (rowHover.hovered ? Theme.c.maskingPanel.rowHover
                                                                       : "transparent")

                        HoverHandler {
                            id: rowHover
                        }

                        TapHandler {
                            onTapped: root.selectedId = trackerRow.modelData.id
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.spacingSm
                            anchors.rightMargin: Theme.spacingXs
                            spacing: Theme.spacingSm

                            Text {
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignVCenter
                                text: trackerRow.modelData.name
                                elide: Text.ElideRight
                                color: Theme.c.maskingPanel.foreground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeXs
                            }

                            Rectangle {
                                Layout.alignment: Qt.AlignVCenter
                                implicitWidth: kindText.implicitWidth + Theme.spacingSm * 2
                                implicitHeight: kindText.implicitHeight + Theme.spacingXs * 2
                                radius: Theme.radiusXs
                                color: Theme.c.maskingPanel.rowHover

                                Text {
                                    id: kindText
                                    anchors.centerIn: parent
                                    text: trackerRow.isPoint ? qsTr("Point") : qsTr("Planar")
                                    color: Theme.c.maskingPanel.foregroundMuted
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeXs
                                }
                            }

                            IconButton {
                                Layout.alignment: Qt.AlignVCenter
                                glyph: Theme.icons.trash
                                tooltip: qsTr("Delete tracker")
                                onClicked: EditorState.tracking.removeTracker(root.target.track, root.target.clip,
                                                                              trackerRow.modelData.id)
                            }
                        }
                    }

                    Column {
                        visible: trackerRow.selected
                        x: Theme.spacingLg
                        width: parent.width - Theme.spacingLg
                        spacing: Theme.spacingSm

                        RowLayout {
                            width: parent.width
                            spacing: Theme.spacingXs

                            IconButton {
                                glyph: Theme.icons.skipBack
                                tooltip: qsTr("Track back to the start")
                                enabled: !EditorState.tracking.busy
                                onClicked: root.runTrack(trackerRow.modelData.id, false, false)
                            }

                            IconButton {
                                glyph: Theme.icons.stepBack
                                tooltip: qsTr("Track one frame back")
                                enabled: !EditorState.tracking.busy
                                onClicked: root.runTrack(trackerRow.modelData.id, false, true)
                            }

                            IconButton {
                                glyph: Theme.icons.square
                                tooltip: qsTr("Stop tracking")
                                enabled: EditorState.tracking.busy
                                onClicked: EditorState.tracking.cancel()
                            }

                            IconButton {
                                glyph: Theme.icons.stepForward
                                tooltip: qsTr("Track one frame forward")
                                enabled: !EditorState.tracking.busy
                                onClicked: root.runTrack(trackerRow.modelData.id, true, true)
                            }

                            IconButton {
                                glyph: Theme.icons.skipForward
                                tooltip: qsTr("Track to the end")
                                enabled: !EditorState.tracking.busy
                                onClicked: root.runTrack(trackerRow.modelData.id, true, false)
                            }
                        }

                        ThemedProgressBar {
                            visible: EditorState.tracking.busy
                                     && EditorState.tracking.busyTrackerId === trackerRow.modelData.id
                            width: parent.width
                            value: EditorState.tracking.progress
                        }

                        Item {
                            id: strip
                            width: parent.width
                            height: 14
                            property var values: []
                            readonly property real bucketWidth: values.length > 0 ? width / values.length : 0

                            onWidthChanged: trackerRow.refreshDetail()

                            Repeater {
                                model: strip.values

                                delegate: Rectangle {
                                    required property var modelData
                                    required property int index
                                    x: index * strip.bucketWidth
                                    width: strip.bucketWidth + 0.5
                                    height: strip.height
                                    color: root.bucketColor(modelData, trackerRow.modelData.minConfidence)
                                }
                            }

                            Rectangle {
                                readonly property real fraction: root.clipDuration > 0
                                                                 ? (EditorState.playheadSeconds - root.clipStart) / root.clipDuration
                                                                 : -1
                                visible: fraction >= 0 && fraction <= 1
                                x: fraction * strip.width - width / 2
                                y: -3
                                width: 2
                                height: strip.height + 6
                                color: Theme.c.trackingPanel.stripPlayhead
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onPressed: (mouse) => root.seekStrip(mouse.x / strip.width)
                                onPositionChanged: (mouse) => {
                                    if (pressed)
                                        root.seekStrip(mouse.x / strip.width)
                                }
                            }
                        }

                        RowLayout {
                            visible: trackerRow.isPoint
                            width: parent.width
                            spacing: Theme.spacingSm

                            Text {
                                Layout.preferredWidth: 56
                                Layout.alignment: Qt.AlignVCenter
                                text: qsTr("Solve")
                                color: Theme.c.maskingPanel.foregroundMuted
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeXs
                            }

                            ThemedComboBox {
                                id: solveBox
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignVCenter
                                tooltip: qsTr("Add up to 4 points on the same surface for rotation, scale and perspective")
                                model: root.solveModes
                                displayText: root.solveLabels[model[currentIndex]] || model[currentIndex]
                                currentIndex: Math.max(0, model.indexOf(trackerRow.modelData.solve))
                                onActivated: (index) => {
                                    if (model[index] === "perspective" && trackerRow.modelData.pointCount < 4) {
                                        Toasts.info(qsTr("Add up to 4 points on the same surface for rotation, scale and perspective"))
                                        currentIndex = Math.max(0, model.indexOf(trackerRow.modelData.solve))
                                        return
                                    }
                                    EditorState.tracking.setTrackerProperty(root.target.track, root.target.clip,
                                                                            trackerRow.modelData.id,
                                                                            "solve", model[index])
                                }
                            }
                        }

                        RowLayout {
                            width: parent.width
                            spacing: Theme.spacingSm

                            Text {
                                Layout.preferredWidth: 56
                                Layout.alignment: Qt.AlignVCenter
                                text: qsTr("Confidence")
                                color: Theme.c.maskingPanel.foregroundMuted
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeXs
                            }

                            ThemedSlider {
                                id: minConfSlider
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignVCenter
                                label: qsTr("Minimum confidence")
                                from: 30
                                to: 95
                                valueFormatter: function (v) { return Math.round(v) + " %" }
                                Binding on value {
                                    when: !minConfSlider.pressed
                                    value: trackerRow.modelData.minConfidence * 100
                                }
                                onPressedChanged: {
                                    if (!pressed)
                                        EditorState.tracking.setTrackerProperty(root.target.track, root.target.clip,
                                                                                trackerRow.modelData.id,
                                                                                "minConfidence", value / 100)
                                }
                            }
                        }

                        RowLayout {
                            visible: trackerRow.isPoint
                            width: parent.width
                            spacing: Theme.spacingSm

                            Text {
                                Layout.preferredWidth: 56
                                Layout.alignment: Qt.AlignVCenter
                                text: qsTr("Pattern")
                                color: Theme.c.maskingPanel.foregroundMuted
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeXs
                            }

                            ThemedSlider {
                                id: patternSlider
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignVCenter
                                label: qsTr("Pattern size")
                                from: 2
                                to: 20
                                valueFormatter: function (v) { return Math.round(v) + " %" }
                                Binding on value {
                                    when: !patternSlider.pressed
                                    value: trackerRow.patternPercent
                                }
                                onPressedChanged: {
                                    if (!pressed)
                                        EditorState.tracking.setTrackerProperty(root.target.track, root.target.clip,
                                                                                trackerRow.modelData.id,
                                                                                "patternSize", value / 100)
                                }
                            }
                        }

                        Flow {
                            width: parent.width
                            spacing: Theme.spacingSm

                            ThemedButton {
                                text: qsTr("Clear ◀")
                                tooltip: qsTr("Remove the samples before the playhead")
                                enabled: !EditorState.tracking.busy
                                onClicked: EditorState.tracking.clearSamples(root.target.track, root.target.clip,
                                                                             trackerRow.modelData.id, false)
                            }

                            ThemedButton {
                                text: qsTr("Clear ▶")
                                tooltip: qsTr("Remove the samples after the playhead")
                                enabled: !EditorState.tracking.busy
                                onClicked: EditorState.tracking.clearSamples(root.target.track, root.target.clip,
                                                                             trackerRow.modelData.id, true)
                            }

                            ThemedButton {
                                visible: trackerRow.isPoint
                                glyph: Theme.icons.plus
                                text: qsTr("Add point")
                                tooltip: qsTr("Place another point on the preview")
                                enabled: trackerRow.modelData.pointCount < 4
                                onClicked: root.startPlacing("addPoint", trackerRow.modelData.id)
                            }
                        }

                        Text {
                            text: qsTr("Drives")
                            color: Theme.c.maskingPanel.foregroundMuted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs
                            font.weight: Font.Medium
                        }

                        Repeater {
                            model: trackerRow.modelData.targets

                            delegate: RowLayout {
                                required property var modelData
                                width: parent.width
                                spacing: Theme.spacingSm

                                Text {
                                    Layout.fillWidth: true
                                    Layout.alignment: Qt.AlignVCenter
                                    text: modelData.name
                                    elide: Text.ElideRight
                                    color: Theme.c.maskingPanel.foreground
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeXs
                                }

                                Text {
                                    Layout.alignment: Qt.AlignVCenter
                                    text: modelData.mode === "mask" ? qsTr("Mask") : qsTr("Follows")
                                    color: Theme.c.maskingPanel.foregroundMuted
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeXs
                                }

                                IconButton {
                                    Layout.alignment: Qt.AlignVCenter
                                    glyph: Theme.icons.x
                                    tooltip: qsTr("Stop driving from this")
                                    onClicked: EditorState.tracking.removeTarget(root.target.track, root.target.clip,
                                                                                 trackerRow.modelData.id,
                                                                                 modelData.clipId)
                                }
                            }
                        }

                        Flow {
                            width: parent.width
                            spacing: Theme.spacingSm

                            ThemedButton {
                                id: addTargetButton
                                glyph: Theme.icons.plus
                                text: qsTr("Add target…")
                                tooltip: qsTr("Let a mask or clip follow this tracker")
                                onClicked: {
                                    const drives = trackerRow.modelData.targets
                                    targetMenu.candidates = EditorState.tracking.targetCandidates(root.target.track,
                                                                                                  root.target.clip)
                                                                .filter(c => !drives.some(d => d.clipId === c.clipId))
                                    if (targetMenu.candidates.length === 0)
                                        Toasts.info(qsTr("No masks or clips to drive from"))
                                    else
                                        targetMenu.popup(addTargetButton, 0, addTargetButton.height)
                                }
                            }

                            ThemedButton {
                                glyph: Theme.icons.refresh
                                text: qsTr("Re-apply")
                                tooltip: qsTr("Regenerate the keys on the drives")
                                enabled: !EditorState.tracking.busy && trackerRow.modelData.targets.length > 0
                                onClicked: EditorState.tracking.apply(root.target.track, root.target.clip,
                                                                      trackerRow.modelData.id)
                            }
                        }

                        ThemedContextMenu {
                            id: targetMenu
                            property var candidates: []

                            Instantiator {
                                model: targetMenu.candidates

                                delegate: ThemedMenuItem {
                                    required property var modelData
                                    text: modelData.kind === "mask" ? qsTr("Mask: %1").arg(modelData.name)
                                                                    : qsTr("Make follow: %1").arg(modelData.name)
                                    onTriggered: EditorState.tracking.setTarget(root.target.track, root.target.clip,
                                                                                trackerRow.modelData.id,
                                                                                modelData.clipId,
                                                                                modelData.kind === "mask" ? "mask" : "followClip")
                                }

                                onObjectAdded: (index, object) => targetMenu.insertItem(index, object)
                                onObjectRemoved: (index, object) => targetMenu.removeItem(object)
                            }
                        }
                    }
                }
            }
        }
    }
}
