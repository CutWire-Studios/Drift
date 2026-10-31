pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Drift

PanelFrame {
    id: root
    SearchAnchor {
        searchId: "audio-sheet"
        title: qsTr("Audio Sheet")
        keywords: "audio sheet loudness lufs spectrogram events sync analysis report png export sections sound tags find search speech music"
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
    property bool withTags: true
    property string tagTab: "sections"
    property string searchTerm: ""
    property bool searchStarted: false

    property bool runtimeReady: Addons.runtimeAvailable()
    property bool clapReady: Addons.hasKind("audio-tag-model") && runtimeReady
    property bool astReady: Addons.hasKind("audio-class-model") && runtimeReady
    readonly property bool tagsReady: clapReady || astReady
    readonly property var vocabulary: clapReady ? mcp.audioVocabulary() : []
    readonly property var searchResult: mcp.audioSearchResult
    readonly property bool searchBusy: mcp.audioSearchBusy
    readonly property var sections: resultOk && result.sections ? result.sections : []
    readonly property var taggedEvents: resultOk && result.tagged_events ? result.tagged_events : []
    readonly property var presence: resultOk && result.presence ? result.presence : null

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

    function scopeOptions() {
        const options = {}
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
        return options
    }

    function generate() {
        const options = root.scopeOptions()
        options.width = root.sheetWidth
        options.frames = root.withFrames
        options.tags = root.withTags
        root.mcp.generateAudioSheet(options)
    }

    function search(term) {
        const options = root.scopeOptions()
        options.term = term
        root.searchTerm = term
        root.searchStarted = true
        root.mcp.searchAudioSheet(options)
    }

    readonly property var suggestions: {
        const text = findField.text.trim().toLowerCase()
        if (text.length === 0 || root.vocabulary.indexOf(text) >= 0)
            return []
        const out = []
        for (const term of root.vocabulary) {
            if (term.toLowerCase().indexOf(text) >= 0)
                out.push(term)
            if (out.length >= 5)
                break
        }
        return out
    }

    function clock(seconds) {
        const s = Math.max(0, seconds)
        return Math.floor(s / 60) + ":" + String(Math.floor(s % 60)).padStart(2, "0")
    }

    function searchErrorText() {
        const r = root.searchResult
        if (r.closest && r.closest.length > 0)
            return qsTr("Not a sound Drift knows. Closest: %1").arg(r.closest.slice(0, 4).join(", "))
        return r.detail ?? ""
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

    Connections {
        target: Addons
        function onKindChanged(kind) {
            if (kind !== "audio-tag-model" && kind !== "audio-class-model" && kind !== "onnxruntime")
                return
            root.runtimeReady = Addons.runtimeAvailable()
            root.clapReady = Addons.hasKind("audio-tag-model") && root.runtimeReady
            root.astReady = Addons.hasKind("audio-class-model") && root.runtimeReady
        }
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

        ThemedSwitch {
            visible: root.tagsReady
            text: qsTr("Sound tags")
            enabled: !root.busy
            checked: root.withTags
            onToggled: root.withTags = checked
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

    Item {
        id: tagsSlot
        anchors.top: summary.visible ? summary.bottom : controls.bottom
        anchors.topMargin: Theme.spacingSm
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.spacingMd
        anchors.rightMargin: Theme.spacingMd
        height: installPrompt.visible ? installPrompt.height : tagsColumn.height

        Rectangle {
            id: installPrompt
            visible: !root.tagsReady
            width: parent.width
            height: promptRow.height + Theme.spacingMd * 2
            radius: Theme.c.audioSheetPanel.statRadius
            color: Theme.c.audioSheetPanel.statBackground
            border.color: Theme.c.audioSheetPanel.statBorder
            border.width: 1

            Row {
                id: promptRow
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: Theme.spacingMd
                spacing: Theme.spacingMd

                Text {
                    width: parent.width - installButton.width - parent.spacing
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.runtimeReady
                          ? qsTr("Name the sounds, split the audio into sections and find a sound by description with the audio tagging add-ons.")
                          : qsTr("Naming sounds needs the AI engine and the audio tagging add-ons.")
                    wrapMode: Text.WordWrap
                    color: Theme.c.audioSheetPanel.label
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                ThemedButton {
                    id: installButton
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Get add-ons")
                    variant: "primary"
                    glyph: Theme.icons.download
                    onClicked: ShellBridge.main.openAddonManager(root.runtimeReady ? "audio-tag-model" : "onnxruntime")
                }
            }
        }

        Column {
            id: tagsColumn
            visible: root.tagsReady
            width: parent.width
            spacing: Theme.spacingSm

            Row {
                spacing: Theme.spacingSm

                Repeater {
                    model: [
                        { id: "sections", label: qsTr("Sections") },
                        { id: "sounds", label: qsTr("Sounds") },
                        { id: "find", label: qsTr("Find") }
                    ]
                    delegate: ThemedChip {
                        required property var modelData
                        text: modelData.label
                        enabled: modelData.id !== "find" || root.clapReady
                        selected: root.tagTab === modelData.id
                        onClicked: root.tagTab = modelData.id
                    }
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: root.presence !== null
                    readonly property var p: root.presence
                    text: p ? qsTr("Speech %1  ·  Music %2").arg(root.clock(p.speech_seconds)).arg(root.clock(p.music_seconds)) : ""
                    color: Theme.c.audioSheetPanel.label
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                ThemedButton {
                    visible: root.resultOk && !root.astReady && root.tagTab === "sections"
                    text: qsTr("Add speech & music")
                    variant: "secondary"
                    onClicked: ShellBridge.main.openAddonManager("audio-class-model")
                }
            }

            ThemedTextField {
                id: findField
                visible: root.tagTab === "find"
                width: parent.width
                placeholderText: qsTr("Find a sound, e.g. heavy rain or a fast whoosh")
                enabled: !root.searchBusy
                onAccepted: {
                    const typed = findField.text.trim()
                    if (typed.length === 0)
                        return
                    root.search(root.suggestions.length > 0 ? root.suggestions[0] : typed)
                }
            }

            Rectangle {
                id: listFrame
                width: parent.width
                height: 150
                radius: Theme.c.audioSheetPanel.statRadius
                color: Theme.c.audioSheetPanel.listBackground
                border.color: Theme.c.audioSheetPanel.listBorder
                border.width: 1
                clip: true

                ListView {
                    id: list
                    anchors.fill: parent
                    anchors.margins: 1
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: AppScrollBar {}
                    model: root.tagTab === "sections" ? root.sections
                           : root.tagTab === "sounds" ? root.taggedEvents
                           : (root.suggestions.length > 0 ? root.suggestions
                              : (root.searchStarted && root.searchResult.ok === true ? root.searchResult.moments : []))

                    delegate: Rectangle {
                        id: row
                        required property var modelData
                        readonly property bool isTerm: root.tagTab === "find" && root.suggestions.length > 0
                        readonly property real at: isTerm ? -1 : (modelData.start ?? modelData.at ?? -1)
                        width: list.width
                        height: Theme.controlHeightSm
                        color: rowHover.hovered ? Theme.c.audioSheetPanel.rowHover : "transparent"

                        Text {
                            id: timeText
                            anchors.left: parent.left
                            anchors.leftMargin: Theme.spacingMd
                            anchors.verticalCenter: parent.verticalCenter
                            width: row.isTerm ? 0 : 92
                            visible: !row.isTerm
                            text: root.tagTab === "sounds" ? root.clock(row.at) + "." + Math.floor((row.at % 1) * 10)
                                                           : root.clock(row.at) + " – " + root.clock(row.modelData.end)
                            color: Theme.c.audioSheetPanel.rowTime
                            font.family: Theme.monoFontFamily
                            font.pixelSize: Theme.fontSizeXs
                        }

                        Text {
                            anchors.left: timeText.right
                            anchors.leftMargin: row.isTerm ? Theme.spacingMd : 0
                            anchors.right: verdict.left
                            anchors.rightMargin: Theme.spacingMd
                            anchors.verticalCenter: parent.verticalCenter
                            elide: Text.ElideRight
                            text: row.isTerm ? row.modelData
                                  : root.tagTab === "sections" ? row.modelData.label
                                  : root.tagTab === "sounds" ? row.modelData.sound + "  " + row.modelData.label
                                  : (row.modelData.source === "event" ? qsTr("hit") : qsTr("window"))
                            color: Theme.c.audioSheetPanel.value
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeSm
                        }

                        Text {
                            id: verdict
                            anchors.right: parent.right
                            anchors.rightMargin: Theme.spacingMd
                            anchors.verticalCenter: parent.verticalCenter
                            visible: root.tagTab === "find" && !row.isTerm
                            text: root.tagTab === "find" && !row.isTerm ? qsTr("%1  z %2").arg(row.modelData.verdict ?? "").arg((row.modelData.z ?? 0).toFixed(1)) : ""
                            color: (row.modelData.z ?? 0) >= 3 ? Theme.c.audioSheetPanel.good
                                   : ((row.modelData.z ?? 0) >= 2.5 ? Theme.c.audioSheetPanel.warn : Theme.c.audioSheetPanel.label)
                            font.family: Theme.monoFontFamily
                            font.pixelSize: Theme.fontSizeXs
                        }

                        HoverHandler {
                            id: rowHover
                            cursorShape: Qt.PointingHandCursor
                        }
                        TapHandler {
                            onTapped: {
                                if (row.isTerm) {
                                    findField.text = row.modelData
                                    root.search(row.modelData)
                                } else if (row.at >= 0) {
                                    EditorState.playheadSeconds = row.at
                                }
                            }
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    width: parent.width - Theme.spacingLg * 2
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    visible: list.count === 0
                    text: root.tagTab === "find"
                          ? (root.searchBusy ? qsTr("Searching…")
                             : (root.searchStarted && root.searchResult.ok === false
                                ? root.searchErrorText()
                                : qsTr("Type one of the %1 sounds Drift knows, or pick a suggestion.").arg(root.vocabulary.length)))
                          : (root.busy ? qsTr("Analysing audio…")
                             : (root.resultOk ? qsTr("Nothing to show: turn on Sound tags and generate again.")
                                              : qsTr("Generate a sheet to name its sections and sounds.")))
                    color: Theme.c.audioSheetPanel.label
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
            }
        }
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
