import QtQuick
import QtQuick.Layouts
import Drift
import ".."

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

    function withShortcut(label, actionId) {
        const key = EditorState.shortcutFor(actionId)
        return key.length > 0 ? qsTr("%1 (%2)").arg(label).arg(key) : label
    }

    implicitHeight: Theme.iconButtonSize + Theme.previewTransportPadding * 2

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacing2xl + Theme.spacingSm
        anchors.rightMargin: Theme.spacing2xl + Theme.spacingSm
        spacing: Theme.spacingLg

        Row {
            Layout.alignment: Qt.AlignVCenter
            spacing: 0

            HoverHandler { id: timecodeHover }

            ThemedToolTip {
                text: qsTr("Current time / total · %1 frames per second").arg(bar.projectFps)
                visible: timecodeHover.hovered
            }

            Text {
                text: bar.formatTimecode(bar.currentSeconds)
                color: Theme.primary
                font.family: Theme.monoFontFamily
                font.pixelSize: Theme.fontSizeXs
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: " / "
                color: Theme.mutedForeground
                font.family: Theme.monoFontFamily
                font.pixelSize: Theme.fontSizeXs
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: bar.formatTimecode(bar.durationSeconds)
                color: Theme.mutedForeground
                font.family: Theme.monoFontFamily
                font.pixelSize: Theme.fontSizeXs
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        // Spacers on both sides keep Play optically centred while still letting the whole row
        // shrink instead of overlap.
        Item { Layout.fillWidth: true; Layout.minimumWidth: 0 }

        Row {
            Layout.alignment: Qt.AlignVCenter
            spacing: Theme.spacingXs

            // Jump amount comes from the modifiers held at click time rather than from a separate
            // control: three amounts in each direction would be six more buttons in a row that has
            // to stay centred. AbstractButton.clicked carries no modifiers, hence the query into Qt.
            function jumpStep() {
                const modifiers = EditorState.keyboardModifiers()
                if (Theme.primaryModifierPressed(modifiers))
                    return 10
                if (modifiers & Qt.ShiftModifier)
                    return 5
                return 1
            }

            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                glyph: Theme.icons.rewind
                variant: "text"
                tooltip: Theme.platformShortcutText(qsTr("Jump back 1s · Shift for 5s · Ctrl for 10s"))
                onClicked: EditorState.jumpSeconds(-parent.jumpStep())
            }

            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                glyph: Theme.icons.stepBack
                variant: "text"
                tooltip: bar.withShortcut(qsTr("Previous frame"), "stepBack")
                onClicked: EditorState.stepFrames(-1)
            }

            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                glyph: EditorState.playing ? Theme.icons.pause : Theme.icons.play
                variant: "text"
                tooltip: EditorState.playing ? qsTr("Pause") : qsTr("Play")
                onClicked: EditorState.togglePlayback()
            }

            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                glyph: Theme.icons.stepForward
                variant: "text"
                tooltip: bar.withShortcut(qsTr("Next frame"), "stepForward")
                onClicked: EditorState.stepFrames(1)
            }

            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                glyph: Theme.icons.repeat
                variant: "text"
                tooltip: EditorState.loopWorkAreaEnabled
                         ? qsTr("Loop work area on — click to turn off")
                         : qsTr("Loop work area off — click to turn on")
                active: EditorState.loopWorkAreaEnabled
                enabled: EditorState.workAreaActive
                onClicked: EditorState.toggleLoopWorkArea()
            }

            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                glyph: Theme.icons.fastForward
                variant: "text"
                tooltip: Theme.platformShortcutText(qsTr("Jump forward 1s · Shift for 5s · Ctrl for 10s"))
                onClicked: EditorState.jumpSeconds(parent.jumpStep())
            }
        }

        Item { Layout.fillWidth: true; Layout.minimumWidth: 0 }

        IconButton {
            Layout.alignment: Qt.AlignVCenter
            glyph: bar.fullscreen ? Theme.icons.minimize : Theme.icons.maximize
            variant: "text"
            tooltip: bar.fullscreen ? qsTr("Exit fullscreen preview (Esc)") : qsTr("Fullscreen preview")
            active: bar.fullscreen
            // The window and the surrounding panels belong to Main, so the toggle is requested
            // rather than performed here.
            onClicked: bar.fullscreenRequested()
        }
    }
}
