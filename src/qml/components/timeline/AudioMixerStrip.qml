import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

// Audio mixer docked on the right side at the end of the timeline: one channel strip per
// audio track (name, mute/solo/record, pan, stereo meter, fader, level readout) and a
// Master strip pinned to the left edge; the track strips after it scroll horizontally.
// The left edge drags to resize; double-clicking it goes back to fitting the strips.
Rectangle {
    id: root

    readonly property bool isRecording: EditorState.isRecordingAudio
    readonly property bool mixerVisible: EditorState.audioMixerVisible || isRecording

    readonly property real baseStripWidth: 96
    readonly property real minMixerWidth: isRecording ? 250 : 160
    // Keep enough of the timeline visible that the mixer can never swallow it.
    readonly property real maxMixerWidth: parent
        ? Math.max(minMixerWidth, parent.width - EditorState.trackLabelsWidth - 240)
        : 2000
    readonly property int columnCount: Math.max(1, mixerLoader.item ? mixerLoader.item.audioTracksList.length : 0) + 1
    readonly property real fitWidth: columnCount * baseStripWidth + 1
    readonly property real preferredWidth: EditorState.audioMixerWidth > 0 ? EditorState.audioMixerWidth : fitWidth
    readonly property real stripWidth: baseStripWidth

    width: mixerVisible ? Math.max(minMixerWidth, Math.min(maxMixerWidth, preferredWidth)) : 0
    visible: width > 0
    clip: true
    color: Theme.panelBackground

    property bool resizing: false

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

    // === Reusable pieces =====================================================

    // Square toggle used for mute / solo / record. `tone` picks the checked treatment:
    // "destructive" (mute, record) or "active" (solo, the app's amber active state).
    component StripToggle: AbstractButton {
        id: toggle

        property string glyph: ""
        property string tooltip: ""
        property string tone: "destructive"
        property bool blinking: false

        readonly property color _checkedFg: tone === "active" ? Theme.panelSecondaryForeground : Theme.destructive

        implicitWidth: 28
        implicitHeight: 28
        hoverEnabled: true
        focusPolicy: Qt.TabFocus
        scale: down ? Theme.pressScale : 1.0

        Behavior on scale {
            NumberAnimation { duration: Theme.durationPress; easing.type: Theme.easing }
        }

        Accessible.role: Accessible.CheckBox
        Accessible.name: tooltip
        Accessible.checked: checked

        background: Rectangle {
            radius: Theme.radiusXs
            color: toggle.checked
                   ? (toggle.tone === "active" ? Theme.panelSecondaryBg
                                               : Qt.rgba(Theme.destructive.r, Theme.destructive.g, Theme.destructive.b, 0.16))
                   : (toggle.down ? Theme.panelMuted : (toggle.hovered ? Theme.popoverHover : "transparent"))
            border.width: toggle.checked || toggle.visualFocus ? Theme.borderWidth : 0
            border.color: toggle.visualFocus ? Theme.focusRing
                        : toggle.tone === "active" ? Theme.panelSecondaryBorder
                        : Qt.rgba(Theme.destructive.r, Theme.destructive.g, Theme.destructive.b, 0.45)

            Behavior on color {
                ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
            }
        }

        contentItem: Item {
            IconGlyph {
                id: toggleGlyph
                anchors.centerIn: parent
                glyph: toggle.glyph
                iconSize: Theme.iconSizeBase
                iconColor: toggle.checked ? toggle._checkedFg
                         : (toggle.hovered ? Theme.panelForeground : Theme.mutedForeground)

                SequentialAnimation on opacity {
                    running: toggle.blinking
                    loops: Animation.Infinite
                    onStopped: toggleGlyph.opacity = 1
                    NumberAnimation { to: 0.3; duration: 450 }
                    NumberAnimation { to: 1.0; duration: 450 }
                }
            }
        }

        ThemedToolTip {
            visible: toggle.tooltip.length > 0 && (toggle.hovered || toggle.visualFocus)
            text: toggle.tooltip
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.NoButton
            cursorShape: Qt.PointingHandCursor
        }
    }

    // Zone colours: `vivid` draws the live level, the muted tints mark the zones at rest.
    component ZoneGradient: Gradient {
        property bool vivid: false
        readonly property color red: vivid ? Theme.destructive
                                           : (Theme.darkMode ? "#4a2c2c" : "#f1d4d1")
        readonly property color yellow: vivid ? Theme.primary
                                              : (Theme.darkMode ? "#453c22" : "#f2e7c4")
        readonly property color green: vivid ? Theme.constructive
                                             : (Theme.darkMode ? "#26392d" : "#d6e8da")
        GradientStop { position: 0.00; color: red }
        GradientStop { position: 0.15; color: red }
        GradientStop { position: 0.15; color: yellow }
        GradientStop { position: 0.33; color: yellow }
        GradientStop { position: 0.33; color: green }
        GradientStop { position: 1.00; color: green }
    }

    component MeterChannel: Item {
        id: channel

        property Item mixer
        property real level: 0
        property real peak: 0

        readonly property real levelRatio: mixer.dbToRatio(mixer.linearToDb(level))
        readonly property real peakRatio: mixer.dbToRatio(mixer.linearToDb(peak))

        Rectangle {
            anchors.fill: parent
            radius: 1
            gradient: ZoneGradient {}
        }

        Item {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: Math.max(0, Math.min(1, channel.levelRatio)) * parent.height
            clip: true

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: channel.height
                radius: 1
                gradient: ZoneGradient { vivid: true }
            }
        }

        // Peak hold; red once the held peak has crossed 0 dBFS.
        Rectangle {
            visible: channel.peak > 0.001
            width: parent.width
            height: 2
            y: Math.max(0, parent.height * (1.0 - channel.peakRatio) - 1)
            color: channel.peak >= 1.0 ? Theme.destructive : Theme.panelForeground
        }
    }

    // Stereo peak meter. The zones are fixed to the scale, not to the bar, so a quiet signal
    // is green all the way up and only the part above -6 dB turns amber, above 0 red.
    component LevelMeter: Item {
        id: meter

        property Item mixer
        property real levelL: 0
        property real levelR: 0
        property real peakL: 0
        property real peakR: 0

        readonly property real innerH: height - 2

        implicitWidth: 16

        MeterChannel {
            x: 1
            y: 1
            width: meter.width / 2 - 2
            height: meter.innerH
            mixer: meter.mixer
            level: meter.levelL
            peak: meter.peakL
        }
        MeterChannel {
            x: meter.width / 2 + 1
            y: 1
            width: meter.width / 2 - 2
            height: meter.innerH
            mixer: meter.mixer
            level: meter.levelR
            peak: meter.peakR
        }

        // Hairline tick marks at the labelled scale points.
        Repeater {
            model: [0, -5, -10, -20, -30, -45]
            delegate: Rectangle {
                required property var modelData
                x: 1
                width: meter.width - 2
                height: 1
                y: 1 + meter.innerH * (1.0 - meter.mixer.dbToRatio(modelData))
                color: Theme.panelBackground
                opacity: 0.7
            }
        }
    }

    // One channel strip. Tracks and Master share it so their meters and faders line up;
    // Master simply hides the controls it does not have.
    component ChannelStrip: Item {
        id: strip

        property Item mixer
        property bool master: false
        property string name: ""
        property string fullName: name
        property bool muted: false
        property bool solo: false
        property bool armed: false
        // Voiceover recording targets audio tracks only.
        property bool canRecord: true
        property bool isVideo: false
        property bool armPaused: false
        property real volume: 1.0
        property real pan: 0.0
        property real levelL: 0
        property real levelR: 0
        property real peakL: 0
        property real peakR: 0

        signal muteToggled()
        signal soloToggled()
        signal armToggled()
        signal volumePreviewed(real gain)
        signal volumeCommitted()
        signal volumeSet(real gain)
        signal panPreviewed(real value)
        signal panCommitted()
        signal panSet(real value)

        readonly property real curDb: mixer.linearToDb(volume)
        readonly property color fillColor: armed ? Theme.destructive : Theme.primary

        function nudgeDb(step) {
            volumeSet(mixer.dbToLinear(mixer.snapDb(mixer.linearToDb(volume) + step)))
        }
        function nudgePan(step) {
            var p = Math.max(-1.0, Math.min(1.0, pan + step))
            panSet(Math.abs(p) < 0.03 ? 0.0 : p)
        }

        // Recording tint across the whole strip so the armed track reads from a distance.
        Rectangle {
            anchors.fill: parent
            visible: strip.armed
            color: Qt.rgba(Theme.destructive.r, Theme.destructive.g, Theme.destructive.b, Theme.darkMode ? 0.08 : 0.05)
        }

        // --- Name --------------------------------------------------------------
        Item {
            id: nameRow
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 32

            Row {
                anchors.centerIn: parent
                width: Math.min(implicitWidth, parent.width - Theme.spacingLg * 2)
                spacing: Theme.spacingSm

                Rectangle {
                    id: nameDot
                    width: 6
                    height: 6
                    radius: 3
                    anchors.verticalCenter: parent.verticalCenter
                    color: strip.armed ? (strip.armPaused ? "#eab308" : Theme.destructive)
                         : strip.master ? Theme.primary
                         : strip.isVideo ? Theme.clipVideoOverview : Theme.clipAudio
                }

                Text {
                    width: Math.min(implicitWidth, nameRow.width - Theme.spacingLg * 2 - nameDot.width - parent.spacing)
                    anchors.verticalCenter: parent.verticalCenter
                    text: strip.name
                    elide: Text.ElideRight
                    color: strip.master ? Theme.accentOnPanel : Theme.panelForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm
                    font.weight: Font.DemiBold
                }
            }

            HoverHandler { id: nameHover }
            ThemedToolTip {
                visible: nameHover.hovered
                text: strip.fullName + (strip.armed ? qsTr(" (recording)") : "")
            }
        }

        // --- Mute / Solo / Record -------------------------------------------------
        Row {
            id: toggleRow
            anchors.top: nameRow.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            height: 32
            spacing: Theme.spacingXs

            StripToggle {
                glyph: strip.muted ? Theme.icons.volumeOff : Theme.icons.volumeHigh
                checked: strip.muted
                tooltip: strip.master ? (strip.muted ? qsTr("Unmute master") : qsTr("Mute master"))
                                      : (strip.muted ? qsTr("Unmute") : qsTr("Mute"))
                onClicked: strip.muteToggled()
            }
            StripToggle {
                visible: !strip.master
                glyph: Theme.icons.headphones
                tone: "active"
                checked: strip.solo
                tooltip: strip.solo ? qsTr("Unsolo") : qsTr("Solo")
                onClicked: strip.soloToggled()
            }
            StripToggle {
                visible: !strip.master && strip.canRecord
                glyph: Theme.icons.mic
                checked: strip.armed
                blinking: strip.armed && !strip.armPaused
                tooltip: strip.armed
                         ? (strip.armPaused ? qsTr("Recording paused — click to finish") : qsTr("Recording — click to finish"))
                         : qsTr("Record voiceover on %1").arg(strip.name)
                onClicked: strip.armToggled()
            }
        }

        // --- Pan ---------------------------------------------------------------------
        Item {
            id: panArea
            anchors.top: toggleRow.bottom
            anchors.topMargin: Theme.spacingSm
            anchors.left: parent.left
            anchors.right: parent.right
            height: 62

            Canvas {
                id: panKnob
                visible: !strip.master
                width: 40
                height: 40
                anchors.horizontalCenter: parent.horizontalCenter
                antialiasing: true

                readonly property real value: strip.pan
                readonly property bool dark: Theme.darkMode
                readonly property bool hot: panMouse.containsMouse || panMouse.pressed
                onValueChanged: requestPaint()
                onDarkChanged: requestPaint()
                onHotChanged: requestPaint()

                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    const cx = width / 2
                    const cy = height / 2
                    const r = 17
                    // 270 degree sweep with centre at 12 o'clock.
                    const center = 1.5 * Math.PI
                    const target = center + value * 0.75 * Math.PI

                    ctx.lineCap = "round"
                    ctx.lineWidth = 2.5
                    ctx.strokeStyle = "" + Theme.sliderTrack
                    ctx.beginPath()
                    ctx.arc(cx, cy, r, 0.75 * Math.PI, 2.25 * Math.PI, false)
                    ctx.stroke()

                    if (Math.abs(value) > 0.005) {
                        ctx.strokeStyle = "" + Theme.primary
                        ctx.beginPath()
                        ctx.arc(cx, cy, r, Math.min(center, target), Math.max(center, target), false)
                        ctx.stroke()
                    }

                    ctx.beginPath()
                    ctx.arc(cx, cy, 12, 0, 2 * Math.PI, false)
                    ctx.fillStyle = "" + (hot ? Theme.panelMuted : Theme.panelAccent)
                    ctx.fill()
                    ctx.lineWidth = 1
                    ctx.strokeStyle = "" + Theme.panelBorder
                    ctx.stroke()

                    ctx.lineWidth = 2
                    ctx.strokeStyle = "" + Theme.panelForeground
                    ctx.beginPath()
                    ctx.moveTo(cx + 4 * Math.cos(target), cy + 4 * Math.sin(target))
                    ctx.lineTo(cx + 11 * Math.cos(target), cy + 11 * Math.sin(target))
                    ctx.stroke()
                }

                MouseArea {
                    id: panMouse
                    anchors.fill: parent
                    anchors.margins: -4
                    hoverEnabled: true
                    cursorShape: Qt.SizeVerCursor
                    preventStealing: true

                    property point last
                    property real dragPan: 0

                    onPressed: (mouse) => {
                        last = Qt.point(mouse.x, mouse.y)
                        dragPan = strip.pan
                    }
                    onPositionChanged: (mouse) => {
                        if (!pressed)
                            return
                        // Up or right pans right; Shift for fine control.
                        const fine = (mouse.modifiers & Qt.ShiftModifier) ? 0.2 : 1.0
                        const d = ((mouse.x - last.x) - (mouse.y - last.y)) / 100.0 * fine
                        last = Qt.point(mouse.x, mouse.y)
                        dragPan = Math.max(-1.0, Math.min(1.0, dragPan + d))
                        strip.panPreviewed(Math.abs(dragPan) < 0.04 ? 0.0 : dragPan)
                    }
                    onReleased: strip.panCommitted()
                    onDoubleClicked: strip.panSet(0.0)
                    onWheel: (wheel) => strip.nudgePan(wheel.angleDelta.y > 0 ? 0.05 : -0.05)
                }

                ThemedToolTip {
                    visible: panMouse.containsMouse && !panMouse.pressed
                    text: qsTr("Pan %1 — drag to adjust, double-click to center").arg(strip.mixer.formatPan(strip.pan))
                }
            }

            Text {
                visible: !strip.master
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                text: strip.mixer.formatPan(strip.pan)
                color: Math.abs(strip.pan) > 0.005 ? Theme.accentOnPanel : Theme.mutedForeground
                font.family: Theme.monoFontFamily
                font.pixelSize: Theme.fontSizeXs
            }
        }

        // --- Level readout -------------------------------------------------------------
        Rectangle {
            id: readout
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.spacingMd
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(parent.width - Theme.spacingLg, 64)
            height: 24
            radius: Theme.radiusXs
            color: readoutMouse.containsMouse ? Theme.popoverHover : Theme.panelAccent
            border.width: Theme.borderWidth
            border.color: Theme.panelBorder

            Text {
                anchors.centerIn: parent
                text: strip.mixer.formatDb(strip.curDb)
                color: strip.armed || strip.curDb > 0.05 ? Theme.destructive : Theme.panelForeground
                font.family: Theme.monoFontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            MouseArea {
                id: readoutMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.SizeVerCursor
                onDoubleClicked: strip.volumeSet(1.0)
                onWheel: (wheel) => strip.nudgeDb(wheel.angleDelta.y > 0 ? 0.5 : -0.5)
            }

            ThemedToolTip {
                visible: readoutMouse.containsMouse
                text: strip.armed
                      ? qsTr("Mic gain %1 dB (%2%) — scroll to adjust, double-click for 0 dB")
                            .arg(strip.mixer.formatDb(strip.curDb)).arg(Math.round(strip.volume * 100))
                      : qsTr("%1 dB — scroll to adjust, double-click for 0 dB").arg(strip.mixer.formatDb(strip.curDb))
            }
        }

        // --- Meter + fader ------------------------------------------------------------
        Row {
            id: meterRow
            anchors.top: panArea.bottom
            anchors.topMargin: Theme.spacingLg
            anchors.bottom: readout.top
            anchors.bottomMargin: Theme.spacingLg
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Theme.spacingSm

            LevelMeter {
                height: parent.height
                mixer: strip.mixer
                levelL: strip.levelL
                levelR: strip.levelR
                peakL: strip.peakL
                peakR: strip.peakR
            }

            // dB scale
            Item {
                width: 20
                height: parent.height

                Repeater {
                    model: [0, -5, -10, -20, -30, -45]
                    delegate: Text {
                        required property var modelData
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: 1 + (parent.height - 2) * (1.0 - strip.mixer.dbToRatio(modelData)) - height / 2
                        text: modelData === 0 ? "0" : String(-modelData)
                        color: Theme.mutedForeground
                        font.family: Theme.monoFontFamily
                        font.pixelSize: Theme.fontSizeTick
                    }
                }
            }

            Item {
                id: fader
                width: 26
                height: parent.height

                readonly property real capH: 16
                readonly property real travel: height - capH
                readonly property real capY: travel * (1.0 - strip.mixer.dbToRatio(strip.curDb))

                Accessible.role: Accessible.Slider
                Accessible.name: strip.master ? qsTr("Master volume") : qsTr("%1 volume").arg(strip.fullName)

                // Groove
                Rectangle {
                    id: groove
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: fader.capH / 2
                    width: 3
                    height: fader.travel
                    radius: 1.5
                    color: Theme.sliderTrack

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: Math.max(0, parent.height - fader.capY)
                        radius: parent.radius
                        color: strip.fillColor
                    }
                }

                // Unity (0 dB) notch
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: fader.capH / 2 + fader.travel * (1.0 - strip.mixer.dbToRatio(0)) - 0.5
                    width: 16
                    height: 1
                    color: Theme.mutedForeground
                }

                Rectangle {
                    id: cap
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: fader.capY
                    width: 24
                    height: fader.capH
                    radius: Theme.radiusXs
                    color: faderMouse.pressed ? strip.fillColor
                         : faderMouse.containsMouse ? Theme.panelMuted : Theme.panelAccent
                    border.width: Theme.borderWidth
                    border.color: faderMouse.pressed ? strip.fillColor : Theme.sliderTrack

                    Behavior on color {
                        ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                    }

                    Rectangle {
                        anchors.centerIn: parent
                        width: 12
                        height: 1
                        color: faderMouse.pressed ? Theme.primaryForeground : Theme.panelForeground
                    }
                }

                MouseArea {
                    id: faderMouse
                    anchors.fill: parent
                    anchors.leftMargin: -4
                    anchors.rightMargin: -4
                    hoverEnabled: true
                    cursorShape: pressed ? Qt.ClosedHandCursor : Qt.SizeVerCursor
                    preventStealing: true

                    property real lastY: 0
                    property real dragRatio: 0

                    function emitRatio() {
                        strip.volumePreviewed(strip.mixer.dbToLinear(strip.mixer.snapDb(strip.mixer.ratioToDb(dragRatio))))
                    }

                    // Grabbing the cap drags relative to where it was; clicking the groove
                    // jumps the cap there first. Shift slows the drag for fine trims.
                    onPressed: (mouse) => {
                        lastY = mouse.y
                        if (mouse.y >= fader.capY - 3 && mouse.y <= fader.capY + fader.capH + 3) {
                            dragRatio = strip.mixer.dbToRatio(strip.curDb)
                        } else {
                            dragRatio = Math.max(0, Math.min(1, 1.0 - (mouse.y - fader.capH / 2) / fader.travel))
                            emitRatio()
                        }
                    }
                    onPositionChanged: (mouse) => {
                        if (!pressed || fader.travel <= 0)
                            return
                        const fine = (mouse.modifiers & Qt.ShiftModifier) ? 0.2 : 1.0
                        dragRatio = Math.max(0, Math.min(1, dragRatio - (mouse.y - lastY) / fader.travel * fine))
                        lastY = mouse.y
                        emitRatio()
                    }
                    onReleased: strip.volumeCommitted()
                    onDoubleClicked: strip.volumeSet(1.0)
                    onWheel: (wheel) => strip.nudgeDb(wheel.angleDelta.y > 0 ? 0.5 : -0.5)
                }

                ThemedToolTip {
                    visible: faderMouse.containsMouse || faderMouse.pressed
                    text: (strip.armed ? qsTr("Mic gain %1 dB") : qsTr("Volume %1 dB")).arg(strip.mixer.formatDb(strip.curDb))
                          + (faderMouse.pressed ? "" : qsTr(" — Shift-drag for fine, double-click for 0 dB"))
                }
            }
        }

        // Divider
        Rectangle {
            anchors.right: parent.right
            width: 1
            height: parent.height
            color: Theme.panelBorder
        }
    }

    // Everything below exists only while the mixer is open (or still animating closed), so a
    // hidden mixer costs no strips, no meter timer and no track-list rebuilds.
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
                        name: t.name && t.name.length > 0 ? t.name : (isVideo ? "V" + videoCount : "A" + audioCount),
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
                            iconColor: Theme.mutedForeground
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        Text {
                            text: qsTr("Audio Mixer")
                            color: Theme.panelForeground
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
                        onClicked: EditorState.audioMixerVisible = false
                    }

                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 1
                        color: Theme.panelBorder
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
                           ? Qt.rgba(0.92, 0.70, 0.03, Theme.darkMode ? 0.10 : 0.07)
                           : Qt.rgba(Theme.destructive.r, Theme.destructive.g, Theme.destructive.b, Theme.darkMode ? 0.08 : 0.05)

                    Behavior on height {
                        NumberAnimation { duration: Theme.durationBase; easing.type: Theme.easing }
                    }

                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 1
                        color: Theme.panelBorder
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
                                    radius: 4
                                    color: EditorState.isAudioRecordingPaused ? "#eab308" : Theme.destructive
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
                                    color: EditorState.isAudioRecordingPaused ? "#d97706" : Theme.destructive
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
                                    color: Theme.panelForeground
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
                                radius: Theme.radiusXs
                                color: voMicMouse.containsMouse || voMicMenu.opened ? Theme.popoverHover : Theme.panelAccent
                                border.color: Theme.panelBorder
                                border.width: Theme.borderWidth
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
                                    iconColor: Theme.mutedForeground
                                    anchors.verticalCenter: parent.verticalCenter
                                }

                                Text {
                                    text: EditorState.currentMicrophoneName || qsTr("Default Mic")
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeXs
                                    color: Theme.panelForeground
                                    elide: Text.ElideRight
                                    width: Math.max(10, voMicPickerBtn.width - Theme.iconSizeSm * 2 - Theme.spacingMd * 2 - Theme.spacingSm * 2)
                                    anchors.verticalCenter: parent.verticalCenter
                                }

                                IconGlyph {
                                    glyph: Theme.icons.chevronDown
                                    iconSize: Theme.iconSizeSm
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
                                    radius: Theme.radiusSm
                                    color: addTrackMouse.containsMouse ? Theme.popoverHover : "transparent"
                                    border.width: Theme.borderWidth
                                    border.color: addTrackMouse.containsMouse ? Theme.panelMuted : Theme.panelBorder

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
                                            iconColor: addTrackMouse.containsMouse ? Theme.panelForeground : Theme.mutedForeground
                                        }

                                        Text {
                                            width: parent.width
                                            text: qsTr("Add audio track")
                                            horizontalAlignment: Text.AlignHCenter
                                            wrapMode: Text.WordWrap
                                            font.family: Theme.fontFamily
                                            font.pixelSize: Theme.fontSizeXs
                                            color: addTrackMouse.containsMouse ? Theme.panelForeground : Theme.mutedForeground
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

                                delegate: ChannelStrip {
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

                    ChannelStrip {
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
                            color: Theme.panelAccent
                            opacity: 0.5
                        }
                    }
                }
            }

            // --- Resize handle ---------------------------------------------------------------
            // Drag the left edge to resize; double-click returns to fitting the strips.
            MouseArea {
                id: resizeHandle
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
                    EditorState.audioMixerWidth = Math.max(root.minMixerWidth, Math.min(root.maxMixerWidth, pressWidth - dx))
                }
                onReleased: Haptics.drop()
                onDoubleClicked: EditorState.audioMixerWidth = 0

                // Resting edge line; turns amber while the handle is hovered or dragged.
                Rectangle {
                    anchors.left: parent.left
                    width: resizeHandle.pressed || resizeHandle.containsMouse ? 2 : 1
                    height: parent.height
                    color: resizeHandle.pressed || resizeHandle.containsMouse ? Theme.primary : Theme.panelBorder

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
