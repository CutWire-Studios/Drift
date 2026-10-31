pragma ComponentBehavior: Bound
import QtQuick
import Drift

PanelFrame {
    id: root
    SearchAnchor {
        searchId: "audio-sheet"
        title: qsTr("Audio Sheet")
        keywords: "audio sheet loudness lufs spectrogram events sync analysis report png export"
    }

    readonly property bool hasClip: EditorState.selectedTrack >= 0 && EditorState.selectedClip >= 0
    readonly property bool hasWorkArea: EditorState.workAreaActive
    readonly property var mcp: EditorState.mcp
    readonly property var result: mcp.audioSheetResult
    readonly property bool resultOk: result.ok === true
    readonly property bool busy: mcp.audioSheetBusy

    property string scope: hasClip ? "clip" : (hasWorkArea ? "workArea" : "timeline")
    property int sheetWidth: 2000
    property bool withFrames: true

    readonly property var scopes: [
        { id: "workArea", label: qsTr("Work area"), available: root.hasWorkArea },
        { id: "clip", label: qsTr("Selected clip"), available: root.hasClip },
        { id: "timeline", label: qsTr("Whole timeline"), available: true }
    ]
    readonly property var widths: [
        { label: qsTr("1000 px"), value: 1000 },
        { label: qsTr("1600 px"), value: 1600 },
        { label: qsTr("2000 px"), value: 2000 },
        { label: qsTr("2400 px"), value: 2400 }
    ]

    function generate() {
        const options = { width: root.sheetWidth, frames: root.withFrames }
        if (root.scope === "clip") {
            options.mode = "clip"
            options.track = EditorState.selectedTrack
            options.clip = EditorState.selectedClip
        } else if (root.scope === "workArea") {
            options.mode = "timeline"
            options.start = EditorState.workAreaInSeconds
            options.duration = EditorState.workAreaOutSeconds - EditorState.workAreaInSeconds
        } else {
            options.mode = "timeline"
            options.start = 0
            options.duration = EditorState.durationSeconds
        }
        root.mcp.generateAudioSheet(options)
    }

    function clock(seconds) {
        const s = Math.max(0, seconds)
        return Math.floor(s / 60) + ":" + String(Math.floor(s % 60)).padStart(2, "0")
    }

    function gapCount(r) {
        return r.gaps_total ?? (r.gaps ? r.gaps.length : 0)
    }

    readonly property var stats: {
        const r = root.result
        if (!root.resultOk)
            return []
        const t = Theme.c.audioSheetPanel
        const rows = []
        if (r.lufs !== undefined)
            rows.push({ label: qsTr("Integrated"), value: qsTr("%1 LUFS").arg(r.lufs.toFixed(1)), tone: t.value })
        if (r.lra_lu !== undefined)
            rows.push({ label: qsTr("Loudness range"), value: qsTr("%1 LU").arg(r.lra_lu.toFixed(1)), tone: t.value })
        if (r.true_peak_db !== undefined)
            rows.push({ label: qsTr("True peak"), value: qsTr("%1 dBTP").arg(r.true_peak_db.toFixed(1)),
                        tone: r.true_peak_db > -1 ? t.bad : t.value })
        const gaps = root.gapCount(r)
        rows.push({ label: qsTr("Gaps"), value: String(gaps), tone: gaps > 0 ? t.warn : t.good })
        const clips = r.clipping ? r.clipping.runs : 0
        rows.push({ label: qsTr("Clipping"), value: String(clips), tone: clips > 0 ? t.bad : t.good })
        if (r.events !== undefined)
            rows.push({ label: qsTr("Events"), value: String(r.events), tone: t.value })
        if (r.sync_counts) {
            const ok = r.sync_counts.ok ?? 0
            const off = r.sync_counts.off ?? 0
            rows.push({ label: qsTr("Cuts in sync"), value: qsTr("%1 ok, %2 off").arg(ok).arg(off),
                        tone: off > 0 ? t.warn : t.good })
        }
        return rows
    }

    Component.onCompleted: {
        if (root.scope === "clip" && !root.hasClip)
            root.scope = "timeline"
    }

    AssetsPanelHeader {
        id: header
        width: parent.width
        height: Theme.panelHeaderHeight
        title: qsTr("Audio Sheet")
    }

    Flow {
        id: controls
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingMd
        spacing: Theme.spacingSm

        Repeater {
            model: root.scopes
            delegate: ThemedChip {
                required property var modelData
                text: modelData.label
                enabled: modelData.available && !root.busy
                selected: root.scope === modelData.id
                onClicked: root.scope = modelData.id
            }
        }

        ThemedComboBox {
            width: widestContentWidth + Theme.spacingLg
            enabled: !root.busy
            model: root.widths
            textRole: "label"
            valueRole: "value"
            currentIndex: 2
            tooltip: qsTr("Width of the sheet")
            onActivated: root.sheetWidth = currentValue
        }

        ThemedSwitch {
            text: qsTr("Frame strip")
            enabled: !root.busy
            checked: root.withFrames
            onToggled: root.withFrames = checked
        }

        ThemedButton {
            text: root.busy ? qsTr("Generating…") : qsTr("Generate")
            variant: "primary"
            glyph: Theme.icons.audioLines
            enabled: !root.busy
            onClicked: root.generate()
        }

        ThemedButton {
            text: qsTr("Export PNG…")
            variant: "secondary"
            glyph: Theme.icons.save
            enabled: root.resultOk && !root.busy
            onClicked: viewer.saveAs()
        }
    }

    Flow {
        id: summary
        anchors.top: controls.bottom
        anchors.topMargin: Theme.spacingSm
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.spacingMd
        anchors.rightMargin: Theme.spacingMd
        spacing: Theme.spacingSm
        visible: root.stats.length > 0

        Repeater {
            model: root.stats
            delegate: Rectangle {
                id: stat
                required property var modelData
                width: statColumn.width + Theme.spacingLg * 2
                height: statColumn.height + Theme.spacingSm * 2
                radius: Theme.c.audioSheetPanel.statRadius
                color: Theme.c.audioSheetPanel.statBackground
                border.color: Theme.c.audioSheetPanel.statBorder
                border.width: 1

                Column {
                    id: statColumn
                    anchors.centerIn: parent

                    Text {
                        text: stat.modelData.label
                        color: Theme.c.audioSheetPanel.label
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                    }

                    Text {
                        text: stat.modelData.value
                        color: stat.modelData.tone
                        font.family: Theme.monoFontFamily
                        font.pixelSize: Theme.fontSizeSm
                        font.weight: Font.Medium
                    }
                }
            }
        }
    }

    // Phase 3: tag list and vocabulary search land here, once the audio model addons exist.
    Item {
        id: tagsSlot
        anchors.top: summary.visible ? summary.bottom : controls.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 0
    }

    EmptyState {
        anchors.top: tagsSlot.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingLg
        visible: !root.resultOk
        glyph: root.busy ? Theme.icons.spinner : Theme.icons.audioLines
        glyphSpinning: root.busy
        title: root.busy ? qsTr("Analysing audio…")
                         : (root.result.ok === false ? qsTr("Couldn’t build the sheet.")
                                                     : qsTr("No sheet yet."))
        hint: root.busy ? "" : (root.result.ok === false ? (root.result.detail ?? "")
                                                         : qsTr("Pick what to analyse and press Generate to see loudness, spectrogram and sound events laid out against your cuts."))
    }

    ImageViewer {
        id: viewer
        anchors.top: tagsSlot.bottom
        anchors.topMargin: Theme.spacingSm
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        visible: root.resultOk
        source: root.resultOk ? root.result.url : ""
        imageWidth: root.resultOk ? root.result.w : 0
        imageHeight: root.resultOk ? root.result.h : 0
        path: root.resultOk ? root.result.path : ""
        heading: qsTr("Audio Sheet")
        details: root.resultOk ? qsTr("%1 – %2").arg(root.clock(root.result.start))
                                                 .arg(root.clock(root.result.start + root.result.duration)) : ""
        seekSeconds: root.resultOk ? root.result.start : -1
        saveName: "audio-sheet"
    }
}
