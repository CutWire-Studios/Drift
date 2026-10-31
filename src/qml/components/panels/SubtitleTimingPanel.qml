pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Drift
import "../subtitles/SubtitleTime.js" as SubtitleTime

PanelFrame {
    id: root
    SearchAnchor {
        searchId: "subtitle-timing"
        title: qsTr("Subtitle timing")
        keywords: "captions tools shift offset sync frame rate speed gaps durations"
    }

    property bool shown: true
    property int scope: 0

    property real shiftAmount: 100
    property int shiftUnit: 0
    property int shiftDirection: 0
    property int shiftEdges: 0

    property int aCue: 1
    property int bCue: 2
    property real aTo: NaN
    property real bTo: NaN

    property real fromFps: 25
    property real toFps: 25
    property real speedPercent: 100

    property real bridgeMaxMs: 1000
    property real bridgeMinMs: 80
    property real bridgeRatio: 0.5
    property real minGapMs: 80
    property real leadInMs: 0
    property real leadOutMs: 0
    property real minDurMs: 700
    property real maxDurMs: 7000

    property var cueList: []

    property int shiftCount: 0
    property int syncCount: 0
    property int fpsCount: 0
    property int speedCount: 0
    property int bridgeCount: 0
    property int minGapCount: 0
    property int leadCount: 0
    property int durationCount: 0
    property int overlapCount: 0

    readonly property int track: EditorState.selectedTrack
    readonly property int clipIndex: EditorState.selectedClip
    readonly property var clipData: EditorState.selectedClipData
    readonly property bool hasClip: track >= 0 && clipIndex >= 0 && Object.keys(clipData).length > 0
    readonly property bool isSubtitle: hasClip && clipData.kind === "subtitle"
    readonly property bool active: shown && isSubtitle
    readonly property int selectedCount: EditorState.subtitles.selectedCues.length
    readonly property real fps: {
        void EditorState.tracksRevision
        return EditorState.subtitles.projectFps() || 25
    }

    readonly property real shiftSeconds: (shiftDirection === 0 ? -1 : 1)
                                         * (shiftUnit === 0 ? shiftAmount / 1000 : shiftAmount / fps)
    readonly property var aCueData: cueList[aCue - 1] ?? null
    readonly property var bCueData: cueList[bCue - 1] ?? null
    readonly property real aFrom: aCueData ? aCueData.start : 0
    readonly property real bFrom: bCueData ? bCueData.start : 0
    readonly property real aTarget: isNaN(aTo) ? aFrom : aTo
    readonly property real bTarget: isNaN(bTo) ? bFrom : bTo
    readonly property string syncProblem: {
        if (!aCueData || !bCueData)
            return qsTr("Pick two existing cues")
        if (Math.abs(aFrom - bFrom) < 0.0005)
            return qsTr("Points A and B must start at different times")
        return ""
    }
    readonly property real syncScale: syncProblem === "" ? (bTarget - aTarget) / (bFrom - aFrom) : 1
    readonly property real syncOffset: aTarget - syncScale * aFrom
    readonly property string paramsSig: JSON.stringify([
        scope, shiftSeconds, shiftEdges, aCue, bCue, aTarget, bTarget, fromFps, toFps, speedPercent,
        bridgeMaxMs, bridgeMinMs, bridgeRatio, minGapMs, leadInMs, leadOutMs, minDurMs, maxDurMs
    ])
    readonly property var fpsPresets: [23.976, 24, 25, 29.97, 30, 50, 59.94, 60]

    function refreshCues() {
        cueList = isSubtitle ? EditorState.subtitles.cues(track, clipIndex) : []
    }

    function recount() {
        if (!active) {
            shiftCount = syncCount = fpsCount = speedCount = bridgeCount = 0
            minGapCount = leadCount = durationCount = overlapCount = 0
            return
        }
        const s = EditorState.subtitles
        const t = track
        const c = clipIndex
        const sc = scope
        shiftCount = shiftSeconds === 0 ? 0 : s.previewShift(t, c, sc, shiftSeconds, shiftEdges)
        syncCount = syncProblem === "" ? s.previewResync(t, c, sc, aFrom, aTarget, bFrom, bTarget) : 0
        fpsCount = s.previewScale(t, c, sc, fromFps / toFps)
        speedCount = s.previewScale(t, c, sc, 100 / speedPercent)
        bridgeCount = s.previewBridgeGaps(t, c, sc, bridgeMaxMs / 1000, bridgeMinMs / 1000, bridgeRatio)
        minGapCount = s.previewMinGap(t, c, sc, minGapMs / 1000)
        leadCount = s.previewLeadInOut(t, c, sc, leadInMs / 1000, leadOutMs / 1000)
        durationCount = s.previewDurations(t, c, sc, minDurMs / 1000, maxDurMs / 1000)
        overlapCount = s.previewFixOverlaps(t, c, sc)
    }

    function nudge(seconds) {
        EditorState.subtitles.applyShift(track, clipIndex, scope, seconds, shiftEdges)
    }

    function useSelected(which) {
        const idx = EditorState.subtitles.anchorCue
        const sel = EditorState.subtitles.selectedCues
        const cue = idx >= 0 ? idx : (sel.length > 0 ? sel[0] : -1)
        if (cue < 0)
            return
        if (which === 0) {
            aCue = cue + 1
            aTo = NaN
        } else {
            bCue = cue + 1
            bTo = NaN
        }
    }

    function usePlayhead(which) {
        const t = EditorState.subtitleLocalPlayheadSeconds(track, clipIndex)
        if (which === 0)
            aTo = t
        else
            bTo = t
    }

    function swapFps() {
        const f = fromFps
        fromFps = toFps
        toFps = f
    }

    function applySync() {
        if (EditorState.subtitles.applyResync(track, clipIndex, scope, aFrom, aTarget, bFrom, bTarget) > 0) {
            aTo = NaN
            bTo = NaN
        }
    }

    onParamsSigChanged: countTimer.restart()
    onActiveChanged: {
        refreshCues()
        countTimer.restart()
    }
    onTrackChanged: Qt.callLater(refreshCues)
    onClipIndexChanged: Qt.callLater(refreshCues)
    onSelectedCountChanged: {
        if (scope === 1 && selectedCount === 0)
            scope = 0
        countTimer.restart()
    }
    Component.onCompleted: {
        const ms = Math.round(2000 / fps)
        bridgeMinMs = ms
        minGapMs = ms
        refreshCues()
        countTimer.restart()
    }

    Connections {
        target: EditorState.subtitles

        function onCuesChanged() {
            root.refreshCues()
            countTimer.restart()
        }
    }

    Timer {
        id: countTimer
        interval: 100
        onTriggered: root.recount()
    }

    component Segments: Row {
        id: segments

        property var options: []
        property int current: 0
        property var disabledIndex: -1
        signal picked(int index)

        spacing: 1

        Repeater {
            model: segments.options

            ThemedToggleButton {
                required property string modelData
                required property int index

                text: modelData
                checkable: false
                checked: segments.current === index
                enabled: segments.disabledIndex !== index
                onClicked: segments.picked(index)
            }
        }
    }

    component NumField: ThemedNumberField {
        id: field

        property real bound: 0
        signal boundEdited(real v)

        width: 72
        height: Theme.controlHeightSm
        onBoundChanged: if (value !== bound) value = bound
        onEdited: (v) => field.boundEdited(v)
        Component.onCompleted: value = bound
    }

    component TimeField: ThemedTextField {
        id: tf

        property real bound: 0
        signal timeEdited(real v)

        width: 110
        height: Theme.controlHeightSm
        font.family: Theme.monoFontFamily
        text: SubtitleTime.format(bound)
        onBoundChanged: {
            errorText = ""
            text = SubtitleTime.format(bound)
        }
        onEditingFinished: {
            const v = SubtitleTime.parse(text)
            if (isNaN(v)) {
                errorText = qsTr("Invalid time")
                return
            }
            errorText = ""
            text = SubtitleTime.format(v)
            tf.timeEdited(v)
        }
    }

    component Field: Column {
        id: field

        property string label: ""
        default property alias content: slot.data

        spacing: 2

        Text {
            text: field.label
            color: Theme.c.subtitleTimingPanel.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        Row {
            id: slot
            spacing: Theme.spacingXs
        }
    }

    component SubHead: Text {
        color: Theme.c.subtitleTimingPanel.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeSm
        font.weight: Font.DemiBold
    }

    component ApplyRow: Item {
        id: applyRow

        property int count: 0
        property string problem: ""
        property string text: qsTr("Apply")
        signal applied()

        width: parent.width
        height: Theme.controlHeightSm

        Text {
            anchors.left: parent.left
            anchors.right: applyButton.left
            anchors.rightMargin: Theme.spacingSm
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
            text: applyRow.problem !== "" ? applyRow.problem : qsTr("%n cue(s) will change", "", applyRow.count)
            color: applyRow.problem !== "" ? Theme.c.subtitleTimingPanel.foregroundError
                                           : Theme.c.subtitleTimingPanel.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        ThemedButton {
            id: applyButton
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: applyRow.text
            variant: "primary"
            enabled: applyRow.count > 0 && applyRow.problem === ""
            onClicked: applyRow.applied()
        }
    }

    component Rule: Rectangle {
        width: parent.width
        height: Theme.c.subtitleTimingPanel.borderWidth
        color: Theme.c.subtitleTimingPanel.border
    }

    component Section: Column {
        id: section

        property string title: ""
        property bool expanded: true
        default property alias content: body.data

        width: parent.width

        CollapsibleSectionHeader {
            title: section.title
            expanded: section.expanded
            onExpandToggled: section.expanded = !section.expanded
        }

        Column {
            id: body
            x: Theme.spacingMd
            width: parent.width - Theme.spacingMd * 2
            visible: section.expanded
            spacing: Theme.spacingMd
            bottomPadding: Theme.spacingMd
        }
    }

    Rectangle {
        id: scopeBar
        visible: root.isSubtitle
        width: parent.width
        height: visible ? scopeFlow.height + Theme.spacingMd * 2 : 0
        color: Theme.c.subtitleTimingPanel.stickyBackground
        z: 1

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: Theme.c.subtitleTimingPanel.borderWidth
            color: Theme.c.subtitleTimingPanel.border
        }

        Flow {
            id: scopeFlow
            x: Theme.spacingMd
            y: Theme.spacingMd
            width: parent.width - Theme.spacingMd * 2
            spacing: Theme.spacingMd

            Text {
                height: Theme.controlHeightSm
                verticalAlignment: Text.AlignVCenter
                text: qsTr("Scope")
                color: Theme.c.subtitleTimingPanel.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
            }

            Segments {
                options: [qsTr("All"), qsTr("Selected (%1)").arg(root.selectedCount), qsTr("From playhead")]
                current: root.scope
                disabledIndex: root.selectedCount === 0 ? 1 : -1
                onPicked: (i) => root.scope = i
            }
        }
    }

    Flickable {
        id: flick
        visible: root.isSubtitle
        anchors.top: scopeBar.bottom
        anchors.bottom: parent.bottom
        width: parent.width
        contentWidth: width
        contentHeight: sections.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar { }

        Column {
            id: sections
            width: flick.width

            Section {
                title: qsTr("Shift")
                SearchAnchor {
                    searchId: "subtitle-shift-times"
                    title: qsTr("Shift times")
                    keywords: "captions offset delay early late move"
                }

                Flow {
                    width: parent.width
                    spacing: Theme.spacingMd

                    Field {
                        label: qsTr("Amount")
                        NumField {
                            bound: root.shiftAmount
                            from: 0
                            to: 36000000
                            onBoundEdited: (v) => root.shiftAmount = v
                        }
                        Segments {
                            options: [qsTr("ms"), qsTr("frames")]
                            current: root.shiftUnit
                            onPicked: (i) => root.shiftUnit = i
                        }
                    }

                    Field {
                        label: qsTr("Direction")
                        Segments {
                            options: [qsTr("Earlier"), qsTr("Later")]
                            current: root.shiftDirection
                            onPicked: (i) => root.shiftDirection = i
                        }
                    }

                    Field {
                        label: qsTr("Edges")
                        Segments {
                            options: [qsTr("Both"), qsTr("Start"), qsTr("End")]
                            current: root.shiftEdges
                            onPicked: (i) => root.shiftEdges = i
                        }
                    }
                }

                ApplyRow {
                    count: root.shiftCount
                    onApplied: root.nudge(root.shiftSeconds)
                }

                Flow {
                    width: parent.width
                    spacing: Theme.spacingXs

                    Repeater {
                        model: [
                            { "text": qsTr("−1 s"), "frames": 0, "seconds": -1 },
                            { "text": qsTr("−100 ms"), "frames": 0, "seconds": -0.1 },
                            { "text": qsTr("−1 f"), "frames": -1, "seconds": 0 },
                            { "text": qsTr("+1 f"), "frames": 1, "seconds": 0 },
                            { "text": qsTr("+100 ms"), "frames": 0, "seconds": 0.1 },
                            { "text": qsTr("+1 s"), "frames": 0, "seconds": 1 }
                        ]

                        ThemedButton {
                            required property var modelData

                            text: modelData.text
                            enabled: root.scope !== 1 || root.selectedCount > 0
                            onClicked: root.nudge(modelData.frames !== 0 ? modelData.frames / root.fps
                                                                         : modelData.seconds)
                        }
                    }
                }
            }

            Rule { }

            Section {
                title: qsTr("Two-point sync")
                SearchAnchor {
                    searchId: "subtitle-two-point-sync"
                    title: qsTr("Two-point sync")
                    keywords: "captions resync stretch scale align drift"
                }

                Repeater {
                    model: 2

                    Flow {
                        id: pointRow

                        required property int index
                        readonly property bool isA: index === 0

                        width: parent.width
                        spacing: Theme.spacingMd

                        Field {
                            label: pointRow.isA ? qsTr("Point A cue") : qsTr("Point B cue")
                            NumField {
                                bound: pointRow.isA ? root.aCue : root.bCue
                                from: 1
                                to: Math.max(1, root.cueList.length)
                                onBoundEdited: (v) => {
                                    if (pointRow.isA) {
                                        root.aCue = v
                                        root.aTo = NaN
                                    } else {
                                        root.bCue = v
                                        root.bTo = NaN
                                    }
                                }
                            }
                            ThemedButton {
                                height: Theme.controlHeightSm
                                text: qsTr("Use selected")
                                enabled: root.selectedCount > 0 || EditorState.subtitles.anchorCue >= 0
                                onClicked: root.useSelected(pointRow.index)
                            }
                        }

                        Field {
                            label: qsTr("Currently starts at")
                            Text {
                                height: Theme.controlHeightSm
                                verticalAlignment: Text.AlignVCenter
                                text: SubtitleTime.format(pointRow.isA ? root.aFrom : root.bFrom)
                                color: Theme.c.subtitleTimingPanel.foreground
                                font.family: Theme.monoFontFamily
                                font.pixelSize: Theme.fontSizeSm
                            }
                        }

                        Field {
                            label: qsTr("Should start at")
                            TimeField {
                                bound: pointRow.isA ? root.aTarget : root.bTarget
                                onTimeEdited: (v) => {
                                    if (pointRow.isA)
                                        root.aTo = v
                                    else
                                        root.bTo = v
                                }
                            }
                            ThemedButton {
                                height: Theme.controlHeightSm
                                text: qsTr("Use playhead")
                                onClicked: root.usePlayhead(pointRow.index)
                            }
                        }
                    }
                }

                Text {
                    visible: root.syncProblem === ""
                    text: qsTr("Scale %1%  ·  Offset %2 s")
                              .arg((root.syncScale * 100).toFixed(4)).arg(root.syncOffset.toFixed(3))
                    color: Theme.c.subtitleTimingPanel.foregroundMuted
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                ApplyRow {
                    count: root.syncCount
                    problem: root.syncProblem
                    onApplied: root.applySync()
                }
            }

            Rule { }

            Section {
                title: qsTr("Frame rate / speed")
                SearchAnchor {
                    searchId: "subtitle-frame-rate"
                    title: qsTr("Frame rate conversion")
                    keywords: "captions fps 23.976 24 25 29.97 30 speed"
                }

                Flow {
                    width: parent.width
                    spacing: Theme.spacingMd

                    Repeater {
                        model: 2

                        Field {
                            id: fpsField

                            required property int index
                            readonly property bool isFrom: index === 0
                            readonly property real current: isFrom ? root.fromFps : root.toFps

                            label: isFrom ? qsTr("From fps") : qsTr("To fps")

                            NumField {
                                width: 80
                                decimals: 3
                                from: 1
                                to: 1000
                                bound: fpsField.current
                                onBoundEdited: (v) => {
                                    if (fpsField.isFrom)
                                        root.fromFps = v
                                    else
                                        root.toFps = v
                                }
                            }
                            ThemedComboBox {
                                height: Theme.controlHeightSm
                                width: widestContentWidth + Theme.spacingLg * 2
                                model: root.fpsPresets.map(f => String(f))
                                currentIndex: root.fpsPresets.findIndex(f => Math.abs(f - fpsField.current) < 1e-6)
                                onActivated: (i) => {
                                    if (fpsField.isFrom)
                                        root.fromFps = root.fpsPresets[i]
                                    else
                                        root.toFps = root.fpsPresets[i]
                                }
                            }
                        }
                    }

                    Field {
                        label: " "
                        ThemedButton {
                            height: Theme.controlHeightSm
                            text: qsTr("Swap")
                            tooltip: qsTr("Swap the from and to frame rates")
                            onClicked: root.swapFps()
                        }
                    }
                }

                ApplyRow {
                    count: root.fpsCount
                    onApplied: EditorState.subtitles.applyScale(root.track, root.clipIndex, root.scope,
                                                                root.fromFps / root.toFps)
                }

                Rule { }

                Flow {
                    width: parent.width
                    spacing: Theme.spacingMd

                    Field {
                        label: qsTr("Speed %")
                        NumField {
                            width: 90
                            decimals: 4
                            from: 1
                            to: 1000
                            bound: root.speedPercent
                            onBoundEdited: (v) => root.speedPercent = v
                        }
                        ThemedButton {
                            height: Theme.controlHeightSm
                            text: "100.1001"
                            onClicked: root.speedPercent = 100.1001
                        }
                        ThemedButton {
                            height: Theme.controlHeightSm
                            text: "99.9001"
                            onClicked: root.speedPercent = 99.9001
                        }
                    }
                }

                ApplyRow {
                    count: root.speedCount
                    onApplied: EditorState.subtitles.applyScale(root.track, root.clipIndex, root.scope,
                                                                100 / root.speedPercent)
                }
            }

            Rule { }

            Section {
                title: qsTr("Gaps and durations")

                SubHead {
                    text: qsTr("Bridge small gaps")
                    SearchAnchor {
                        searchId: "subtitle-bridge-gaps"
                        title: qsTr("Bridge gaps")
                        keywords: "captions minimum gap lead in out duration overlap"
                    }
                }

                Flow {
                    width: parent.width
                    spacing: Theme.spacingMd

                    Field {
                        label: qsTr("Shorter than (ms)")
                        NumField {
                            from: 0
                            to: 600000
                            bound: root.bridgeMaxMs
                            onBoundEdited: (v) => root.bridgeMaxMs = v
                        }
                    }

                    Field {
                        label: qsTr("Keep min gap (ms)")
                        NumField {
                            from: 0
                            to: 600000
                            bound: root.bridgeMinMs
                            onBoundEdited: (v) => root.bridgeMinMs = v
                        }
                    }

                    Field {
                        label: qsTr("Given to previous")
                        ThemedSlider {
                            width: 140
                            from: 0
                            to: 1
                            stepSize: 0.01
                            label: qsTr("Share of the gap given to the previous cue")
                            valueFormatter: (v) => Math.round(v * 100) + "%"
                            value: root.bridgeRatio
                            onMoved: root.bridgeRatio = value
                        }
                        Text {
                            height: Theme.controlHeightSm
                            verticalAlignment: Text.AlignVCenter
                            text: Math.round(root.bridgeRatio * 100) + "%"
                            color: Theme.c.subtitleTimingPanel.foreground
                            font.family: Theme.monoFontFamily
                            font.pixelSize: Theme.fontSizeXs
                        }
                    }
                }

                ApplyRow {
                    count: root.bridgeCount
                    onApplied: EditorState.subtitles.applyBridgeGaps(
                                   root.track, root.clipIndex, root.scope, root.bridgeMaxMs / 1000,
                                   root.bridgeMinMs / 1000, root.bridgeRatio)
                }

                Rule { }

                SubHead { text: qsTr("Minimum gap") }

                Flow {
                    width: parent.width
                    spacing: Theme.spacingMd

                    Field {
                        label: qsTr("Gap (ms)")
                        NumField {
                            from: 0
                            to: 600000
                            bound: root.minGapMs
                            onBoundEdited: (v) => root.minGapMs = v
                        }
                    }
                }

                ApplyRow {
                    count: root.minGapCount
                    onApplied: EditorState.subtitles.applyMinGap(root.track, root.clipIndex, root.scope,
                                                                 root.minGapMs / 1000)
                }

                Rule { }

                SubHead { text: qsTr("Lead-in / lead-out") }

                Flow {
                    width: parent.width
                    spacing: Theme.spacingMd

                    Field {
                        label: qsTr("Lead-in (ms)")
                        NumField {
                            from: 0
                            to: 600000
                            bound: root.leadInMs
                            onBoundEdited: (v) => root.leadInMs = v
                        }
                    }

                    Field {
                        label: qsTr("Lead-out (ms)")
                        NumField {
                            from: 0
                            to: 600000
                            bound: root.leadOutMs
                            onBoundEdited: (v) => root.leadOutMs = v
                        }
                    }
                }

                ApplyRow {
                    count: root.leadCount
                    onApplied: EditorState.subtitles.applyLeadInOut(root.track, root.clipIndex, root.scope,
                                                                    root.leadInMs / 1000, root.leadOutMs / 1000)
                }

                Rule { }

                SubHead { text: qsTr("Duration limits") }

                Flow {
                    width: parent.width
                    spacing: Theme.spacingMd

                    Field {
                        label: qsTr("Min duration (ms)")
                        NumField {
                            from: 0
                            to: 600000
                            bound: root.minDurMs
                            onBoundEdited: (v) => root.minDurMs = v
                        }
                    }

                    Field {
                        label: qsTr("Max duration (ms, 0 = off)")
                        NumField {
                            from: 0
                            to: 600000
                            bound: root.maxDurMs
                            onBoundEdited: (v) => root.maxDurMs = v
                        }
                    }
                }

                ApplyRow {
                    count: root.durationCount
                    onApplied: EditorState.subtitles.applyDurations(root.track, root.clipIndex, root.scope,
                                                                    root.minDurMs / 1000, root.maxDurMs / 1000)
                }

                Rule { }

                SubHead { text: qsTr("Overlaps") }

                ApplyRow {
                    count: root.overlapCount
                    text: qsTr("Fix overlaps")
                    onApplied: EditorState.subtitles.applyFixOverlaps(root.track, root.clipIndex, root.scope)
                }
            }
        }
    }

    EmptyState {
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.spacing3xl, 280)
        visible: !root.isSubtitle
        compact: true
        title: qsTr("Select a subtitle clip")
        hint: qsTr("Pick a subtitle clip on the timeline to adjust its timing here.")
    }
}
