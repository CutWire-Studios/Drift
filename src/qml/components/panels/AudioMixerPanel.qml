import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Drift

PanelFrame {
    id: root

    readonly property bool hasClip: EditorState.selectedTrack >= 0 && EditorState.selectedClip >= 0
    property var duckSources: []
    property int duckPreset: 1
    property real duckReduceDb: -12
    property real duckSensitivity: 0.5
    property real duckAttack: 0.2
    property real duckRelease: 0.5
    property real duckHold: 0.5
    property bool duckApplying: false
    property string duckText: ""
    property bool duckIsError: false
    readonly property bool voiceAvailable: EditorState.audioAlign.voiceDetectionAvailable()
    readonly property var duckPresets: [
        { reduceDb: -6, sensitivity: 0.5, attack: 0.3, release: 0.8, hold: 0.8 },
        { reduceDb: -12, sensitivity: 0.5, attack: 0.2, release: 0.5, hold: 0.5 },
        { reduceDb: -18, sensitivity: 0.6, attack: 0.1, release: 0.4, hold: 0.4 },
        { reduceDb: -40, sensitivity: 0.5, attack: 0.08, release: 0.3, hold: 0.3 }
    ]
    property bool canDuck: false
    property string loudnessText: qsTr("Loudness: not measured")
    property bool loudnessIsError: false
    property bool loudnessNormalizing: false

    Connections {
        target: EditorState
        function onSelectionChanged() {
            EditorState.cancelFocusedClipLoudness()
            root.loudnessText = qsTr("Loudness: not measured")
            root.loudnessIsError = false
            root.duckText = ""
            root.refreshDuck()
        }
        function onTracksChanged() { root.refreshDuck() }
        function onLoudnessFinished(result) {
            if (root.loudnessNormalizing)
                root.showResult(result, result.clipping
                                        ? qsTr("Normalized to %1 LUFS, but peaks clip").arg(result.target_lufs)
                                        : qsTr("Normalized to %1 LUFS").arg(result.target_lufs))
            else
                root.showResult(result, qsTr("Loudness: %1 LUFS, true peak %2 dBTP")
                                       .arg(result.lufs?.toFixed(1)).arg(result.true_peak_db?.toFixed(1)))
        }
    }

    function fmtDb(v) { return qsTr("%1 dB").arg(Math.round(v)) }
    function fmtPct(v) { return qsTr("%1%").arg(Math.round(v * 100)) }
    function fmtMs(v) { return qsTr("%1 ms").arg(Math.round(v)) }

    function refreshDuck() {
        duckSources = hasClip ? EditorState.audioAlign.duckSources(EditorState.selectedTrack, EditorState.selectedClip) : []
        canDuck = hasClip && EditorState.audioAlign.canDuck(EditorState.selectedTrack, EditorState.selectedClip)
    }

    function applyDuckPreset(index) {
        if (index < 0 || index >= duckPresets.length)
            return
        const p = duckPresets[index]
        duckApplying = true
        duckReduceDb = p.reduceDb
        duckSensitivity = p.sensitivity
        duckAttack = p.attack
        duckRelease = p.release
        duckHold = p.hold
        duckApplying = false
        duckPreset = index
    }

    function markDuckCustom() {
        if (!duckApplying)
            duckPreset = 4
    }

    function startDuck() {
        duckText = ""
        EditorState.audioAlign.duck(EditorState.selectedTrack, EditorState.selectedClip,
                                    duckOverBox.currentValue ?? -1, {
            reduceDb: duckReduceDb,
            attack: duckAttack,
            release: duckRelease,
            hold: duckHold,
            sensitivity: duckSensitivity,
            detect: duckDetectBox.currentValue ?? "auto"
        })
    }

    onHasClipChanged: refreshDuck()
    Component.onCompleted: refreshDuck()

    Connections {
        target: EditorState.audioAlign
        function onDuckFinished(result) {
            root.duckIsError = !result.ok
            root.duckText = result.detail ?? (result.ok ? qsTr("Ducked") : qsTr("Failed"))
        }
    }

    Connections {
        target: ShellBridge
        function onAnchorRevealed(id) {
            if (id === "audio-ducking")
                duckSection.expanded = true
        }
    }

    function showResult(result, text) {
        loudnessIsError = !result.ok
        loudnessText = result.ok ? text : (result.detail ?? result.error ?? qsTr("Failed"))
    }

    function analyze() {
        loudnessNormalizing = false
        EditorState.startFocusedClipLoudness(false, 0)
    }

    function normalize() {
        loudnessNormalizing = true
        EditorState.startFocusedClipLoudness(true, targetBox.currentValue)
    }

    AssetsPanelHeader {
        id: header
        width: parent.width
        height: Theme.panelHeaderHeight
        title: qsTr("Audio Mixer")
    }

    Rectangle {
        id: footer
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: Math.min(footerCol.implicitHeight + Theme.spacingMd * 2, Math.max(160, root.height * 0.6))
        color: Theme.c.audioMixerStrip.background

        Rectangle {
            anchors.top: parent.top
            width: parent.width
            height: 1
            color: Theme.c.audioMixerStrip.border
        }

        Flickable {
            anchors.fill: parent
            anchors.margins: Theme.spacingMd
            contentWidth: width
            contentHeight: footerCol.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            clip: true

        Column {
            id: footerCol
            width: parent.width
            spacing: Theme.spacingSm

            CollapsibleSection {
                id: duckSection
                title: qsTr("Ducking")
                tooltip: qsTr("Lower the selected music clip while other tracks are speaking")
                expanded: false
                showSeparator: false

                SearchAnchor {
                    searchId: "audio-ducking"
                    title: qsTr("Ducking")
                    keywords: "ducking duck sidechain lower music under voice auto duck"
                    target: duckSection
                }

                ColumnLayout {
                    width: parent.width
                    spacing: Theme.spacingSm

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSm
                        Text {
                            Layout.preferredWidth: 84
                            text: qsTr("Duck against")
                            color: Theme.c.audioMixerStrip.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs
                        }
                        ThemedComboBox {
                            id: duckOverBox
                            Layout.fillWidth: true
                            enabled: !EditorState.audioAlign.running
                            model: [{ label: qsTr("All other tracks"), value: -1 }].concat(
                                       root.duckSources.map(s => ({ label: s.name, value: s.track })))
                            textRole: "label"
                            valueRole: "value"
                            tooltip: qsTr("The tracks whose audio lowers the music")
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSm
                        Text {
                            Layout.preferredWidth: 84
                            text: qsTr("Detect")
                            color: Theme.c.audioMixerStrip.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs
                        }
                        ThemedComboBox {
                            id: duckDetectBox
                            Layout.fillWidth: true
                            enabled: !EditorState.audioAlign.running
                            model: root.voiceAvailable
                                   ? [{ label: qsTr("Auto"), value: "auto" },
                                      { label: qsTr("Level"), value: "level" },
                                      { label: qsTr("Voice"), value: "voice" }]
                                   : [{ label: qsTr("Auto"), value: "auto" },
                                      { label: qsTr("Level"), value: "level" }]
                            textRole: "label"
                            valueRole: "value"
                            tooltip: root.voiceAvailable
                                     ? qsTr("Auto uses voice detection when installed, otherwise loudness. Voice ignores music and noise.")
                                     : qsTr("Auto uses loudness only. Install the Voice activity (silero.vad) addon to enable Voice detection.")
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSm
                        Text {
                            Layout.preferredWidth: 84
                            text: qsTr("Preset")
                            color: Theme.c.audioMixerStrip.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs
                        }
                        ThemedComboBox {
                            id: duckPresetBox
                            Layout.fillWidth: true
                            enabled: !EditorState.audioAlign.running
                            model: [qsTr("Gentle"), qsTr("Standard"), qsTr("Voiceover"), qsTr("Silence"), qsTr("Custom")]
                            currentIndex: root.duckPreset
                            onActivated: function (index) { root.applyDuckPreset(index) }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSm
                        Text {
                            Layout.preferredWidth: 84
                            text: qsTr("Reduce by")
                            color: Theme.c.audioMixerStrip.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs
                        }
                        ThemedSlider {
                            id: duckReduceSlider
                            Layout.fillWidth: true
                            label: qsTr("Reduce by")
                            enabled: !EditorState.audioAlign.running
                            from: -40
                            to: -3
                            stepSize: 1
                            valueFormatter: function (v) { return root.fmtDb(v) }
                            Binding on value {
                                value: root.duckReduceDb
                                when: !duckReduceSlider.pressed
                            }
                            onMoved: {
                                root.duckReduceDb = value
                                root.markDuckCustom()
                            }
                        }
                        Text {
                            Layout.preferredWidth: 52
                            horizontalAlignment: Text.AlignRight
                            text: root.fmtDb(duckReduceSlider.value)
                            color: Theme.c.audioMixerStrip.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSm
                        Text {
                            Layout.preferredWidth: 84
                            text: qsTr("Sensitivity")
                            color: Theme.c.audioMixerStrip.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs
                        }
                        ThemedSlider {
                            id: duckSensSlider
                            Layout.fillWidth: true
                            label: qsTr("Sensitivity")
                            enabled: !EditorState.audioAlign.running
                            from: 0
                            to: 1
                            stepSize: 0.01
                            valueFormatter: function (v) { return root.fmtPct(v) }
                            Binding on value {
                                value: root.duckSensitivity
                                when: !duckSensSlider.pressed
                            }
                            onMoved: {
                                root.duckSensitivity = value
                                root.markDuckCustom()
                            }
                        }
                        Text {
                            Layout.preferredWidth: 52
                            horizontalAlignment: Text.AlignRight
                            text: root.fmtPct(duckSensSlider.value)
                            color: Theme.c.audioMixerStrip.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSm
                        Text {
                            Layout.preferredWidth: 84
                            text: qsTr("Fade down")
                            color: Theme.c.audioMixerStrip.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs
                        }
                        ThemedSlider {
                            id: duckAttackSlider
                            Layout.fillWidth: true
                            label: qsTr("Fade down")
                            enabled: !EditorState.audioAlign.running
                            from: 0
                            to: 1000
                            stepSize: 10
                            valueFormatter: function (v) { return root.fmtMs(v) }
                            Binding on value {
                                value: root.duckAttack * 1000
                                when: !duckAttackSlider.pressed
                            }
                            onMoved: {
                                root.duckAttack = value / 1000
                                root.markDuckCustom()
                            }
                        }
                        Text {
                            Layout.preferredWidth: 52
                            horizontalAlignment: Text.AlignRight
                            text: root.fmtMs(duckAttackSlider.value)
                            color: Theme.c.audioMixerStrip.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSm
                        Text {
                            Layout.preferredWidth: 84
                            text: qsTr("Fade up")
                            color: Theme.c.audioMixerStrip.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs
                        }
                        ThemedSlider {
                            id: duckReleaseSlider
                            Layout.fillWidth: true
                            label: qsTr("Fade up")
                            enabled: !EditorState.audioAlign.running
                            from: 0
                            to: 2000
                            stepSize: 10
                            valueFormatter: function (v) { return root.fmtMs(v) }
                            Binding on value {
                                value: root.duckRelease * 1000
                                when: !duckReleaseSlider.pressed
                            }
                            onMoved: {
                                root.duckRelease = value / 1000
                                root.markDuckCustom()
                            }
                        }
                        Text {
                            Layout.preferredWidth: 52
                            horizontalAlignment: Text.AlignRight
                            text: root.fmtMs(duckReleaseSlider.value)
                            color: Theme.c.audioMixerStrip.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSm
                        ThemedButton {
                            text: qsTr("Duck music")
                            variant: "primary"
                            glyph: Theme.icons.volumeHigh
                            tooltip: root.canDuck ? qsTr("Lower the selected clip under the other tracks' audio")
                                                  : qsTr("Select a music clip with other audio overlapping it")
                            enabled: root.canDuck && !EditorState.audioAlign.running
                            onClicked: root.startDuck()
                        }
                        ThemedButton {
                            text: qsTr("Cancel")
                            variant: "secondary"
                            visible: EditorState.audioAlign.running
                            onClicked: EditorState.audioAlign.cancel()
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: !EditorState.audioAlign.running
                            text: root.duckText
                            wrapMode: Text.WordWrap
                            color: root.duckIsError ? Theme.c.audioMixerStrip.destructive
                                                    : Theme.c.audioMixerStrip.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            visible: EditorState.audioAlign.running
                            spacing: Theme.spacingXs
                            Text {
                                Layout.fillWidth: true
                                text: EditorState.audioAlign.status
                                elide: Text.ElideRight
                                color: Theme.c.audioMixerStrip.foreground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeXs
                            }
                            ThemedProgressBar {
                                Layout.fillWidth: true
                                barHeight: 4
                                value: EditorState.audioAlign.progress
                            }
                        }
                    }
                }
            }


            Row {
                width: parent.width
                spacing: Theme.spacingSm

                Text {
                    width: Math.max(0, parent.width - analyzeBtn.width - parent.spacing)
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !EditorState.loudnessBusy
                    text: root.loudnessText
                    elide: Text.ElideRight
                    color: root.loudnessIsError ? Theme.c.audioMixerStrip.destructive
                                                : Theme.c.audioMixerStrip.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                Column {
                    width: Math.max(0, parent.width - analyzeBtn.width - parent.spacing)
                    anchors.verticalCenter: parent.verticalCenter
                    visible: EditorState.loudnessBusy
                    spacing: Theme.spacingXs

                    Text {
                        text: qsTr("Measuring… %1%").arg(Math.round(EditorState.loudnessProgress * 100))
                        color: Theme.c.audioMixerStrip.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                    }

                    ThemedProgressBar {
                        width: parent.width
                        barHeight: 4
                        value: EditorState.loudnessProgress
                    }
                }

                ThemedButton {
                    id: analyzeBtn
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Analyze")
                    variant: "secondary"
                    glyph: Theme.icons.audioLines
                    tooltip: root.hasClip ? qsTr("Measure loudness of the selected clip")
                                          : qsTr("Select a clip first")
                    enabled: root.hasClip && !EditorState.loudnessBusy
                    onClicked: root.analyze()
                }
            }

            Row {
                spacing: Theme.spacingSm

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Normalize to")
                    color: Theme.c.audioMixerStrip.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                ThemedComboBox {
                    id: targetBox
                    anchors.verticalCenter: parent.verticalCenter
                    width: 110
                    enabled: root.hasClip
                    model: [
                        { label: qsTr("-14 LUFS"), value: -14 },
                        { label: qsTr("-16 LUFS"), value: -16 },
                        { label: qsTr("-23 LUFS"), value: -23 }
                    ]
                    textRole: "label"
                    valueRole: "value"
                }

                ThemedButton {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Normalize")
                    variant: "secondary"
                    glyph: Theme.icons.gauge
                    tooltip: root.hasClip ? qsTr("Set the selected clip's volume to the chosen loudness")
                                          : qsTr("Select a clip first")
                    enabled: root.hasClip && !EditorState.loudnessBusy
                    onClicked: root.normalize()
                }
            }
        }
        }
    }

    AudioMixerStrip {
        anchors.top: header.bottom
        anchors.bottom: footer.top
        width: parent.width
        resizing: true
        embedded: true
    }
}
