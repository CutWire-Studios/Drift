import QtQuick
import Drift

// One channel strip. Tracks and Master share it so their meters and faders line up;
// Master simply hides the controls it does not have.
Item {
    id: strip

    property var mixer
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
    // Control-surface target for the fader while learn mode is on, e.g. "mixer.volume.1".
    property string learnId: ""
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
    readonly property color fillColor: armed ? Theme.c.mixerChannelStrip.armedFill : Theme.c.mixerChannelStrip.fill

    function nudgeDb(step) {
        volumeSet(mixer.dbToLinear(mixer.snapDb(mixer.linearToDb(volume) + step)))
    }
    function nudgePan(step) {
        var p = Math.max(-1.0, Math.min(1.0, pan + step))
        panSet(Math.abs(p) < 0.03 ? 0.0 : p)
    }

    MixerChannelArmTint {
        anchors.fill: parent
        armed: strip.armed
    }

    MixerChannelName {
        id: nameRow
        channel: strip
    }

    // --- Mute / Solo / Record -------------------------------------------------
    Row {
        id: toggleRow
        anchors.top: nameRow.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        height: 32
        spacing: Theme.spacingXs

        MixerStripToggle {
            glyph: strip.muted ? Theme.icons.volumeOff : Theme.icons.volumeHigh
            checked: strip.muted
            tooltip: strip.master ? (strip.muted ? qsTr("Unmute master") : qsTr("Mute master"))
                                  : (strip.muted ? qsTr("Unmute") : qsTr("Mute"))
            onClicked: strip.muteToggled()
        }
        MixerStripToggle {
            visible: !strip.master
            glyph: Theme.icons.headphones
            tone: "active"
            checked: strip.solo
            tooltip: strip.solo ? qsTr("Unsolo") : qsTr("Solo")
            onClicked: strip.soloToggled()
        }
        MixerStripToggle {
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

    MixerPanControl {
        id: panArea
        channel: strip
        anchors.top: toggleRow.bottom
        anchors.topMargin: Theme.spacingSm
        anchors.left: parent.left
        anchors.right: parent.right
    }

    MixerLevelReadout {
        id: readout
        channel: strip
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

        MixerLevelMeter {
            height: parent.height
            mixer: strip.mixer
            levelL: strip.levelL
            levelR: strip.levelR
            peakL: strip.peakL
            peakR: strip.peakR
        }

        MixerDbScale {
            mixer: strip.mixer
        }

        MixerFader {
            channel: strip
        }
    }

    SurfaceLearnArea {
        anchors.fill: meterRow
        targetId: strip.learnId
        label: qsTr("%1 volume").arg(strip.name)
        visible: strip.learnId !== "" && EditorState.surfaces.learnMode
    }

    MixerChannelDivider {
    }
}
