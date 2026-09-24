import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

// Audio mixer strip docked on the right side at the end of the timeline.
// Displays per-audio-track channel strips (A1, A2, ...) and a Master strip.
// Includes track label, Mute/Solo/Arm buttons, rotary Pan knob with numeric readout,
// dual-channel vertical stereo VU peak meters with dB markings & peak hold,
// vertical volume fader slider, and bottom numeric dB readout.
Rectangle {
    id: root

    // Docked at the end of the timeline
    readonly property bool isRecording: EditorState.isRecordingAudio
    readonly property bool mixerVisible: EditorState.audioMixerVisible || isRecording
    readonly property real minMixerWidth: isRecording ? 250 : 130
    width: mixerVisible ? Math.max(minMixerWidth, stripsRow.implicitWidth + 2) : 0
    visible: width > 0
    clip: true

    readonly property color hoverBg: Theme.popoverHover
    readonly property color headerBg: Theme.darkMode
        ? Qt.rgba(Theme.panelForeground.r, Theme.panelForeground.g, Theme.panelForeground.b, 0.06)
        : Qt.rgba(0, 0, 0, 0.04)

    color: Theme.panelBackground
    border.color: Theme.panelBorder
    border.width: 1

    Behavior on width {
        NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing }
    }

    // --- Piecewise dB <-> Y-Ratio Conversions ---------------------------------
    // Scale markings: 6, 0, -2, -5, -10, -15, -20, -30, -45, -60
    readonly property var dbPoints: [
        { db: 6.0,  r: 1.00 },
        { db: 0.0,  r: 0.85 },
        { db: -2.0, r: 0.79 },
        { db: -5.0, r: 0.70 },
        { db: -10.0, r: 0.57 },
        { db: -15.0, r: 0.46 },
        { db: -20.0, r: 0.36 },
        { db: -30.0, r: 0.22 },
        { db: -45.0, r: 0.10 },
        { db: -60.0, r: 0.00 }
    ]

    function dbToRatio(db) {
        if (db <= -60.0) return 0.0
        if (db >= 6.0) return 1.0
        for (var i = 0; i < dbPoints.length - 1; i++) {
            const p1 = dbPoints[i]
            const p2 = dbPoints[i + 1]
            if (db <= p1.db && db >= p2.db) {
                const frac = (db - p2.db) / (p1.db - p2.db)
                return p2.r + frac * (p1.r - p2.r)
            }
        }
        return 0.0
    }

    function ratioToDb(ratio) {
        if (ratio <= 0.0) return -60.0
        if (ratio >= 1.0) return 6.0
        for (var i = 0; i < dbPoints.length - 1; i++) {
            const p1 = dbPoints[i]
            const p2 = dbPoints[i + 1]
            if (ratio <= p1.r && ratio >= p2.r) {
                const frac = (ratio - p2.r) / (p1.r - p2.r)
                return p2.db + frac * (p1.db - p2.db)
            }
        }
        return -60.0
    }

    function linearToDb(gain) {
        if (gain <= 0.0009) return -60.0
        return Math.max(-60.0, Math.min(6.0, 20.0 * Math.log10(gain)))
    }

    function dbToLinear(db) {
        if (db <= -59.5) return 0.0
        return Math.pow(10.0, db / 20.0)
    }

    function formatDb(db) {
        if (db <= -59.5) return "-∞dB"
        const sign = db > 0.005 ? "+" : (db < -0.005 ? "-" : "")
        return sign + Math.abs(db).toFixed(2) + "dB"
    }

    function formatPan(pan) {
        const val = Math.round(pan * 100)
        if (val === 0) return "0"
        if (val < 0) return "< " + Math.abs(val)
        return "> " + val
    }

    // Audio tracks model: filters project tracks to only non-nested audio tracks
    readonly property var audioTracksList: {
        const result = []
        const allTracks = EditorState.tracks
        var audioCount = 0
        for (var i = 0; i < allTracks.length; i++) {
            const t = allTracks[i]
            if (t.type === "audio" && !t.isAdjustmentLane) {
                audioCount++
                result.push({
                    trackIndex: i,
                    ordinal: audioCount,
                    name: t.name && t.name.length > 0 ? t.name : ("A" + audioCount),
                    shortName: "A" + audioCount,
                    muted: t.muted === true,
                    solo: t.solo === true,
                    volume: t.volume !== undefined ? t.volume : 1.0,
                    pan: t.pan !== undefined ? t.pan : 0.0
                })
            }
        }
        return result
    }

    // --- Metering Animation Engine -------------------------------------------
    property var liveTrackLevels: ({})
    property var liveTrackPeaks: ({})
    property var livePeakHoldTimes: ({})

    property real masterLevelL: 0.0
    property real masterLevelR: 0.0
    property real masterPeakL: 0.0
    property real masterPeakR: 0.0
    property real masterPeakHoldTimeL: 0
    property real masterPeakHoldTimeR: 0

    Timer {
        id: meterTimer
        interval: 30
        repeat: true
        running: root.visible && (EditorState.playing || EditorState.isRecordingAudio)
        onTriggered: {
            const now = Date.now()
            const decay = 0.04
            const peakDecay = 0.02
            const newLevels = {}
            const newPeaks = {}
            const newTimes = Object.assign({}, root.livePeakHoldTimes)

            // Update each audio track
            for (var i = 0; i < root.audioTracksList.length; i++) {
                const t = root.audioTracksList[i]
                const ti = t.trackIndex
                const raw = EditorState.trackAudioLevels(ti)
                const targetL = raw ? (raw.left || 0.0) : 0.0
                const targetR = raw ? (raw.right || 0.0) : 0.0

                const cur = root.liveTrackLevels[ti] || { l: 0.0, r: 0.0 }
                const curPeaks = root.liveTrackPeaks[ti] || { l: 0.0, r: 0.0 }
                var holdTimes = newTimes[ti] || { l: 0, r: 0 }

                // Smooth bar levels (instant attack, exponential decay)
                const l = targetL >= cur.l ? targetL : Math.max(0.0, cur.l - decay)
                const r = targetR >= cur.r ? targetR : Math.max(0.0, cur.r - decay)

                // Peak hold L
                var pkL = curPeaks.l
                if (targetL >= pkL) {
                    pkL = targetL
                    holdTimes.l = now
                } else if (now - holdTimes.l > 1200) {
                    pkL = Math.max(0.0, pkL - peakDecay)
                }

                // Peak hold R
                var pkR = curPeaks.r
                if (targetR >= pkR) {
                    pkR = targetR
                    holdTimes.r = now
                } else if (now - holdTimes.r > 1200) {
                    pkR = Math.max(0.0, pkR - peakDecay)
                }

                newLevels[ti] = { l: l, r: r }
                newPeaks[ti] = { l: pkL, r: pkR }
                newTimes[ti] = holdTimes
            }
            root.liveTrackLevels = newLevels
            root.liveTrackPeaks = newPeaks
            root.livePeakHoldTimes = newTimes

            // Update master levels
            const mRaw = EditorState.masterAudioLevels()
            const mTargetL = mRaw ? (mRaw.left || 0.0) : 0.0
            const mTargetR = mRaw ? (mRaw.right || 0.0) : 0.0

            root.masterLevelL = mTargetL >= root.masterLevelL ? mTargetL : Math.max(0.0, root.masterLevelL - decay)
            root.masterLevelR = mTargetR >= root.masterLevelR ? mTargetR : Math.max(0.0, root.masterLevelR - decay)

            if (mTargetL >= root.masterPeakL) {
                root.masterPeakL = mTargetL
                root.masterPeakHoldTimeL = now
            } else if (now - root.masterPeakHoldTimeL > 1200) {
                root.masterPeakL = Math.max(0.0, root.masterPeakL - peakDecay)
            }

            if (mTargetR >= root.masterPeakR) {
                root.masterPeakR = mTargetR
                root.masterPeakHoldTimeR = now
            } else if (now - root.masterPeakHoldTimeR > 1200) {
                root.masterPeakR = Math.max(0.0, root.masterPeakR - peakDecay)
            }
        }
    }

    // Reset levels when playback stops
    Connections {
        target: EditorState
        function onPlayingChanged() {
            if (!EditorState.playing && !EditorState.isRecordingAudio) {
                root.liveTrackLevels = ({})
                root.liveTrackPeaks = ({})
                root.masterLevelL = 0.0
                root.masterLevelR = 0.0
                root.masterPeakL = 0.0
                root.masterPeakR = 0.0
            }
        }
        function onAudioRecordingStateChanged() {
            if (!EditorState.playing && !EditorState.isRecordingAudio) {
                root.liveTrackLevels = ({})
                root.liveTrackPeaks = ({})
                root.masterLevelL = 0.0
                root.masterLevelR = 0.0
                root.masterPeakL = 0.0
                root.masterPeakR = 0.0
            }
        }
    }

    Column {
        anchors.fill: parent

        // === Panel Top Header ================================================
        Rectangle {
            id: topHeader
            width: parent.width
            height: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
            color: Theme.panelBackground
            border.color: Theme.panelBorder
            border.width: 1

            Row {
                anchors.left: parent.left
                anchors.leftMargin: 8
                anchors.right: headerCloseBtn.left
                anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6
                clip: true

                IconGlyph {
                    glyph: Theme.icons.audioLines
                    iconSize: 14
                    iconColor: Theme.primary
                    anchors.verticalCenter: parent.verticalCenter
                }

                Text {
                    text: qsTr("Audio Mixer")
                    color: Theme.panelForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm
                    font.bold: true
                    elide: Text.ElideRight
                    width: Math.max(0, parent.width - 20)
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            IconButton {
                id: headerCloseBtn
                anchors.right: parent.right
                anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                glyph: Theme.icons.x
                variant: "text"
                tooltip: qsTr("Close audio mixer")
                onClicked: EditorState.audioMixerVisible = false
            }
        }

        // === Dedicated Voiceover Recording Section (active during voiceover recording) ===
        Rectangle {
            id: voPanel
            visible: EditorState.isRecordingAudio
            width: parent.width
            height: visible ? 52 : 0
            clip: true
            color: EditorState.isAudioRecordingPaused
                   ? (Theme.darkMode ? Qt.rgba(0.9, 0.7, 0.1, 0.1) : Qt.rgba(0.9, 0.7, 0.1, 0.06))
                   : (Theme.darkMode ? Qt.rgba(0.9, 0.1, 0.1, 0.08) : Qt.rgba(0.9, 0.1, 0.1, 0.04))
            border.color: EditorState.isAudioRecordingPaused ? "#eab308" : (Theme.darkMode ? Qt.rgba(0.9, 0.1, 0.1, 0.3) : Qt.rgba(0.9, 0.1, 0.1, 0.2))
            border.width: 1

            Behavior on height {
                NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
            }

            Column {
                anchors.fill: parent
                anchors.margins: 4
                spacing: 3

                // --- Row 1: Recording Status, Timer & Transport Action Buttons ---
                Item {
                    width: parent.width
                    height: 22

                    // Status pill & timer
                    Row {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 5

                        Rectangle {
                            width: 8
                            height: 8
                            radius: 4
                            color: EditorState.isAudioRecordingPaused ? "#eab308" : Theme.destructive
                            anchors.verticalCenter: parent.verticalCenter
                            SequentialAnimation on opacity {
                                running: voPanel.visible && !EditorState.isAudioRecordingPaused
                                loops: Animation.Infinite
                                NumberAnimation { to: 0.2; duration: 400 }
                                NumberAnimation { to: 1.0; duration: 400 }
                            }
                        }

                        Text {
                            text: EditorState.isAudioRecordingPaused ? qsTr("PAUSED") : qsTr("REC")
                            font.bold: true
                            font.pixelSize: 10
                            color: EditorState.isAudioRecordingPaused ? "#d97706" : Theme.destructive
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        Text {
                            text: {
                                const secs = EditorState.audioRecordSeconds
                                const m = Math.floor(secs / 60)
                                const s = Math.floor(secs % 60)
                                const ms = Math.floor((secs % 1) * 10)
                                return (m < 10 ? "0" + m : m) + ":" + (s < 10 ? "0" + s : s) + "." + ms
                            }
                            font.family: Theme.monoFontFamily
                            font.bold: true
                            font.pixelSize: 11
                            color: Theme.panelForeground
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    // Transport Actions (Pause, Done, Discard)
                    Row {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2

                        IconButton {
                            glyph: EditorState.isAudioRecordingPaused ? Theme.icons.play : Theme.icons.pause
                            variant: "text"
                            anchors.verticalCenter: parent.verticalCenter
                            tooltip: EditorState.isAudioRecordingPaused ? qsTr("Resume recording") : qsTr("Pause recording")
                            onClicked: EditorState.toggleAudioRecordingPause()
                        }

                        IconButton {
                            glyph: Theme.icons.check
                            variant: "text"
                            anchors.verticalCenter: parent.verticalCenter
                            tooltip: qsTr("Done — save recording to track")
                            onClicked: EditorState.stopAudioRecording()
                        }

                        IconButton {
                            glyph: Theme.icons.trash
                            variant: "text"
                            anchors.verticalCenter: parent.verticalCenter
                            tooltip: qsTr("Discard — cancel recording")
                            onClicked: EditorState.cancelAudioRecording()
                        }
                    }
                }

                // --- Row 2: Microphone Switcher ---
                Row {
                    width: parent.width
                    height: 22
                    spacing: 6

                    // Microphone Picker Button
                    Item {
                        id: voMicPickerBtn
                        width: Math.min(parent.width, 180)
                        height: 20
                        anchors.verticalCenter: parent.verticalCenter

                        Rectangle {
                            anchors.fill: parent
                            radius: Theme.radiusSm
                            color: voMicMouse.containsMouse || voMicMenu.opened
                                   ? Theme.popoverHover
                                   : (Theme.darkMode ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(0, 0, 0, 0.06))
                            border.color: Theme.panelBorder
                            border.width: 1
                        }

                        Row {
                            anchors.left: parent.left
                            anchors.leftMargin: 6
                            anchors.right: parent.right
                            anchors.rightMargin: 6
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 4

                            IconGlyph {
                                glyph: Theme.icons.mic
                                iconSize: 11
                                iconColor: Theme.panelForeground
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Text {
                                text: EditorState.currentMicrophoneName || qsTr("Default Mic")
                                font.pixelSize: 10
                                color: Theme.panelForeground
                                elide: Text.ElideRight
                                width: Math.max(10, voMicPickerBtn.width - 40)
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            IconGlyph {
                                glyph: Theme.icons.chevronDown
                                iconSize: 9
                                iconColor: Theme.mutedForeground
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }

                        MouseArea {
                            id: voMicMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: voMicMenu.popup(0, voMicPickerBtn.height + 2)
                        }

                        ThemedContextMenu {
                            id: voMicMenu
                            implicitWidth: 220

                            Instantiator {
                                model: EditorState.availableMicrophones
                                delegate: ThemedMenuItem {
                                    required property var modelData
                                    text: modelData.name
                                    icon.name: modelData.name === EditorState.currentMicrophoneName ? Theme.icons.check : ""
                                    onTriggered: EditorState.selectMicrophone(modelData.id)
                                }
                                onObjectAdded: (index, object) => voMicMenu.insertItem(index, object)
                                onObjectRemoved: (index, object) => voMicMenu.removeItem(object)
                            }
                        }

                        ThemedToolTip {
                            visible: voMicMouse.containsMouse && !voMicMenu.opened
                            text: qsTr("Microphone: %1 (click to switch)").arg(EditorState.currentMicrophoneName)
                        }
                    }
                }
            }
        }

        // === Channel Strips Area =============================================
        Flickable {
            id: stripsFlick
            width: parent.width
            height: parent.height - topHeader.height - (voPanel.visible ? voPanel.height : 0)
            contentWidth: stripsRow.implicitWidth
            contentHeight: height
            boundsBehavior: Flickable.StopAtBounds
            clip: true

            ScrollBar.horizontal: AppScrollBar { policy: ScrollBar.AsNeeded }

            Row {
                id: stripsRow
                height: parent.height

                // Empty state when no audio tracks exist
                Item {
                    id: addTrackEmptyState
                    visible: root.audioTracksList.length === 0
                    width: visible ? 62 : 0
                    height: parent.height

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 4
                        radius: 4
                        color: addTrackMouse.containsMouse ? root.hoverBg : "transparent"
                        border.color: addTrackMouse.containsMouse ? Theme.primary : Theme.panelBorder
                        border.width: 1

                        Column {
                            anchors.centerIn: parent
                            spacing: 8
                            width: parent.width - 8

                            Rectangle {
                                width: 28
                                height: 28
                                radius: 14
                                anchors.horizontalCenter: parent.horizontalCenter
                                color: addTrackMouse.containsMouse
                                    ? Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.15)
                                    : Qt.rgba(Theme.panelForeground.r, Theme.panelForeground.g, Theme.panelForeground.b, 0.06)

                                IconGlyph {
                                    anchors.centerIn: parent
                                    glyph: Theme.icons.plus
                                    iconSize: 14
                                    iconColor: addTrackMouse.containsMouse ? Theme.primary : Theme.mutedForeground
                                }
                            }

                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: qsTr("Add\nAudio\nTrack")
                                horizontalAlignment: Text.AlignHCenter
                                font.family: Theme.fontFamily
                                font.pixelSize: 10
                                font.bold: true
                                color: addTrackMouse.containsMouse ? Theme.primary : Theme.mutedForeground
                                lineHeight: 1.1
                            }
                        }

                        ThemedToolTip {
                            visible: addTrackMouse.containsMouse
                            text: qsTr("Click to add an audio track")
                        }

                        MouseArea {
                            id: addTrackMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: EditorState.addTrack("audio")
                        }
                    }

                    // Right border divider
                    Rectangle {
                        anchors.right: parent.right
                        width: 1
                        height: parent.height
                        color: Theme.panelBorder
                    }
                }

                // Channel strip per audio track
                Repeater {
                    model: root.audioTracksList

                    delegate: Item {
                        id: trackStrip
                        required property var modelData
                        required property int index

                        readonly property int trackIndex: modelData.trackIndex
                        readonly property string trackName: modelData.name
                        readonly property string shortName: modelData.shortName
                        readonly property bool trackMuted: modelData.muted
                        readonly property bool trackSolo: modelData.solo
                        readonly property real trackVol: modelData.volume
                        readonly property real trackPan: modelData.pan
                        readonly property bool isRecordingHere:
                            EditorState.isRecordingAudio && EditorState.recordingTrackIndex === trackIndex

                        readonly property var curLevels: root.liveTrackLevels[trackIndex] || { l: 0.0, r: 0.0 }
                        readonly property var curPeaks: root.liveTrackPeaks[trackIndex] || { l: 0.0, r: 0.0 }

                        width: 62
                        height: parent.height

                        // Channel strip background
                        Rectangle {
                            anchors.fill: parent
                            color: trackStrip.isRecordingHere
                                   ? (Theme.darkMode ? Qt.rgba(0.9, 0.1, 0.1, 0.08) : Qt.rgba(0.9, 0.1, 0.1, 0.04))
                                   : "transparent"
                        }

                        // Right border divider
                        Rectangle {
                            anchors.right: parent.right
                            width: 1
                            height: parent.height
                            color: Theme.panelBorder
                        }

                        Column {
                            anchors.fill: parent
                            anchors.margins: 2
                            spacing: 2

                            // --- 1. Track Name Header ------------------------
                            Rectangle {
                                width: parent.width
                                height: 22
                                radius: 2
                                color: trackStrip.isRecordingHere
                                       ? (Theme.darkMode ? Qt.rgba(0.9, 0.1, 0.1, 0.25) : Qt.rgba(0.9, 0.1, 0.1, 0.12))
                                       : root.headerBg
                                border.color: trackStrip.isRecordingHere ? Theme.destructive : Theme.panelBorder
                                border.width: 1

                                Text {
                                    anchors.centerIn: parent
                                    text: trackStrip.shortName
                                    font.bold: true
                                    font.pixelSize: 11
                                    color: trackStrip.isRecordingHere ? Theme.destructive : Theme.panelForeground
                                }

                                ThemedToolTip {
                                    visible: headerMouse.containsMouse
                                    text: trackStrip.trackName + (trackStrip.isRecordingHere ? qsTr(" (Recording)") : "")
                                }
                                MouseArea {
                                    id: headerMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                }
                            }

                            // --- 2. 3-Button Row: Mute, Solo, Record ---------
                            Row {
                                anchors.horizontalCenter: parent.horizontalCenter
                                spacing: 2
                                height: 22

                                // Mute Button
                                Rectangle {
                                    width: 18
                                    height: 18
                                    radius: 2
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: trackStrip.trackMuted
                                           ? Qt.rgba(Theme.destructive.r, Theme.destructive.g, Theme.destructive.b, 0.25)
                                           : (muteMouse.containsMouse ? root.hoverBg : "transparent")
                                    border.color: trackStrip.trackMuted ? Theme.destructive : "transparent"
                                    border.width: 1

                                    IconGlyph {
                                        anchors.centerIn: parent
                                        glyph: trackStrip.trackMuted ? Theme.icons.volumeOff : Theme.icons.volumeHigh
                                        iconSize: 12
                                        iconColor: trackStrip.trackMuted ? Theme.destructive : Theme.mutedForeground
                                    }

                                    ThemedToolTip {
                                        visible: muteMouse.containsMouse
                                        text: trackStrip.trackMuted ? qsTr("Unmute") : qsTr("Mute")
                                    }
                                    MouseArea {
                                        id: muteMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: EditorState.setTrackMuted(trackStrip.trackIndex, !trackStrip.trackMuted)
                                    }
                                }

                                // Solo Button
                                Rectangle {
                                    width: 18
                                    height: 18
                                    radius: 2
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: trackStrip.trackSolo
                                           ? Qt.rgba(0.2, 0.5, 0.9, 0.25)
                                           : (soloMouse.containsMouse ? root.hoverBg : "transparent")
                                    border.color: trackStrip.trackSolo ? "#38bdf8" : "transparent"
                                    border.width: 1

                                    IconGlyph {
                                        anchors.centerIn: parent
                                        glyph: Theme.icons.headphones
                                        iconSize: 12
                                        iconColor: trackStrip.trackSolo ? "#38bdf8" : Theme.mutedForeground
                                    }

                                    ThemedToolTip {
                                        visible: soloMouse.containsMouse
                                        text: trackStrip.trackSolo ? qsTr("Unsolo") : qsTr("Solo")
                                    }
                                    MouseArea {
                                        id: soloMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: EditorState.setTrackSolo(trackStrip.trackIndex, !trackStrip.trackSolo)
                                    }
                                }

                                // Record Arm Button (Red Dot 🔴)
                                Rectangle {
                                    width: 18
                                    height: 18
                                    radius: 2
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: trackStrip.isRecordingHere
                                           ? Qt.rgba(0.9, 0.1, 0.1, 0.3)
                                           : (recMouse.containsMouse ? root.hoverBg : "transparent")
                                    border.color: trackStrip.isRecordingHere ? Theme.destructive : "transparent"
                                    border.width: 1

                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: 8
                                        height: 8
                                        radius: 4
                                        color: trackStrip.isRecordingHere
                                               ? (EditorState.isAudioRecordingPaused ? "#eab308" : Theme.destructive)
                                               : (recMouse.containsMouse ? Theme.destructive : "#7f1d1d")

                                        SequentialAnimation on opacity {
                                            running: trackStrip.isRecordingHere && !EditorState.isAudioRecordingPaused
                                            loops: Animation.Infinite
                                            NumberAnimation { to: 0.2; duration: 400 }
                                            NumberAnimation { to: 1.0; duration: 400 }
                                        }
                                    }

                                    ThemedToolTip {
                                        visible: recMouse.containsMouse
                                        text: trackStrip.isRecordingHere
                                              ? (EditorState.isAudioRecordingPaused ? qsTr("Recording paused — click to finish") : qsTr("Recording — click to finish"))
                                              : qsTr("Record voiceover on %1").arg(trackStrip.shortName)
                                    }
                                    MouseArea {
                                        id: recMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            if (trackStrip.isRecordingHere) {
                                                EditorState.stopAudioRecording()
                                            } else {
                                                EditorState.startAudioRecording(trackStrip.trackIndex)
                                            }
                                        }
                                    }
                                }
                            }

                            // --- 3. Circular Rotary Pan Knob -----------------
                            Item {
                                width: 36
                                height: 36
                                anchors.horizontalCenter: parent.horizontalCenter

                                Canvas {
                                    id: panCanvas
                                    anchors.fill: parent
                                    antialiasing: true

                                    readonly property real currentPan: trackStrip.trackPan

                                    onCurrentPanChanged: requestPaint()

                                    onPaint: {
                                        const ctx = getContext("2d")
                                        ctx.reset()
                                        const cx = width / 2
                                        const cy = height / 2
                                        const r = 12

                                        // Total sweep: 135 deg to 405 deg (270 deg span)
                                        // 0 pan is at 270 deg (top center / 12 o'clock)
                                        const startAngle = 135 * Math.PI / 180
                                        const endAngle = 405 * Math.PI / 180
                                        const centerAngle = 270 * Math.PI / 180
                                        const targetAngle = centerAngle + currentPan * (135 * Math.PI / 180)

                                        // Background arc (slate grey)
                                        ctx.beginPath()
                                        ctx.arc(cx, cy, r, startAngle, endAngle, false)
                                        ctx.lineWidth = 3
                                        ctx.strokeStyle = Theme.darkMode ? "#475569" : "#cbd5e1"
                                        ctx.lineCap = "round"
                                        ctx.stroke()

                                        // Active arc (cyan / blue)
                                        if (Math.abs(currentPan) > 0.005) {
                                            ctx.beginPath()
                                            if (currentPan < 0) {
                                                ctx.arc(cx, cy, r, targetAngle, centerAngle, false)
                                            } else {
                                                ctx.arc(cx, cy, r, centerAngle, targetAngle, false)
                                            }
                                            ctx.lineWidth = 3
                                            ctx.strokeStyle = Theme.darkMode ? "#38bdf8" : "#0284c7"
                                            ctx.lineCap = "round"
                                            ctx.stroke()
                                        }

                                        // Handle circle at targetAngle
                                        const hx = cx + r * Math.cos(targetAngle)
                                        const hy = cy + r * Math.sin(targetAngle)
                                        ctx.beginPath()
                                        ctx.arc(hx, hy, 4, 0, 2 * Math.PI, false)
                                        ctx.fillStyle = Theme.darkMode ? "#0f172a" : "#ffffff"
                                        ctx.fill()
                                        ctx.lineWidth = 1.5
                                        ctx.strokeStyle = Theme.darkMode ? "#94a3b8" : "#64748b"
                                        ctx.stroke()
                                    }
                                }

                                MouseArea {
                                    id: panMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.SizeVerCursor
                                    preventStealing: true

                                    property real startY: 0
                                    property real startPan: 0

                                    onPressed: (mouse) => {
                                        startY = mouse.y
                                        startPan = trackStrip.trackPan
                                    }
                                    onPositionChanged: (mouse) => {
                                        if (pressed) {
                                            const dy = startY - mouse.y
                                            var newPan = startPan + (dy / 50.0)
                                            if (Math.abs(newPan) < 0.04) newPan = 0.0
                                            newPan = Math.max(-1.0, Math.min(1.0, newPan))
                                            EditorState.previewTrackPan(trackStrip.trackIndex, newPan)
                                        }
                                    }
                                    onReleased: {
                                        EditorState.setTrackPan(trackStrip.trackIndex, trackStrip.trackPan)
                                    }
                                    onDoubleClicked: {
                                        EditorState.setTrackPan(trackStrip.trackIndex, 0.0)
                                    }
                                    onWheel: (wheel) => {
                                        const delta = wheel.angleDelta.y > 0 ? 0.05 : -0.05
                                        var p = Math.max(-1.0, Math.min(1.0, trackStrip.trackPan + delta))
                                        if (Math.abs(p) < 0.03) p = 0.0
                                        EditorState.setTrackPan(trackStrip.trackIndex, p)
                                    }
                                }

                                ThemedToolTip {
                                    visible: panMouse.containsMouse
                                    text: qsTr("Pan: %1 (double-click to center)").arg(root.formatPan(trackStrip.trackPan))
                                }
                            }

                            // --- 4. Pan Numeric Readout (Spinbox styling) ----
                            Item {
                                width: 38
                                height: 16
                                anchors.horizontalCenter: parent.horizontalCenter

                                Rectangle {
                                    anchors.fill: parent
                                    radius: 2
                                    color: panReadoutMouse.containsMouse
                                           ? root.hoverBg
                                           : (Theme.darkMode ? Qt.rgba(0, 0, 0, 0.35) : Qt.rgba(0, 0, 0, 0.05))
                                    border.color: Theme.panelBorder
                                    border.width: 1
                                }

                                Text {
                                    anchors.left: parent.left
                                    anchors.leftMargin: 3
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: root.formatPan(trackStrip.trackPan)
                                    font.family: Theme.monoFontFamily
                                    font.pixelSize: 9
                                    color: Math.abs(trackStrip.trackPan) > 0.01
                                           ? (Theme.darkMode ? "#38bdf8" : "#0284c7")
                                           : Theme.mutedForeground
                                }

                                // Up/down tiny arrows
                                Column {
                                    anchors.right: parent.right
                                    anchors.rightMargin: 2
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 1

                                    IconGlyph {
                                        glyph: Theme.icons.chevronUp
                                        iconSize: 7
                                        iconColor: Theme.mutedForeground
                                    }
                                    IconGlyph {
                                        glyph: Theme.icons.chevronDown
                                        iconSize: 7
                                        iconColor: Theme.mutedForeground
                                    }
                                }

                                MouseArea {
                                    id: panReadoutMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onDoubleClicked: EditorState.setTrackPan(trackStrip.trackIndex, 0.0)
                                    onWheel: (wheel) => {
                                        const delta = wheel.angleDelta.y > 0 ? 0.05 : -0.05
                                        var p = Math.max(-1.0, Math.min(1.0, trackStrip.trackPan + delta))
                                        if (Math.abs(p) < 0.03) p = 0.0
                                        EditorState.setTrackPan(trackStrip.trackIndex, p)
                                    }
                                }
                            }

                            // --- 5. Meter & Fader Main Section ----------------
                            Item {
                                id: meterFaderArea
                                width: parent.width
                                height: Math.max(120, trackStrip.height - 105)

                                Row {
                                    anchors.centerIn: parent
                                    spacing: 4
                                    height: parent.height

                                    // --- Dual Stereo VU Peak Meter ---------------
                                    Item {
                                        id: dualMeter
                                        width: 22
                                        height: parent.height

                                        // Meter trough background
                                        Rectangle {
                                            anchors.fill: parent
                                            radius: 2
                                            color: Theme.darkMode ? "#0f1115" : "#1a1d24"
                                            border.color: Theme.panelBorder
                                            border.width: 1
                                        }

                                        // Left channel bar
                                        Rectangle {
                                            id: meterBarL
                                            anchors.left: parent.left
                                            anchors.leftMargin: 2
                                            anchors.bottom: parent.bottom
                                            anchors.bottomMargin: 1
                                            width: 7
                                            height: Math.max(0, Math.min(parent.height - 2,
                                                root.dbToRatio(root.linearToDb(trackStrip.curLevels.l)) * (parent.height - 2)))
                                            radius: 1
                                            gradient: Gradient {
                                                GradientStop { position: 0.0; color: "#ef4444" }
                                                GradientStop { position: 0.15; color: "#eab308" }
                                                GradientStop { position: 0.35; color: "#22c55e" }
                                                GradientStop { position: 1.0; color: "#15803d" }
                                            }
                                        }

                                        // Right channel bar
                                        Rectangle {
                                            id: meterBarR
                                            anchors.right: parent.right
                                            anchors.rightMargin: 2
                                            anchors.bottom: parent.bottom
                                            anchors.bottomMargin: 1
                                            width: 7
                                            height: Math.max(0, Math.min(parent.height - 2,
                                                root.dbToRatio(root.linearToDb(trackStrip.curLevels.r)) * (parent.height - 2)))
                                            radius: 1
                                            gradient: Gradient {
                                                GradientStop { position: 0.0; color: "#ef4444" }
                                                GradientStop { position: 0.15; color: "#eab308" }
                                                GradientStop { position: 0.35; color: "#22c55e" }
                                                GradientStop { position: 1.0; color: "#15803d" }
                                            }
                                        }

                                        // Peak hold white line L
                                        Rectangle {
                                            anchors.left: parent.left
                                            anchors.leftMargin: 2
                                            width: 7
                                            height: 1
                                            y: Math.max(1, (dualMeter.height - 2) * (1.0 - root.dbToRatio(root.linearToDb(trackStrip.curPeaks.l))))
                                            color: "#ffffff"
                                            visible: trackStrip.curPeaks.l > 0.001
                                        }

                                        // Peak hold white line R
                                        Rectangle {
                                            anchors.right: parent.right
                                            anchors.rightMargin: 2
                                            width: 7
                                            height: 1
                                            y: Math.max(1, (dualMeter.height - 2) * (1.0 - root.dbToRatio(root.linearToDb(trackStrip.curPeaks.r))))
                                            color: "#ffffff"
                                            visible: trackStrip.curPeaks.r > 0.001
                                        }

                                        // dB Scale markings overlay
                                        Repeater {
                                            model: [
                                                { db: 0,   label: "0" },
                                                { db: -2,  label: "-2" },
                                                { db: -5,  label: "-5" },
                                                { db: -10, label: "-10" },
                                                { db: -15, label: "-15" },
                                                { db: -20, label: "-20" },
                                                { db: -30, label: "-30" },
                                                { db: -45, label: "-45" }
                                            ]

                                            delegate: Item {
                                                id: tickItem
                                                required property var modelData
                                                width: dualMeter.width
                                                height: 1
                                                y: (dualMeter.height - 2) * (1.0 - root.dbToRatio(modelData.db))

                                                // Divider tick line across bars
                                                Rectangle {
                                                    anchors.fill: parent
                                                    color: Qt.rgba(0, 0, 0, 0.4)
                                                    height: 1
                                                }

                                                // Label
                                                Text {
                                                    visible: dualMeter.height >= 140 || modelData.db === 0 || modelData.db === -10 || modelData.db === -20
                                                    anchors.centerIn: parent
                                                    text: modelData.label
                                                    font.pixelSize: 6
                                                    font.family: Theme.monoFontFamily
                                                    color: Qt.rgba(1, 1, 1, 0.45)
                                                }
                                            }
                                        }
                                    }

                                    // --- Vertical Fader Slider -------------------
                                    Item {
                                        id: faderItem
                                        width: 18
                                        height: parent.height

                                        readonly property real curDb: trackStrip.isRecordingHere
                                            ? root.linearToDb(EditorState.audioRecordGain)
                                            : root.linearToDb(trackStrip.trackVol)
                                        readonly property real thumbY: (parent.height - 12) * (1.0 - root.dbToRatio(curDb))

                                        // Vertical track line
                                        Rectangle {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            anchors.top: parent.top
                                            anchors.topMargin: 6
                                            anchors.bottom: parent.bottom
                                            anchors.bottomMargin: 6
                                            width: 2
                                            color: Theme.sliderTrack

                                            // Fill below thumb (Red when recording, Blue when normal)
                                            Rectangle {
                                                anchors.left: parent.left
                                                anchors.right: parent.right
                                                anchors.bottom: parent.bottom
                                                anchors.top: parent.top
                                                anchors.topMargin: faderItem.thumbY
                                                color: trackStrip.isRecordingHere ? Theme.destructive : Theme.primary
                                            }
                                        }

                                        // Fader Thumb Handle
                                        Rectangle {
                                            id: faderThumb
                                            width: 14
                                            height: 10
                                            radius: 3
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            y: Math.max(0, Math.min(faderItem.height - height, faderItem.thumbY))
                                            color: faderMouse.pressed
                                                   ? (trackStrip.isRecordingHere ? Theme.destructive : Theme.primary)
                                                   : (faderMouse.containsMouse
                                                      ? (Theme.darkMode ? "#1e293b" : "#f1f5f9")
                                                      : (Theme.darkMode ? "#0f172a" : "#ffffff"))
                                            border.color: trackStrip.isRecordingHere
                                                          ? Theme.destructive
                                                          : (faderMouse.pressed
                                                             ? Theme.primaryForeground
                                                             : (Theme.darkMode ? "#64748b" : "#94a3b8"))
                                            border.width: 1.5

                                            // Grip groove inside thumb
                                            Rectangle {
                                                anchors.centerIn: parent
                                                width: 6
                                                height: 1
                                                color: faderMouse.pressed
                                                       ? Theme.primaryForeground
                                                       : (trackStrip.isRecordingHere ? Theme.destructive : (Theme.darkMode ? "#94a3b8" : "#64748b"))
                                            }
                                        }

                                        MouseArea {
                                            id: faderMouse
                                            anchors.fill: parent
                                            anchors.margins: -4
                                            hoverEnabled: true
                                            cursorShape: Qt.SizeVerCursor
                                            preventStealing: true

                                            property real startY: 0
                                            property real startDb: 0

                                            onPressed: (mouse) => {
                                                startY = mouse.y
                                                startDb = faderItem.curDb
                                            }
                                            onPositionChanged: (mouse) => {
                                                if (pressed) {
                                                    const availableH = faderItem.height - 12
                                                    if (availableH > 0) {
                                                        const yRatio = Math.max(0.0, Math.min(1.0, 1.0 - (mouse.y - 6) / availableH))
                                                        var newDb = root.ratioToDb(yRatio)
                                                        if (Math.abs(newDb) < 0.25) newDb = 0.0
                                                        if (trackStrip.isRecordingHere) {
                                                            EditorState.setAudioRecordGain(root.dbToLinear(newDb))
                                                        } else {
                                                            EditorState.previewTrackVolume(trackStrip.trackIndex, root.dbToLinear(newDb))
                                                        }
                                                    }
                                                }
                                            }
                                            onReleased: {
                                                if (!trackStrip.isRecordingHere) {
                                                    EditorState.setTrackVolume(trackStrip.trackIndex, trackStrip.trackVol)
                                                }
                                            }
                                            onDoubleClicked: {
                                                if (trackStrip.isRecordingHere) {
                                                    EditorState.setAudioRecordGain(1.0)
                                                } else {
                                                    EditorState.setTrackVolume(trackStrip.trackIndex, 1.0)
                                                }
                                            }
                                            onWheel: (wheel) => {
                                                const step = wheel.angleDelta.y > 0 ? 0.5 : -0.5
                                                if (trackStrip.isRecordingHere) {
                                                    var d = root.linearToDb(EditorState.audioRecordGain) + step
                                                    if (Math.abs(d) < 0.25) d = 0.0
                                                    EditorState.setAudioRecordGain(root.dbToLinear(d))
                                                } else {
                                                    var d = root.linearToDb(trackStrip.trackVol) + step
                                                    if (Math.abs(d) < 0.25) d = 0.0
                                                    EditorState.setTrackVolume(trackStrip.trackIndex, root.dbToLinear(d))
                                                }
                                            }
                                        }

                                        ThemedToolTip {
                                            visible: faderMouse.containsMouse
                                            text: trackStrip.isRecordingHere
                                                ? qsTr("Mic Recording Gain: %1 (%2%) — scroll to adjust, double-click to reset 0dB")
                                                    .arg(root.formatDb(faderItem.curDb))
                                                    .arg(Math.round(EditorState.audioRecordGain * 100))
                                                : qsTr("Volume: %1 (double-click to reset 0dB)").arg(root.formatDb(faderItem.curDb))
                                        }
                                    }
                                }
                            }

                            // --- 6. Bottom Numeric dB Readout ----------------
                            Item {
                                width: 56
                                height: 18
                                anchors.horizontalCenter: parent.horizontalCenter

                                Rectangle {
                                    anchors.fill: parent
                                    radius: 2
                                    color: dbReadoutMouse.containsMouse
                                           ? root.hoverBg
                                           : (Theme.darkMode ? Qt.rgba(0, 0, 0, 0.4) : Qt.rgba(0, 0, 0, 0.05))
                                    border.color: Theme.panelBorder
                                    border.width: 1
                                }

                                Text {
                                    readonly property real db: trackStrip.isRecordingHere
                                        ? root.linearToDb(EditorState.audioRecordGain)
                                        : root.linearToDb(trackStrip.trackVol)
                                    anchors.left: parent.left
                                    anchors.leftMargin: 3
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: root.formatDb(db)
                                    font.family: Theme.monoFontFamily
                                    font.pixelSize: 9
                                    font.bold: true
                                    color: trackStrip.isRecordingHere
                                           ? Theme.destructive
                                           : (db > 0.05 ? Theme.destructive : Theme.panelForeground)
                                }

                                Column {
                                    anchors.right: parent.right
                                    anchors.rightMargin: 2
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 1

                                    IconGlyph {
                                        glyph: Theme.icons.chevronUp
                                        iconSize: 7
                                        iconColor: Theme.mutedForeground
                                    }
                                    IconGlyph {
                                        glyph: Theme.icons.chevronDown
                                        iconSize: 7
                                        iconColor: Theme.mutedForeground
                                    }
                                }

                                MouseArea {
                                    id: dbReadoutMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onDoubleClicked: {
                                        if (trackStrip.isRecordingHere) {
                                            EditorState.setAudioRecordGain(1.0)
                                        } else {
                                            EditorState.setTrackVolume(trackStrip.trackIndex, 1.0)
                                        }
                                    }
                                    onWheel: (wheel) => {
                                        const step = wheel.angleDelta.y > 0 ? 0.5 : -0.5
                                        if (trackStrip.isRecordingHere) {
                                            var d = root.linearToDb(EditorState.audioRecordGain) + step
                                            if (Math.abs(d) < 0.25) d = 0.0
                                            EditorState.setAudioRecordGain(root.dbToLinear(d))
                                        } else {
                                            var d = root.linearToDb(trackStrip.trackVol) + step
                                            if (Math.abs(d) < 0.25) d = 0.0
                                            EditorState.setTrackVolume(trackStrip.trackIndex, root.dbToLinear(d))
                                        }
                                    }
                                }

                                ThemedToolTip {
                                    visible: dbReadoutMouse.containsMouse
                                    text: trackStrip.isRecordingHere
                                        ? qsTr("Mic recording level in dB — scroll to adjust, double-click to reset to 0dB (100%)")
                                        : qsTr("Track level in dB — scroll to adjust, double-click to reset to 0dB")
                                }
                            }
                        }
                    }
                }

                // === Master Channel Strip ====================================
                Item {
                    id: masterStrip
                    width: 66
                    height: parent.height

                    // Background with slight accent tint
                    Rectangle {
                        anchors.fill: parent
                        color: Qt.rgba(Theme.panelForeground.r, Theme.panelForeground.g, Theme.panelForeground.b, 0.03)
                    }

                    Column {
                        anchors.fill: parent
                        anchors.margins: 2
                        spacing: 2

                        // --- 1. Master Header --------------------------------
                        Rectangle {
                            width: parent.width
                            height: 22
                            radius: 2
                            color: root.headerBg
                            border.color: Theme.panelBorder
                            border.width: 1

                            Text {
                                anchors.centerIn: parent
                                text: qsTr("Master")
                                font.bold: true
                                font.pixelSize: 11
                                color: Theme.primary
                            }
                        }

                        // --- 2. Master Buttons -------------------------------
                        Row {
                            anchors.horizontalCenter: parent.horizontalCenter
                            spacing: 4
                            height: 22

                            // Master Mute Button
                            Rectangle {
                                width: 22
                                height: 18
                                radius: 2
                                anchors.verticalCenter: parent.verticalCenter
                                color: EditorState.masterMuted
                                       ? Qt.rgba(Theme.destructive.r, Theme.destructive.g, Theme.destructive.b, 0.25)
                                       : (masterMuteMouse.containsMouse ? root.hoverBg : "transparent")
                                border.color: EditorState.masterMuted ? Theme.destructive : "transparent"
                                border.width: 1

                                IconGlyph {
                                    anchors.centerIn: parent
                                    glyph: EditorState.masterMuted ? Theme.icons.volumeOff : Theme.icons.volumeHigh
                                    iconSize: 12
                                    iconColor: EditorState.masterMuted ? Theme.destructive : Theme.mutedForeground
                                }

                                ThemedToolTip {
                                    visible: masterMuteMouse.containsMouse
                                    text: EditorState.masterMuted ? qsTr("Unmute master") : qsTr("Mute master")
                                }
                                MouseArea {
                                    id: masterMuteMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: EditorState.masterMuted = !EditorState.masterMuted
                                }
                            }
                        }

                        // --- 3. Master Spacer to align meters -----------------
                        Item {
                            width: 36
                            height: 36
                            anchors.horizontalCenter: parent.horizontalCenter

                            IconGlyph {
                                anchors.centerIn: parent
                                glyph: Theme.icons.audioLines
                                iconSize: 18
                                iconColor: Theme.mutedForeground
                            }
                        }

                        // --- 4. Master Label Space ----------------------------
                        Item {
                            width: 38
                            height: 16
                            anchors.horizontalCenter: parent.horizontalCenter

                            Text {
                                anchors.centerIn: parent
                                text: qsTr("Main")
                                font.pixelSize: 9
                                color: Theme.mutedForeground
                            }
                        }

                        // --- 5. Master Meter & Fader Section ------------------
                        Item {
                            id: masterMeterFaderArea
                            width: parent.width
                            height: Math.max(120, masterStrip.height - 105)

                            Row {
                                anchors.centerIn: parent
                                spacing: 4
                                height: parent.height

                                // Master Dual Meter
                                Item {
                                    id: masterDualMeter
                                    width: 22
                                    height: parent.height

                                    Rectangle {
                                        anchors.fill: parent
                                        radius: 2
                                        color: Theme.darkMode ? "#0f1115" : "#1a1d24"
                                        border.color: Theme.panelBorder
                                        border.width: 1
                                    }

                                    // L
                                    Rectangle {
                                        anchors.left: parent.left
                                        anchors.leftMargin: 2
                                        anchors.bottom: parent.bottom
                                        anchors.bottomMargin: 1
                                        width: 7
                                        height: Math.max(0, Math.min(parent.height - 2,
                                            root.dbToRatio(root.linearToDb(root.masterLevelL)) * (parent.height - 2)))
                                        radius: 1
                                        gradient: Gradient {
                                            GradientStop { position: 0.0; color: "#ef4444" }
                                            GradientStop { position: 0.15; color: "#eab308" }
                                            GradientStop { position: 0.35; color: "#22c55e" }
                                            GradientStop { position: 1.0; color: "#15803d" }
                                        }
                                    }

                                    // R
                                    Rectangle {
                                        anchors.right: parent.right
                                        anchors.rightMargin: 2
                                        anchors.bottom: parent.bottom
                                        anchors.bottomMargin: 1
                                        width: 7
                                        height: Math.max(0, Math.min(parent.height - 2,
                                            root.dbToRatio(root.linearToDb(root.masterLevelR)) * (parent.height - 2)))
                                        radius: 1
                                        gradient: Gradient {
                                            GradientStop { position: 0.0; color: "#ef4444" }
                                            GradientStop { position: 0.15; color: "#eab308" }
                                            GradientStop { position: 0.35; color: "#22c55e" }
                                            GradientStop { position: 1.0; color: "#15803d" }
                                        }
                                    }

                                    // Peak hold L
                                    Rectangle {
                                        anchors.left: parent.left
                                        anchors.leftMargin: 2
                                        width: 7
                                        height: 1
                                        y: Math.max(1, (masterDualMeter.height - 2) * (1.0 - root.dbToRatio(root.linearToDb(root.masterPeakL))))
                                        color: "#ffffff"
                                        visible: root.masterPeakL > 0.001
                                    }

                                    // Peak hold R
                                    Rectangle {
                                        anchors.right: parent.right
                                        anchors.rightMargin: 2
                                        width: 7
                                        height: 1
                                        y: Math.max(1, (masterDualMeter.height - 2) * (1.0 - root.dbToRatio(root.linearToDb(root.masterPeakR))))
                                        color: "#ffffff"
                                        visible: root.masterPeakR > 0.001
                                    }

                                    // dB markings
                                    Repeater {
                                        model: [
                                            { db: 0,   label: "0" },
                                            { db: -2,  label: "-2" },
                                            { db: -5,  label: "-5" },
                                            { db: -10, label: "-10" },
                                            { db: -15, label: "-15" },
                                            { db: -20, label: "-20" },
                                            { db: -30, label: "-30" },
                                            { db: -45, label: "-45" }
                                        ]

                                        delegate: Item {
                                            required property var modelData
                                            width: masterDualMeter.width
                                            height: 1
                                            y: (masterDualMeter.height - 2) * (1.0 - root.dbToRatio(modelData.db))

                                            Rectangle {
                                                anchors.fill: parent
                                                color: Qt.rgba(0, 0, 0, 0.4)
                                                height: 1
                                            }

                                            Text {
                                                visible: masterDualMeter.height >= 140 || modelData.db === 0 || modelData.db === -10 || modelData.db === -20
                                                anchors.centerIn: parent
                                                text: modelData.label
                                                font.pixelSize: 6
                                                font.family: Theme.monoFontFamily
                                                color: Qt.rgba(1, 1, 1, 0.45)
                                            }
                                        }
                                    }
                                }

                                // Master Fader Slider
                                Item {
                                    id: masterFaderItem
                                    width: 18
                                    height: parent.height

                                    readonly property real curDb: root.linearToDb(EditorState.masterVolume)
                                    readonly property real thumbY: (parent.height - 12) * (1.0 - root.dbToRatio(curDb))

                                    Rectangle {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        anchors.top: parent.top
                                        anchors.topMargin: 6
                                        anchors.bottom: parent.bottom
                                        anchors.bottomMargin: 6
                                        width: 2
                                        color: Theme.sliderTrack

                                        Rectangle {
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.bottom: parent.bottom
                                            anchors.top: parent.top
                                            anchors.topMargin: masterFaderItem.thumbY
                                            color: Theme.primary
                                        }
                                    }

                                    Rectangle {
                                        id: masterFaderThumb
                                        width: 14
                                        height: 10
                                        radius: 3
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        y: Math.max(0, Math.min(masterFaderItem.height - height, masterFaderItem.thumbY))
                                        color: masterFaderMouse.pressed
                                               ? Theme.primary
                                               : (masterFaderMouse.containsMouse
                                                  ? (Theme.darkMode ? "#1e293b" : "#f1f5f9")
                                                  : (Theme.darkMode ? "#0f172a" : "#ffffff"))
                                        border.color: masterFaderMouse.pressed
                                                      ? Theme.primaryForeground
                                                      : (Theme.darkMode ? "#64748b" : "#94a3b8")
                                        border.width: 1.5

                                        Rectangle {
                                            anchors.centerIn: parent
                                            width: 6
                                            height: 1
                                            color: masterFaderMouse.pressed
                                                   ? Theme.primaryForeground
                                                   : (Theme.darkMode ? "#94a3b8" : "#64748b")
                                        }
                                    }

                                    MouseArea {
                                        id: masterFaderMouse
                                        anchors.fill: parent
                                        anchors.margins: -4
                                        hoverEnabled: true
                                        cursorShape: Qt.SizeVerCursor
                                        preventStealing: true

                                        onPositionChanged: (mouse) => {
                                            if (pressed) {
                                                const availableH = masterFaderItem.height - 12
                                                if (availableH > 0) {
                                                    const yRatio = Math.max(0.0, Math.min(1.0, 1.0 - (mouse.y - 6) / availableH))
                                                    var newDb = root.ratioToDb(yRatio)
                                                    if (Math.abs(newDb) < 0.25) newDb = 0.0
                                                    EditorState.masterVolume = root.dbToLinear(newDb)
                                                }
                                            }
                                        }
                                        onDoubleClicked: EditorState.masterVolume = 1.0
                                        onWheel: (wheel) => {
                                            const step = wheel.angleDelta.y > 0 ? 0.5 : -0.5
                                            var d = root.linearToDb(EditorState.masterVolume) + step
                                            if (Math.abs(d) < 0.25) d = 0.0
                                            EditorState.masterVolume = root.dbToLinear(d)
                                        }
                                    }

                                    ThemedToolTip {
                                        visible: masterFaderMouse.containsMouse
                                        text: qsTr("Master Volume: %1 (double-click to reset 0dB)").arg(root.formatDb(root.linearToDb(EditorState.masterVolume)))
                                    }
                                }
                            }
                        }

                        // --- 6. Master Bottom dB Readout ---------------------
                        Item {
                            width: 58
                            height: 18
                            anchors.horizontalCenter: parent.horizontalCenter

                            Rectangle {
                                anchors.fill: parent
                                radius: 2
                                color: masterDbReadoutMouse.containsMouse
                                       ? root.hoverBg
                                       : (Theme.darkMode ? Qt.rgba(0, 0, 0, 0.4) : Qt.rgba(0, 0, 0, 0.05))
                                border.color: Theme.panelBorder
                                border.width: 1
                            }

                            Text {
                                readonly property real db: root.linearToDb(EditorState.masterVolume)
                                anchors.left: parent.left
                                anchors.leftMargin: 3
                                anchors.verticalCenter: parent.verticalCenter
                                text: root.formatDb(db)
                                font.family: Theme.monoFontFamily
                                font.pixelSize: 9
                                font.bold: true
                                color: db > 0.05 ? Theme.destructive : Theme.panelForeground
                            }

                            Column {
                                anchors.right: parent.right
                                anchors.rightMargin: 2
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 1

                                IconGlyph {
                                    glyph: Theme.icons.chevronUp
                                    iconSize: 7
                                    iconColor: Theme.mutedForeground
                                }
                                IconGlyph {
                                    glyph: Theme.icons.chevronDown
                                    iconSize: 7
                                    iconColor: Theme.mutedForeground
                                }
                            }

                            MouseArea {
                                id: masterDbReadoutMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onDoubleClicked: EditorState.masterVolume = 1.0
                                onWheel: (wheel) => {
                                    const step = wheel.angleDelta.y > 0 ? 0.5 : -0.5
                                    var d = root.linearToDb(EditorState.masterVolume) + step
                                    if (Math.abs(d) < 0.25) d = 0.0
                                    EditorState.masterVolume = root.dbToLinear(d)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
