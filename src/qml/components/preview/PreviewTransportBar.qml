import QtQuick
import QtQuick.Layouts
import Drift

// The strip under the preview that is always there: timecode, transport and fullscreen. Everything
// else lives in the preview's OSD. A RowLayout keeps Play centred while the side groups compress
// instead of colliding at narrow widths.
Item {
    id: bar

    property int projectFps: 30
    property real currentSeconds: 0
    property real durationSeconds: 0
    property bool fullscreen: false
    // Formats seconds as the panel's timecode.
    property var formatTimecode: function (seconds) { return "" }
    signal fullscreenRequested()

    implicitHeight: Theme.iconButtonSize + Theme.previewTransportPadding * 2

    ThemeSlot {
        anchors.fill: parent
        z: -1
        slot: "PreviewTransportBarBackground"
        control: bar
        fallback: Component {
            Rectangle {
                anchors.fill: parent
                color: Theme.c.previewTransportBar.background
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacing2xl + Theme.spacingSm
        anchors.rightMargin: Theme.spacing2xl + Theme.spacingSm
        spacing: Theme.spacingLg

        PreviewTransportTimecode {
            projectFps: bar.projectFps
            currentSeconds: bar.currentSeconds
            durationSeconds: bar.durationSeconds
            formatTimecode: bar.formatTimecode
        }

        // Spacers on both sides keep Play optically centred while still letting the whole row
        // shrink instead of overlap.
        Item { Layout.fillWidth: true; Layout.minimumWidth: 0 }

        Row {
            Layout.alignment: Qt.AlignVCenter
            spacing: Theme.spacingXs

            PreviewTransportGoToStartButton {
                shortcutTip: qsTr("Go to start")
                shortcutActionId: "goToStart"
            }
            PreviewTransportRewindButton { }
            PreviewTransportStepBackButton {
                shortcutTip: qsTr("Previous frame")
                shortcutActionId: "stepBack"
            }
            PreviewTransportPlayButton { }
            PreviewTransportStepForwardButton {
                shortcutTip: qsTr("Next frame")
                shortcutActionId: "stepForward"
            }
            PreviewTransportLoopButton { }
            PreviewTransportFastForwardButton { }
            PreviewTransportGoToEndButton {
                shortcutTip: qsTr("Go to end")
                shortcutActionId: "goToEnd"
            }
        }

        Item { Layout.fillWidth: true; Layout.minimumWidth: 0 }

        PreviewTransportFullscreenButton {
            fullscreen: bar.fullscreen
            onFullscreenRequested: bar.fullscreenRequested()
        }
    }
}
