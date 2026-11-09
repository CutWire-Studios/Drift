import QtQuick
import QtQuick.Window
import Drift
// What the canvas says when it has no picture to show, plus the diagnostics and recording badges.
// `compact` is the phone's version: one short line per state and no badges.
Item {
    id: root

    property bool compact: false

    // Top-left so it never covers the transport controls or the bottom-right resolution readout.
    // Only visible while the diagnostics dialog has the counters armed.
    Loader {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: Theme.spacingLg
        z: 10
        active: !root.compact && !!EditorState.playback.stats && EditorState.playback.stats.active
        sourceComponent: Component { PlaybackStatsOverlay { } }
    }

    // Voiceover recording indicator overlay
    PreviewVoiceoverBadge {
        id: voiceoverBadge
        visible: !root.compact && EditorState.isRecordingAudio
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.spacingLg
        z: 11
        paused: EditorState.isAudioRecordingPaused
        seconds: EditorState.audioRecordSeconds
    }

    Rectangle {
        id: bakeBadge

        readonly property string bakeId: EditorState.playback.currentFrameBakeId
        readonly property var bake: {
            const list = EditorState.bakes.bakes
            for (let i = 0; i < list.length; ++i) {
                if (list[i].id === bakeId)
                    return list[i]
            }
            return null
        }
        readonly property bool stale: bake !== null && bake.state === "stale"

        visible: !root.compact && bakeId !== "" && bake !== null
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: Theme.spacingLg
        anchors.topMargin: voiceoverBadge.visible ? Theme.spacingLg * 2 + voiceoverBadge.height : Theme.spacingLg
        z: 11
        width: bakeBadgeLabel.implicitWidth + Theme.spacingLg * 2
        height: bakeBadgeLabel.implicitHeight + Theme.spacingXs * 2
        radius: Theme.c.previewBakeBadge.radius
        color: stale ? Theme.c.previewBakeBadge.stale : Theme.c.previewBakeBadge.fresh

        Text {
            id: bakeBadgeLabel
            anchors.centerIn: parent
            text: bakeBadge.stale ? qsTr("Stale bake") : qsTr("Baked")
            color: Theme.c.previewBakeBadge.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeTiny
        }
    }

    // Interface only, like the rest of this layer: it never reaches a render or export.
    Loader {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.spacingLg
        height: Math.round(parent.height * 0.3)
        z: 5
        active: !root.compact && EditorState.teleprompter.overlayEnabled && EditorState.teleprompter.script.length > 0
        sourceComponent: Component {
            Rectangle {
                radius: Theme.c.teleprompterOverlay.radius
                color: Theme.c.teleprompterOverlay.background
                clip: true

                TeleprompterView {
                    anchors.fill: parent
                    fontScale: 0.75
                    readLineRatio: 0.4
                    textColor: Theme.c.teleprompterOverlay.foreground
                    markerColor: Theme.c.teleprompterOverlay.marker
                }
            }
        }
    }

    // On a brand-new project this — the largest, most central panel — said nothing at all, while
    // the timeline below it explained what to do. The terse gap message below is right when a
    // project has content and the playhead is simply over a gap; it is not an answer to "what do
    // I do first".
    EmptyState {
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.spacing3xl, 280)
        visible: !root.compact && EditorState.trackCount === 0
        glyph: Theme.icons.film
        title: qsTr("Nothing to preview yet")
        // No CTA: importing and adding tracks both live in the panels either side, and this one
        // should not compete with them.
        hint: qsTr("Import media and drag it onto the timeline below to see it here.")
    }

    readonly property bool gpuFailed: EditorState.playback.gpuCompositorStatus !== "unknown"
                                      && !EditorState.playback.gpuCompositorReady

    // A dead GPU compositor produces no frame at any playhead position, which for a long time
    // read as "No clip at the current time" and sent people hunting through their timeline. Say
    // what actually happened, and where the details are. Held back until the first probe answers,
    // so a slow driver does not flash a failure during startup.
    EmptyState {
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.spacing3xl, 280)
        visible: !root.compact && EditorState.trackCount > 0 && root.gpuFailed
        glyph: Theme.icons.warning
        title: qsTr("GPU preview unavailable")
        hint: EditorState.playback.gpuCompositorStatus === "version-too-low"
              && EditorState.playback.gpuCompositorDetail
              ? qsTr("Your graphics driver only provides %1. Drift's preview needs OpenGL 3.3.")
                    .arg(EditorState.playback.gpuCompositorDetail)
              : qsTr("Drift could not start its GPU renderer, so the preview cannot draw.")
        actionText: qsTr("Debug info")
        onActionTriggered: ShellBridge.main.openDebugInfo()
    }

    PreviewCompactGpuCaption {
        visible: root.compact && root.gpuFailed
    }

    // Fades rather than popping, so scrubbing across a gap no longer flickers this text on and
    // off. Only ever a gap message: when the compositor is down the state above explains that
    // instead.
    PreviewGapCaption {
        show: !(EditorState.playback.hasFrame
                || (!root.compact && EditorState.trackCount === 0)
                || !EditorState.playback.gpuCompositorReady)
        message: EditorState.activeAudioClipAtPlayhead().path
                 ? qsTr("Audio only") : qsTr("No clip at the current time")
    }
}
