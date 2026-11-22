import QtQuick
import Drift

// View settings. A phone transport has room for the transport and very
// little else, so the four playback/view chips live in a strip that the
// sliders button raises over the canvas instead of competing for it.
Rectangle {
    id: strip

    property bool optionsOpen: false
    property bool viewMoved: false
    property real userZoom: 1
    property bool view3d: false

    signal resetViewRequested()
    signal view3dToggled()

    height: optionsFlow.height + Theme.spacingLg * 2
    color: Theme.c.androidPreviewOptionsStrip.background
    visible: opacity > 0
    opacity: strip.optionsOpen && !EditorState.preview.canvasCropMode ? 1 : 0

    Behavior on opacity {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }

    Flow {
        id: optionsFlow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingMd

        // The zoom readout, and the way back to 100% — the same pair
        // desktop's toolbar offers. It replaces a Fit/Fill toggle, which
        // only stretched the letterbox rect: PreviewItem aspect-fits the
        // frame inside it either way, so Fill changed nothing you could see.
        ThemedChip {
            selected: strip.viewMoved
            text: Math.round(strip.userZoom * 100) + "%"
            onClicked: strip.resetViewRequested()
        }

        ThemedChip {
            // Same five the desktop quality combo offers, in the same
            // order — Auto is the engine's default, and cycling a list
            // that left it out mislabelled it as Full.
            readonly property var values: ["full", "half", "quarter", "eighth", "auto"]
            readonly property var labels: [qsTr("Full"), qsTr("Half"),
                                           qsTr("Quarter"), qsTr("1/8 (Eco)"), qsTr("Auto")]
            readonly property int currentIndex:
                Math.max(0, values.indexOf(EditorState.playback.previewQuality))
            text: qsTr("Quality: %1").arg(labels[currentIndex])
            onClicked: EditorState.playback.previewQuality =
                       values[(currentIndex + 1) % values.length]
        }

        ThemedChip {
            readonly property var values: [0.25, 0.5, 1.0, 1.5, 2.0, 4.0]
            readonly property var labels: ["0.25×", "0.5×", "1×", "1.5×", "2×", "4×"]
            readonly property int currentIndex:
                Math.max(0, values.indexOf(EditorState.playback.playbackRate))
            text: labels[currentIndex]
            onClicked: EditorState.playback.playbackRate =
                       values[(currentIndex + 1) % values.length]
        }

        ThemedChip {
            // Populated from the engine, so the entries are the backends
            // whose device actually opens on this phone. Hidden when that
            // leaves nothing to choose between.
            readonly property var modes: EditorState.playback.decodeModes
            readonly property var values:
                modes.map(function (m) { return m.id })
            readonly property int currentIndex:
                Math.max(0, values.indexOf(EditorState.playback.decodeMode))
            visible: modes.length > 1
            text: modes[currentIndex].label
            onClicked: EditorState.playback.decodeMode =
                       values[(currentIndex + 1) % values.length]
        }

        ThemedChip {
            selected: strip.view3d
            text: qsTr("3D view")
            onClicked: strip.view3dToggled()
        }

        ThemedChip {
            selected: EditorState.preview.guidesEnabled
            text: qsTr("Guides")
            onClicked: EditorState.preview.guidesEnabled = !EditorState.preview.guidesEnabled
        }
    }
}
