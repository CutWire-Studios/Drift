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
    readonly property var trackerList: EditorState.tracking.trackerList
    // Timeline span of the target clip, for the confidence strip's playhead tick.
    property real clipStart: 0
    property real clipDuration: 0
    property string clipId: ""
    readonly property string selectedId: trackerList.selectedId

    readonly property bool hasTarget: target.kind === "clip"
    // The target depends on what is under the playhead: an adjustment layer borrows the clip below
    // it, and with nothing selected the panel offers the clip at the playhead.
    readonly property bool followsPlayhead: target.kind === "adjustment" || target.kind === "none"
                                            || target.viaAdjustment !== undefined
    property bool clipAtPlayhead: false
    // A clip that can follow a tracker (text, sticker, image on another track) is selected instead of a video.
    property bool followerSelected: false
    // The video under the selected follower at the playhead ({clipId, name}, empty when none), and the
    // trackers already driving the selected clip.
    property var videoUnderFollower: ({})
    property var drivers: []
    // Set when a drag has just replaced the samples after the playhead; cleared by the next track.
    property string correctedId: ""
    readonly property string placeMode: ShellBridge.trackerPlaceMode

    readonly property var placeHints: ({
        "point": qsTr("Click the feature to track on the preview."),
        "planar": qsTr("Click the corners of a flat surface, Enter to finish."),
        "addPoint": qsTr("Click the feature to add to this tracker."),
        "follow": qsTr("Click the object the clip should follow.")
    })
    readonly property var solveModes: ["translate", "translateRotate", "translateRotateScale", "perspective"]
    readonly property var solveLabels: ({
        "translate": qsTr("Position"),
        "translateRotate": qsTr("Position + rotation"),
        "translateRotateScale": qsTr("Position, rotation + size"),
        "perspective": qsTr("Perspective")
    })

    function withShortcut(label, actionId) {
        const key = EditorState.shortcutFor(actionId)
        return key.length > 0 ? qsTr("%1 (%2)").arg(label).arg(key) : label
    }

    function refresh() {
        const selectedId = EditorState.selectedClipData.id || ""
        if (ShellBridge.trackerFollowerId !== "" && selectedId !== ShellBridge.trackerFollowerId) {
            ShellBridge.trackerHostId = ""
            ShellBridge.trackerFollowerId = ""
        }
        target = ShellBridge.trackerTarget()
        clipAtPlayhead = target.kind === "none"
                         && EditorState.preview.clipsAtPlayhead().some(box => box.kind === "video" || box.kind === "image")
        const selectedKind = EditorState.selectedClipData.kind
        followerSelected = target.kind === "none" && !!selectedKind && selectedKind !== "video"
                           && selectedKind !== "audio" && selectedKind !== "adjustment" && selectedKind !== "subtitle"
        const following = ShellBridge.trackerFollowerId !== ""
        videoUnderFollower = followerSelected && !following && selectedId !== ""
                             ? EditorState.tracking.videoUnder(selectedId) : ({})
        drivers = selectedKind && selectedKind !== "video" && selectedId !== ""
                  ? EditorState.tracking.trackersDriving(selectedId) : []
        const clip = target.kind === "clip" ? EditorState.clipAt(target.track, target.clip) : ({})
        clipStart = clip.start || 0
        clipDuration = clip.duration || 0
        clipId = target.kind === "clip" ? EditorState.masking.clipIdAt(target.track, target.clip) : ""
        if (target.kind === "clip")
            trackerList.refresh(target.track, target.clip)
        else
            trackerList.refresh(-1, -1)
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

    function startFollow() {
        ShellBridge.trackerFollowerId = EditorState.selectedClipData.id
        ShellBridge.trackerHostId = videoUnderFollower.clipId
        EditorState.preview.maskingTool = "tracker"
        ShellBridge.trackerPlaceMode = "follow"
        refresh()
    }

    function runTrack(id, forward, singleFrame) {
        correctedId = ""
        if (hasTarget)
            EditorState.tracking.track(clipId, id, forward, singleFrame)
    }

    function trackBoth(id) {
        correctedId = ""
        if (hasTarget)
            ShellBridge.trackBoth(clipId, id)
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



    function seekStrip(fraction) {
        if (clipDuration <= 0)
            return
        EditorState.playheadSeconds = clipStart + Math.max(0, Math.min(1, fraction)) * clipDuration
    }

    // Escape stops a running track, or cancels an armed placement, even when the Masking panel is
    // not in the layout.
    function handleAction(id) {
        switch (id) {
        case "trackForward":
        case "trackBackward":
        case "trackFrameForward":
        case "trackFrameBackward":
        case "trackBoth":
            if (!shown || !hasTarget || selectedId === "" || EditorState.tracking.busy)
                return false
            if (id === "trackBoth")
                trackBoth(selectedId)
            else
                runTrack(selectedId, id === "trackForward" || id === "trackFrameForward",
                         id === "trackFrameForward" || id === "trackFrameBackward")
            return true
        }
        if (id !== "clearSelection")
            return false
        if (EditorState.tracking.busy) {
            ShellBridge.stopTracking()
            return true
        }
        if (ShellBridge.trackerPlaceMode === "")
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
        trackerList.selectedId = ShellBridge.selectedTrackerId
        if (shown)
            refresh()
    }

    Connections {
        target: ShellBridge
        // The overlay selects a tracker it has just placed.
        function onSelectedTrackerIdChanged() { root.trackerList.selectedId = ShellBridge.selectedTrackerId }
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
        // An adjustment layer has no trackers of its own: the clip under it changes with the playhead.
        function onPlayheadSecondsChanged() {
            if (root.followsPlayhead && !EditorState.scrubbing && !EditorState.tracking.busy)
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
        function onSamplesUpdated(trackerId) {
            if (!EditorState.tracking.busy && trackerId === root.selectedId)
                root.correctedId = trackerId
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
                visible: !root.hasTarget && !(root.followerSelected && root.videoUnderFollower.clipId !== undefined)
                compact: true
                title: qsTr("No clip selected")
                hint: root.target.kind === "adjustment"
                      ? qsTr("No video clip under this adjustment layer at the playhead.")
                      : root.followerSelected
                        ? qsTr("No video under this clip at the playhead. Move the playhead over the video it should follow.")
                        : qsTr("Select a video clip to track motion in it.")
            }

            Column {
                visible: root.followerSelected && root.videoUnderFollower.clipId !== undefined
                         && root.drivers.length === 0
                width: parent.width
                spacing: Theme.spacingSm

                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: qsTr("Follow an object in %1").arg(root.videoUnderFollower.name || "")
                    color: Theme.c.maskingPanel.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm
                    font.weight: Font.Medium
                }

                ThemedButton {
                    width: parent.width
                    variant: "primary"
                    text: qsTr("Follow an object")
                    tooltip: qsTr("Click an object on the video; this clip will follow it through the clip")
                    onClicked: root.startFollow()
                }
            }

            Repeater {
                model: root.drivers

                delegate: RowLayout {
                    id: driverRow
                    required property var modelData
                    width: column.width
                    spacing: Theme.spacingSm

                    Text {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        elide: Text.ElideRight
                        text: qsTr("Follows %1 on %2").arg(driverRow.modelData.trackerName).arg(driverRow.modelData.hostName)
                        color: Theme.c.maskingPanel.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                    }

                    ThemedButton {
                        Layout.alignment: Qt.AlignVCenter
                        text: qsTr("Stop following")
                        onClicked: EditorState.tracking.removeTarget(driverRow.modelData.hostClipId,
                                                                     driverRow.modelData.trackerId,
                                                                     EditorState.selectedClipData.id)
                    }
                }
            }

            ThemedButton {
                visible: !root.hasTarget && root.target.kind === "none" && root.clipAtPlayhead
                text: qsTr("Use clip at playhead")
                onClicked: root.useClipAtPlayhead()
            }

            Text {
                visible: root.hasTarget && root.target.viaAdjustment !== undefined
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("Tracking: %1 (under %2)").arg(root.target.name || "").arg(root.target.viaAdjustment || "")
                color: Theme.c.maskingPanel.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
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

                ThemedToggleButton {
                    Layout.alignment: Qt.AlignVCenter
                    glyph: Theme.icons.crosshair
                    text: qsTr("Point")
                    checked: root.placeMode === "point"
                    checkable: false
                    enabled: !EditorState.tracking.busy
                    tooltip: qsTr("Track a feature with one point")
                    onClicked: {
                        if (root.placeMode === "point")
                            ShellBridge.trackerPlaceMode = ""
                        else
                            root.startPlacing("point", "")
                    }
                }

                ThemedToggleButton {
                    Layout.alignment: Qt.AlignVCenter
                    glyph: Theme.icons.square
                    text: qsTr("Planar")
                    checked: root.placeMode === "planar"
                    checkable: false
                    enabled: !EditorState.tracking.busy
                    tooltip: qsTr("Track a flat surface outlined by its corners")
                    onClicked: {
                        if (root.placeMode === "planar")
                            ShellBridge.trackerPlaceMode = ""
                        else
                            root.startPlacing("planar", "")
                    }
                }
            }

            RowLayout {
                visible: root.hasTarget && root.placeMode !== ""
                width: parent.width
                spacing: Theme.spacingSm

                Text {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    wrapMode: Text.WordWrap
                    text: root.placeHints[root.placeMode] || ""
                    color: Theme.c.maskingPanel.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                ThemedButton {
                    Layout.alignment: Qt.AlignVCenter
                    text: qsTr("Cancel")
                    tooltip: qsTr("Stop placing (Esc)")
                    onClicked: ShellBridge.trackerPlaceMode = ""
                }
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
                visible: root.hasTarget && root.trackerList.count === 0
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
                    required property int index
                    required property var model
                    readonly property bool selected: model.id === root.selectedId
                    readonly property bool isPoint: model.kind === "point"
                    // Read from the controller when the row is selected; the pattern size slider
                    // needs the frame, so it is not bound.
                    property real patternPercent: 0
                    width: column.width
                    spacing: Theme.spacingSm

                    function refreshDetail() {
                        if (!selected)
                            return
                        patternPercent = root.trackerList.patternPercent(model.id, EditorState.playheadSeconds)
                    }

                    onSelectedChanged: refreshDetail()
                    Component.onCompleted: refreshDetail()

                    Connections {
                        target: EditorState.tracking
                        function onSamplesUpdated(trackerId) {
                            if (trackerId === trackerRow.model.id)
                                trackerRow.refreshDetail()
                        }
                        function onTrackersChanged() { trackerRow.refreshDetail() }
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
                            onTapped: {
                                root.trackerList.selectedId = trackerRow.model.id
                                EditorState.preview.maskingTool = "tracker"
                            }
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.spacingSm
                            anchors.rightMargin: Theme.spacingXs
                            spacing: Theme.spacingSm

                            Text {
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignVCenter
                                text: trackerRow.model.name
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
                                onClicked: {
                                    if (trackerRow.model.targets.length === 0)
                                        EditorState.tracking.removeTracker(root.clipId,
                                                                           trackerRow.model.id, false)
                                    else
                                        deleteTrackerDialog.openFor(trackerRow.model)
                                }
                            }
                        }
                    }

                    Column {
                        visible: trackerRow.selected
                        x: Theme.spacingLg
                        width: parent.width - Theme.spacingLg
                        spacing: Theme.spacingSm

                        Text {
                            visible: !((trackerRow.model.sampleRange ?? []).length === 2
                                       && trackerRow.model.sampleRange[1] - trackerRow.model.sampleRange[0] > 0.001)
                                     && !EditorState.tracking.busy
                            width: parent.width
                            wrapMode: Text.WordWrap
                            text: qsTr("Now track it: press Track whole clip, or ◀ ▶ to go one way.")
                            color: Theme.c.maskingPanel.foregroundMuted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs
                        }

                        ThemedButton {
                            visible: EditorState.tracking.busy
                            width: parent.width
                            variant: "destructive"
                            text: qsTr("Stop tracking")
                            tooltip: qsTr("Stop tracking (Esc)")
                            onClicked: ShellBridge.stopTracking()
                        }

                        ThemedButton {
                            visible: !EditorState.tracking.busy
                            width: parent.width
                            variant: "primary"
                            text: qsTr("Track whole clip")
                            tooltip: root.withShortcut(qsTr("Follow the feature backward and forward through the whole clip"), "trackBoth")
                            enabled: !EditorState.tracking.busy
                            onClicked: root.trackBoth(trackerRow.model.id)
                        }

                        RowLayout {
                            visible: root.correctedId === trackerRow.model.id && !EditorState.tracking.busy
                            width: parent.width
                            spacing: Theme.spacingSm

                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: qsTr("Corrected at this frame. Track ▶ to continue.")
                                color: Theme.c.maskingPanel.foregroundMuted
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeXs
                            }

                            ThemedButton {
                                Layout.alignment: Qt.AlignVCenter
                                text: qsTr("Track ▶")
                                onClicked: root.runTrack(trackerRow.model.id, true, false)
                            }
                        }

                        RowLayout {
                            width: parent.width
                            spacing: Theme.spacingXs

                            IconButton {
                                glyph: Theme.icons.skipBack
                                tooltip: root.withShortcut(qsTr("Track back to the start"), "trackBackward")
                                enabled: !EditorState.tracking.busy
                                onClicked: root.runTrack(trackerRow.model.id, false, false)
                            }

                            IconButton {
                                glyph: Theme.icons.stepBack
                                tooltip: root.withShortcut(qsTr("Track one frame back"), "trackFrameBackward")
                                enabled: !EditorState.tracking.busy
                                onClicked: root.runTrack(trackerRow.model.id, false, true)
                            }

                            IconButton {
                                glyph: Theme.icons.x
                                tooltip: qsTr("Stop tracking (Esc)")
                                enabled: EditorState.tracking.busy
                                onClicked: ShellBridge.stopTracking()
                            }

                            IconButton {
                                glyph: Theme.icons.stepForward
                                tooltip: root.withShortcut(qsTr("Track one frame forward"), "trackFrameForward")
                                enabled: !EditorState.tracking.busy
                                onClicked: root.runTrack(trackerRow.model.id, true, true)
                            }

                            IconButton {
                                glyph: Theme.icons.skipForward
                                tooltip: root.withShortcut(qsTr("Track to the end"), "trackForward")
                                enabled: !EditorState.tracking.busy
                                onClicked: root.runTrack(trackerRow.model.id, true, false)
                            }
                        }

                        ThemedProgressBar {
                            visible: EditorState.tracking.busy
                                     && EditorState.tracking.busyTrackerId === trackerRow.model.id
                            width: parent.width
                            value: EditorState.tracking.progress
                        }

                        Item {
                            id: strip
                            width: parent.width
                            height: 14

                            ConfidenceStripItem {
                                anchors.fill: parent
                                tracking: EditorState.tracking
                                clipId: root.clipId
                                trackerId: trackerRow.model.id ?? ""
                                minConfidence: trackerRow.model.minConfidence ?? 0.5
                                emptyColor: Theme.c.trackingPanel.stripEmpty
                                goodColor: Theme.c.trackingPanel.stripGood
                                warnColor: Theme.c.trackingPanel.stripWarn
                                badColor: Theme.c.trackingPanel.stripBad
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
                                text: qsTr("Follow")
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
                                currentIndex: Math.max(0, model.indexOf(trackerRow.model.solve))
                                onActivated: (index) => {
                                    if (model[index] === "perspective" && trackerRow.model.pointCount < 4) {
                                        Toasts.info(qsTr("Add up to 4 points on the same surface for rotation, scale and perspective"))
                                        currentIndex = Math.max(0, model.indexOf(trackerRow.model.solve))
                                        return
                                    }
                                    EditorState.tracking.setTrackerProperty(root.clipId,
                                                                            trackerRow.model.id,
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
                                text: qsTr("Stop if")
                                color: Theme.c.maskingPanel.foregroundMuted
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeXs

                                HoverHandler { id: stopIfHover }
                                ThemedToolTip {
                                    visible: stopIfHover.hovered
                                    text: qsTr("Tracking stops when the match gets worse than this")
                                }
                            }

                            ThemedSlider {
                                id: minConfSlider
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignVCenter
                                label: qsTr("Stop if match is worse than")
                                from: 30
                                to: 95
                                valueFormatter: function (v) { return qsTr("match < %1 %").arg(Math.round(v)) }
                                Binding on value {
                                    when: !minConfSlider.pressed
                                    value: trackerRow.model.minConfidence * 100
                                }
                                onPressedChanged: {
                                    if (!pressed)
                                        EditorState.tracking.setTrackerProperty(root.clipId,
                                                                                trackerRow.model.id,
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
                                text: qsTr("Box size")
                                color: Theme.c.maskingPanel.foregroundMuted
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeXs

                                HoverHandler { id: boxSizeHover }
                                ThemedToolTip {
                                    visible: boxSizeHover.hovered
                                    text: qsTr("Size of the area being followed. Bigger is steadier, smaller is more precise.")
                                }
                            }

                            ThemedSlider {
                                id: patternSlider
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignVCenter
                                label: qsTr("Box size")
                                from: 2
                                to: 20
                                valueFormatter: function (v) { return Math.round(v) + " %" }
                                Binding on value {
                                    when: !patternSlider.pressed
                                    value: trackerRow.patternPercent
                                }
                                onPressedChanged: {
                                    if (!pressed)
                                        EditorState.tracking.setTrackerProperty(root.clipId,
                                                                                trackerRow.model.id,
                                                                                "patternSize", value / 100)
                                }
                            }
                        }

                        Flow {
                            width: parent.width
                            spacing: Theme.spacingSm

                            ThemedButton {
                                text: qsTr("Erase before")
                                tooltip: qsTr("Remove the samples before the playhead")
                                enabled: !EditorState.tracking.busy
                                onClicked: EditorState.tracking.clearSamples(root.clipId,
                                                                             trackerRow.model.id, false)
                            }

                            ThemedButton {
                                text: qsTr("Erase after")
                                tooltip: qsTr("Remove the samples after the playhead")
                                enabled: !EditorState.tracking.busy
                                onClicked: EditorState.tracking.clearSamples(root.clipId,
                                                                             trackerRow.model.id, true)
                            }

                            ThemedButton {
                                visible: trackerRow.isPoint
                                glyph: Theme.icons.plus
                                text: qsTr("Add point")
                                tooltip: qsTr("Place another point on the preview")
                                enabled: trackerRow.model.pointCount < 4
                                onClicked: root.startPlacing("addPoint", trackerRow.model.id)
                            }
                        }

                        Text {
                            text: qsTr("Moves")
                            color: Theme.c.maskingPanel.foregroundMuted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs
                            font.weight: Font.Medium
                        }

                        Repeater {
                            model: trackerRow.model.targets

                            delegate: RowLayout {
                                id: targetRow
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
                                    visible: targetRow.modelData.mode === "mask"
                                    Layout.alignment: Qt.AlignVCenter
                                    text: qsTr("Mask")
                                    color: Theme.c.maskingPanel.foregroundMuted
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeXs
                                }

                                ThemedComboBox {
                                    readonly property var labels: ({
                                        "center": qsTr("Stick to the point"),
                                        "offset": qsTr("Keep current position")
                                    })
                                    visible: targetRow.modelData.mode !== "mask"
                                    Layout.alignment: Qt.AlignVCenter
                                    Layout.preferredWidth: 140
                                    tooltip: qsTr("Put the clip's centre on the tracked point, or keep the offset it has from it at the reference frame")
                                    model: ["center", "offset"]
                                    displayText: labels[model[currentIndex]]
                                    currentIndex: targetRow.modelData.center ? 0 : 1
                                    onActivated: (index) => EditorState.tracking.setTargetCenter(
                                                     root.clipId, trackerRow.model.id,
                                                     targetRow.modelData.clipId, index === 0)
                                }

                                IconButton {
                                    Layout.alignment: Qt.AlignVCenter
                                    glyph: Theme.icons.x
                                    tooltip: qsTr("Stop driving from this")
                                    onClicked: EditorState.tracking.removeTarget(root.clipId,
                                                                                 trackerRow.model.id,
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
                                text: qsTr("Attach…")
                                tooltip: qsTr("Make a mask or clip move with this tracker")
                                onClicked: {
                                    targetMenu.candidates = root.trackerList.candidates(trackerRow.model.id)
                                    if (targetMenu.candidates.length === 0)
                                        Toasts.info(qsTr("No masks or clips to drive from"))
                                    else
                                        targetMenu.popup(addTargetButton, 0, addTargetButton.height)
                                }
                            }

                            ThemedButton {
                                glyph: Theme.icons.plus
                                text: qsTr("Add mask")
                                tooltip: qsTr("Draw a mask from this tracker and make it follow the track")
                                enabled: !EditorState.tracking.busy
                                onClicked: EditorState.tracking.addMaskForTracker(root.clipId, trackerRow.model.id)
                            }

                            ThemedButton {
                                glyph: Theme.icons.refresh
                                text: qsTr("Re-apply")
                                tooltip: qsTr("Update the attached masks and clips from the current track")
                                enabled: !EditorState.tracking.busy && trackerRow.model.targets.length > 0
                                onClicked: EditorState.tracking.apply(root.clipId,
                                                                      trackerRow.model.id)
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
                                    onTriggered: EditorState.tracking.setTarget(root.clipId,
                                                                                trackerRow.model.id,
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
    // Asked only when the tracker drives something: its keys on those clips can stay as plain
    // animation or go with it.
    ThemedDialog {
        id: deleteTrackerDialog

        property string trackerId: ""
        property string trackerName: ""
        property int targetCount: 0
        property bool removeKeys: true

        title: qsTr("Delete tracker")
        acceptText: qsTr("Delete")
        acceptVariant: "destructive"
        preferredWidth: Theme.dialogWidthSm
        acceptOnReturn: false

        function openFor(tracker) {
            trackerId = tracker.id
            trackerName = tracker.name
            targetCount = tracker.targets.length
            removeKeys = true
            open()
        }

        onAccepted: EditorState.tracking.removeTracker(root.clipId,
                                                       deleteTrackerDialog.trackerId, deleteTrackerDialog.removeKeys)

        contentItem: Column {
            spacing: Theme.spacingMd

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("“%1” drives %n clip(s) or mask(s).", "", deleteTrackerDialog.targetCount)
                          .arg(deleteTrackerDialog.trackerName)
                color: Theme.c.maskingPanel.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
            }

            ThemedCheckBox {
                width: parent.width
                text: qsTr("Also remove the keyframes it added to them")
                checked: deleteTrackerDialog.removeKeys
                onToggled: deleteTrackerDialog.removeKeys = checked
            }
        }
    }
}
