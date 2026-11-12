import QtQuick
import QtQuick.Controls.Basic
import Drift
// Audio mixer docked on the right side at the end of the timeline: one channel strip per
// audio track (name, mute/solo/record, pan, stereo meter, fader, level readout) and a
// Master strip pinned to the left edge; the track strips after it scroll horizontally.
// The left edge drags to resize; double-clicking it goes back to fitting the strips.
Rectangle {
    id: root

    readonly property bool isRecording: EditorState.isRecordingAudio
    readonly property bool mixerVisible: EditorState.preferences.audioMixerVisible || isRecording

    readonly property real baseStripWidth: 96
    readonly property real minMixerWidth: isRecording ? 250 : 160
    // Keep enough of the timeline visible that the mixer can never swallow it.
    readonly property real maxMixerWidth: parent
        ? Math.max(minMixerWidth, parent.width - EditorState.preferences.trackLabelsWidth - 240)
        : 2000
    readonly property int columnCount: Math.max(1, mixerLoader.item ? mixerLoader.item.audioTracksList.length : 0) + 1
    readonly property real fitWidth: columnCount * baseStripWidth + 1
    readonly property real preferredWidth: EditorState.preferences.audioMixerWidth > 0 ? EditorState.preferences.audioMixerWidth : fitWidth
    readonly property real stripWidth: baseStripWidth

    width: mixerVisible ? Math.max(minMixerWidth, Math.min(maxMixerWidth, preferredWidth)) : 0
    visible: width > 0
    clip: true
    color: Theme.c.audioMixerStrip.background

    property bool resizing: false
    // Hosted in a panel: the panel supplies the title and owns the size, so the header and
    // the edge resize (which writes the timeline's audioMixerWidth preference) are dropped.
    property bool embedded: false

    Behavior on width {
        enabled: !root.resizing
        NumberAnimation { duration: Theme.durationSlow; easing.type: Theme.easing }
    }

    // --- Piecewise dB <-> Y-Ratio Conversions ---------------------------------
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

    // Unity is a detent: faders and the wheel settle on exactly 0 dB when they pass near it.
    function snapDb(db) {
        return Math.abs(db) < 0.25 ? 0.0 : db
    }

    function formatDb(db) {
        if (db <= -59.5) return "-∞"
        if (Math.abs(db) < 0.05) return "0.0"
        return (db > 0 ? "+" : "") + db.toFixed(1)
    }

    function formatPan(pan) {
        const val = Math.round(pan * 100)
        if (val === 0) return qsTr("C")
        return val < 0 ? qsTr("L%1").arg(-val) : qsTr("R%1").arg(val)
    }

    Loader {
        id: mixerLoader
        anchors.fill: parent
        active: root.mixerVisible || root.width > 0
        sourceComponent: Item {
            id: content

            // One strip per track that feeds the mix: audio tracks, and video tracks whose clips carry
            // their own sound. Unnamed tracks get the "V1"/"A2" label the compact track header uses.
            readonly property var audioTracksList: {
                const result = []
                const allTracks = EditorState.tracks
                var videoCount = 0
                var audioCount = 0
                for (var i = 0; i < allTracks.length; i++) {
                    const t = allTracks[i]
                    if (t.type === "video")
                        videoCount++
                    else if (t.type === "audio")
                        audioCount++
                    else
                        continue
                    if (!t.hasAudio)
                        continue
                    const isVideo = t.type === "video"
                    result.push({
                        trackIndex: i,
                        isVideo: isVideo,
                        name: t.name && t.name.length > 0 ? t.name
                              : (isVideo ? "V" : "A") + (t.number > 0 ? t.number : (isVideo ? videoCount : audioCount)),
                        muted: t.muted === true,
                        solo: t.solo === true,
                        volume: t.volume !== undefined ? t.volume : 1.0,
                        pan: t.pan !== undefined ? t.pan : 0.0
                    })
                }
                return result
            }

            // --- Metering Animation Engine -------------------------------------------
            // One C++ call per tick for every meter, written into each strip's own scalar
            // properties, so a tick only re-evaluates the meters that actually moved.
            readonly property real meterDecay: 0.04
            readonly property real meterPeakDecay: 0.02
            readonly property int meterPeakHoldMs: 1200

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
                    const tracks = content.audioTracksList
                    const indexes = []
                    for (let i = 0; i < tracks.length; i++)
                        indexes.push(tracks[i].trackIndex)
                    const levels = EditorState.meterLevels(indexes)

                    for (let i = 0; i < tracks.length; i++) {
                        const strip = stripRepeater.itemAt(i)
                        if (strip)
                            strip.applyMeter(levels[2 + i * 2], levels[3 + i * 2], now)
                    }

                    const mTargetL = levels[0]
                    const mTargetR = levels[1]
                    const decay = content.meterDecay
                    const peakDecay = content.meterPeakDecay
                    content.masterLevelL = mTargetL >= content.masterLevelL ? mTargetL : Math.max(0.0, content.masterLevelL - decay)
                    content.masterLevelR = mTargetR >= content.masterLevelR ? mTargetR : Math.max(0.0, content.masterLevelR - decay)

                    if (mTargetL >= content.masterPeakL) {
                        content.masterPeakL = mTargetL
                        content.masterPeakHoldTimeL = now
                    } else if (now - content.masterPeakHoldTimeL > content.meterPeakHoldMs) {
                        content.masterPeakL = Math.max(0.0, content.masterPeakL - peakDecay)
                    }

                    if (mTargetR >= content.masterPeakR) {
                        content.masterPeakR = mTargetR
                        content.masterPeakHoldTimeR = now
                    } else if (now - content.masterPeakHoldTimeR > content.meterPeakHoldMs) {
                        content.masterPeakR = Math.max(0.0, content.masterPeakR - peakDecay)
                    }
                }
            }

            function resetMeters() {
                if (EditorState.playing || EditorState.isRecordingAudio)
                    return
                for (let i = 0; i < stripRepeater.count; i++) {
                    const strip = stripRepeater.itemAt(i)
                    if (strip)
                        strip.clearMeter()
                }
                masterLevelL = 0.0
                masterLevelR = 0.0
                masterPeakL = 0.0
                masterPeakR = 0.0
            }

            Connections {
                target: EditorState
                function onPlayingChanged() { content.resetMeters() }
                function onAudioRecordingStateChanged() { content.resetMeters() }
            }

            // === Layout ==============================================================

            Column {
                anchors.fill: parent
                anchors.leftMargin: 1

                // --- Header ----------------------------------------------------------------
                Item {
                    id: topHeader
                    visible: !root.embedded
                    width: parent.width
                    height: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight

                    Row {
                        anchors.left: parent.left
                        anchors.leftMargin: Theme.spacingLg + 2
                        anchors.right: headerCloseBtn.left
                        anchors.rightMargin: Theme.spacingSm
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.spacingMd
                        clip: true

                        IconGlyph {
                            glyph: Theme.icons.slidersVertical
                            iconSize: Theme.iconSizeMd
                            iconColor: Theme.c.audioMixerStrip.icon
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        Text {
                            text: qsTr("Audio Mixer")
                            color: Theme.c.audioMixerStrip.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeSm
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                            width: Math.max(0, parent.width - Theme.iconSizeMd - parent.spacing)
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    IconButton {
                        id: headerCloseBtn
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.spacingSm
                        anchors.verticalCenter: parent.verticalCenter
                        buttonSize: 24
                        iconSize: Theme.iconSizeMd
                        glyph: Theme.icons.x
                        variant: "text"
                        tooltip: qsTr("Close audio mixer")
                        onClicked: EditorState.preferences.audioMixerVisible = false
                    }

                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 1
                        color: Theme.c.audioMixerStrip.border
                    }
                }

                // --- Voiceover recording controls (only while recording) ---------------------
                Rectangle {
                    id: voPanel
                    visible: EditorState.isRecordingAudio
                    width: parent.width
                    height: visible ? 60 : 0
                    clip: true
                    color: EditorState.isAudioRecordingPaused
                           ? Theme.c.audioMixerStrip.recordingPausedBackground
                           : Theme.c.audioMixerStrip.recordingBackground

                    Behavior on height {
                        NumberAnimation { duration: Theme.durationBase; easing.type: Theme.easing }
                    }

                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 1
                        color: Theme.c.audioMixerStrip.border
                    }

                    Column {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacingLg
                        anchors.rightMargin: Theme.spacingSm
                        anchors.topMargin: Theme.spacingSm
                        spacing: Theme.spacingXs

                        Item {
                            width: parent.width
                            height: 26

                            Row {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: Theme.spacingMd

                                Rectangle {
                                    width: 8
                                    height: 8
                                    radius: Theme.c.audioMixerStrip.recordDotRadius
                                    color: EditorState.isAudioRecordingPaused ? Theme.c.audioMixerStrip.recordPaused : Theme.c.audioMixerStrip.destructive
                                    anchors.verticalCenter: parent.verticalCenter
                                    SequentialAnimation on opacity {
                                        running: voPanel.visible && !EditorState.isAudioRecordingPaused
                                        loops: Animation.Infinite
                                        NumberAnimation { to: 0.3; duration: 450 }
                                        NumberAnimation { to: 1.0; duration: 450 }
                                    }
                                }

                                Text {
                                    text: EditorState.isAudioRecordingPaused ? qsTr("Paused") : qsTr("Recording")
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeXs
                                    font.weight: Font.DemiBold
                                    color: EditorState.isAudioRecordingPaused ? Theme.c.audioMixerStrip.recordPausedForeground : Theme.c.audioMixerStrip.destructive
                                    anchors.verticalCenter: parent.verticalCenter
                                }

                                Text {
                                    text: {
                                        const secs = EditorState.audioRecordSeconds
                                        const m = Math.floor(secs / 60)
                                        const s = Math.floor(secs % 60)
                                        const ds = Math.floor((secs % 1) * 10)
                                        return (m < 10 ? "0" + m : m) + ":" + (s < 10 ? "0" + s : s) + "." + ds
                                    }
                                    font.family: Theme.monoFontFamily
                                    font.pixelSize: Theme.fontSizeXs
                                    color: Theme.c.audioMixerStrip.foreground
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }

                            Row {
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: Theme.spacingXs

                                IconButton {
                                    buttonSize: 24
                                    iconSize: Theme.iconSizeMd
                                    glyph: EditorState.isAudioRecordingPaused ? Theme.icons.play : Theme.icons.pause
                                    tooltip: EditorState.isAudioRecordingPaused ? qsTr("Resume recording") : qsTr("Pause recording")
                                    onClicked: EditorState.toggleAudioRecordingPause()
                                }
                                IconButton {
                                    buttonSize: 24
                                    iconSize: Theme.iconSizeMd
                                    glyph: Theme.icons.check
                                    tooltip: qsTr("Done — save recording to track")
                                    onClicked: EditorState.stopAudioRecording()
                                }
                                IconButton {
                                    buttonSize: 24
                                    iconSize: Theme.iconSizeMd
                                    glyph: Theme.icons.trash
                                    tooltip: qsTr("Discard — cancel recording")
                                    onClicked: EditorState.cancelAudioRecording()
                                }
                            }
                        }

                        // Microphone picker
                        Item {
                            id: voMicPickerBtn
                            width: Math.min(parent.width, 240)
                            height: 24

                            Rectangle {
                                anchors.fill: parent
                                radius: Theme.c.audioMixerStrip.pickerRadius
                                color: voMicMouse.containsMouse || voMicMenu.opened ? Theme.c.audioMixerStrip.pickerBackgroundHover : Theme.c.audioMixerStrip.pickerBackground
                                border.color: Theme.c.audioMixerStrip.pickerBorder
                                border.width: Theme.c.audioMixerStrip.pickerBorderWidth
                            }

                            Row {
                                anchors.left: parent.left
                                anchors.leftMargin: Theme.spacingMd
                                anchors.right: parent.right
                                anchors.rightMargin: Theme.spacingMd
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: Theme.spacingSm

                                IconGlyph {
                                    glyph: Theme.icons.mic
                                    iconSize: Theme.iconSizeSm
                                    iconColor: Theme.c.audioMixerStrip.icon
                                    anchors.verticalCenter: parent.verticalCenter
                                }

                                Text {
                                    text: EditorState.currentMicrophoneName || qsTr("Default Mic")
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeXs
                                    color: Theme.c.audioMixerStrip.foreground
                                    elide: Text.ElideRight
                                    width: Math.max(10, voMicPickerBtn.width - Theme.iconSizeSm * 2 - Theme.spacingMd * 2 - Theme.spacingSm * 2)
                                    anchors.verticalCenter: parent.verticalCenter
                                }

                                IconGlyph {
                                    glyph: Theme.icons.chevronDown
                                    iconSize: Theme.iconSizeSm
                                    iconColor: Theme.c.audioMixerStrip.icon
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

                // --- Channel strips --------------------------------------------------------------
                Item {
                    width: parent.width
                    height: parent.height - topHeader.height - voPanel.height

                    Flickable {
                        id: stripsFlick
                        anchors.left: masterStrip.right
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        contentWidth: stripsRow.width
                        contentHeight: height
                        boundsBehavior: Flickable.StopAtBounds
                        flickableDirection: Flickable.HorizontalFlick
                        clip: true

                        ScrollBar.horizontal: AppScrollBar { policy: ScrollBar.AsNeeded }

                        Row {
                            id: stripsRow
                            height: stripsFlick.height

                            // Empty state when no audio tracks exist
                            Item {
                                visible: content.audioTracksList.length === 0
                                width: visible ? root.stripWidth : 0
                                height: parent.height

                                Rectangle {
                                    anchors.fill: parent
                                    anchors.margins: Theme.spacingSm
                                    radius: Theme.c.audioMixerStrip.addTrackRadius
                                    color: addTrackMouse.containsMouse ? Theme.c.audioMixerStrip.addTrackBackgroundHover : "transparent"
                                    border.width: Theme.c.audioMixerStrip.addTrackBorderWidth
                                    border.color: addTrackMouse.containsMouse ? Theme.c.audioMixerStrip.addTrackBorderHover : Theme.c.audioMixerStrip.addTrackBorder

                                    Behavior on color {
                                        ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                                    }

                                    Column {
                                        anchors.centerIn: parent
                                        width: parent.width - Theme.spacingLg
                                        spacing: Theme.spacingMd

                                        IconGlyph {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            glyph: Theme.icons.plus
                                            iconSize: Theme.iconSizeBase
                                            iconColor: addTrackMouse.containsMouse ? Theme.c.audioMixerStrip.addTrackForeground : Theme.c.audioMixerStrip.addTrackForegroundMuted
                                        }

                                        Text {
                                            width: parent.width
                                            text: qsTr("Add audio track")
                                            horizontalAlignment: Text.AlignHCenter
                                            wrapMode: Text.WordWrap
                                            font.family: Theme.fontFamily
                                            font.pixelSize: Theme.fontSizeXs
                                            color: addTrackMouse.containsMouse ? Theme.c.audioMixerStrip.addTrackForeground : Theme.c.audioMixerStrip.addTrackForegroundMuted
                                        }
                                    }

                                    MouseArea {
                                        id: addTrackMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: EditorState.addTrack("audio")
                                    }
                                }
                            }

                            // Count model: previewTrackVolume/Pan rebuild audioTracksList on every drag
                            // tick, and a list model would recreate the strip under the pressed fader.
                            Repeater {
                                id: stripRepeater
                                model: content.audioTracksList.length

                                delegate: MixerChannelStrip {
                                    id: trackStrip
                                    required property int index
                                    readonly property var modelData: content.audioTracksList[index] || ({})

                                    readonly property int trackIndex: modelData.trackIndex
                                    property real peakHoldTimeL: 0
                                    property real peakHoldTimeR: 0

                                    // Instant attack, linear release; peaks hold, then fall.
                                    function applyMeter(targetL, targetR, now) {
                                        const decay = content.meterDecay
                                        const peakDecay = content.meterPeakDecay
                                        levelL = targetL >= levelL ? targetL : Math.max(0.0, levelL - decay)
                                        levelR = targetR >= levelR ? targetR : Math.max(0.0, levelR - decay)
                                        if (targetL >= peakL) {
                                            peakL = targetL
                                            peakHoldTimeL = now
                                        } else if (now - peakHoldTimeL > content.meterPeakHoldMs) {
                                            peakL = Math.max(0.0, peakL - peakDecay)
                                        }
                                        if (targetR >= peakR) {
                                            peakR = targetR
                                            peakHoldTimeR = now
                                        } else if (now - peakHoldTimeR > content.meterPeakHoldMs) {
                                            peakR = Math.max(0.0, peakR - peakDecay)
                                        }
                                    }

                                    function clearMeter() {
                                        levelL = 0
                                        levelR = 0
                                        peakL = 0
                                        peakR = 0
                                    }

                                    width: root.stripWidth
                                    height: stripsRow.height
                                    mixer: root
                                    name: modelData.name
                                    isVideo: modelData.isVideo === true
                                    canRecord: !isVideo
                                    muted: modelData.muted
                                    solo: modelData.solo
                                    armed: EditorState.isRecordingAudio && EditorState.recordingTrackIndex === trackIndex
                                    armPaused: EditorState.isAudioRecordingPaused
                                    volume: armed ? EditorState.audioRecordGain : modelData.volume
                                    pan: modelData.pan

                                    onMuteToggled: EditorState.setTrackMuted(trackIndex, !muted)
                                    onSoloToggled: EditorState.setTrackSolo(trackIndex, !solo)
                                    onArmToggled: {
                                        if (armed)
                                            EditorState.stopAudioRecording()
                                        else
                                            EditorState.startAudioRecording(trackIndex)
                                    }
                                    onVolumePreviewed: (gain) => {
                                        if (armed)
                                            EditorState.setAudioRecordGain(gain)
                                        else
                                            EditorState.previewTrackVolume(trackIndex, gain)
                                    }
                                    onVolumeCommitted: {
                                        if (!armed)
                                            EditorState.setTrackVolume(trackIndex, modelData.volume)
                                    }
                                    onVolumeSet: (gain) => {
                                        if (armed)
                                            EditorState.setAudioRecordGain(gain)
                                        else
                                            EditorState.setTrackVolume(trackIndex, gain)
                                    }
                                    onPanPreviewed: (value) => EditorState.previewTrackPan(trackIndex, value)
                                    onPanCommitted: EditorState.setTrackPan(trackIndex, modelData.pan)
                                    onPanSet: (value) => EditorState.setTrackPan(trackIndex, value)
                                }
                            }
                        }
                    }

                    MixerChannelStrip {
                        id: masterStrip
                        anchors.left: parent.left
                        width: root.stripWidth
                        height: parent.height
                        mixer: root
                        master: true
                        name: qsTr("Master")
                        muted: EditorState.masterMuted
                        volume: EditorState.masterVolume
                        levelL: content.masterLevelL
                        levelR: content.masterLevelR
                        peakL: content.masterPeakL
                        peakR: content.masterPeakR

                        onMuteToggled: EditorState.masterMuted = !EditorState.masterMuted
                        onVolumePreviewed: (gain) => EditorState.masterVolume = gain
                        onVolumeSet: (gain) => EditorState.masterVolume = gain

                        Rectangle {
                            z: -1
                            anchors.fill: parent
                            color: Theme.c.audioMixerStrip.masterBackground
                            opacity: 0.5
                        }
                    }
                }
            }

            // --- Resize handle ---------------------------------------------------------------
            // Drag the left edge to resize; double-click returns to fitting the strips.
            MouseArea {
                id: resizeHandle
                visible: !root.embedded
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 6
                z: 50
                hoverEnabled: true
                cursorShape: Qt.SplitHCursor
                preventStealing: true

                property real pressGlobalX: 0
                property real pressWidth: 0

                onPressedChanged: root.resizing = pressed
                onPressed: (mouse) => {
                    pressGlobalX = mapToItem(null, mouse.x, 0).x
                    pressWidth = root.width
                    Haptics.pickUp()
                }
                onPositionChanged: (mouse) => {
                    if (!pressed)
                        return
                    const dx = mapToItem(null, mouse.x, 0).x - pressGlobalX
                    EditorState.preferences.audioMixerWidth = Math.max(root.minMixerWidth, Math.min(root.maxMixerWidth, pressWidth - dx))
                }
                onReleased: Haptics.drop()
                onDoubleClicked: EditorState.preferences.audioMixerWidth = 0

                // Resting edge line; turns amber while the handle is hovered or dragged.
                Rectangle {
                    anchors.left: parent.left
                    width: resizeHandle.pressed || resizeHandle.containsMouse
                           ? Theme.c.audioMixerStrip.handleBorderWidth
                           : Theme.c.audioMixerStrip.handleBorderWidthRest
                    height: parent.height
                    color: resizeHandle.pressed || resizeHandle.containsMouse
                           ? Theme.c.audioMixerStrip.handleBackground
                           : Theme.c.audioMixerStrip.handleBackgroundRest

                    Behavior on color {
                        ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                    }
                }

                ThemedToolTip {
                    visible: resizeHandle.containsMouse && !resizeHandle.pressed
                    text: qsTr("Drag to resize — double-click to fit")
                }
            }
        }
    }
}
