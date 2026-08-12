import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Drift
import ".."

// Transport toolbar: timecode readout, play/pause, zoom readout, preview
// quality, guides and fullscreen. The three groups were independently anchored
// and overlapped at narrow preview widths. A RowLayout keeps Play centred
// while the side groups compress instead of colliding.
Item {
    id: toolbar

    // Owning PreviewPanel (playing, projectFps, currentSeconds, durationSeconds,
    // previewFullscreen, formatTimecode, fullscreenRequested) and its viewport
    // (userZoom, resetView).
    property var panel
    property var previewViewport

    width: parent.width
    height: Theme.previewToolbarPaddingTop + Theme.previewToolbarPaddingBottom
            + Theme.iconButtonSize

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
            text: qsTr("Current time / total · %1 frames per second").arg(toolbar.panel.projectFps)
            visible: timecodeHover.hovered
        }

        Text {
            text: toolbar.panel.formatTimecode(toolbar.panel.currentSeconds)
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
            text: toolbar.panel.formatTimecode(toolbar.panel.durationSeconds)
            color: Theme.mutedForeground
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.fontSizeXs
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    // Spacers on both sides keep Play optically centred while still
    // letting the whole row shrink instead of overlap.
    Item { Layout.fillWidth: true; Layout.minimumWidth: 0 }

    Row {
        Layout.alignment: Qt.AlignVCenter
        spacing: Theme.spacingXs

        // Jump amount comes from the modifiers held at click time rather than from a separate
        // control: three amounts in each direction would be six more buttons in a row that has to
        // stay centred. AbstractButton.clicked carries no modifiers, hence the query into Qt.
        function jumpStep() {
            const modifiers = EditorState.keyboardModifiers()
            if (modifiers & Qt.ControlModifier)
                return 10
            if (modifiers & Qt.ShiftModifier)
                return 5
            return 1
        }

        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            glyph: Theme.icons.rewind
            variant: "text"
            tooltip: qsTr("Jump back 1s · Shift for 5s · Ctrl for 10s")
            onClicked: EditorState.jumpSeconds(-parent.jumpStep())
        }

        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            glyph: Theme.icons.stepBack
            variant: "text"
            tooltip: qsTr("Previous frame")
            onClicked: EditorState.stepFrames(-1)
        }

        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            glyph: toolbar.panel.playing ? Theme.icons.pause : Theme.icons.play
            variant: "text"
            tooltip: toolbar.panel.playing ? qsTr("Pause") : qsTr("Play")
            onClicked: EditorState.togglePlayback()
        }

        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            glyph: Theme.icons.stepForward
            variant: "text"
            tooltip: qsTr("Next frame")
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
            tooltip: qsTr("Jump forward 1s · Shift for 5s · Ctrl for 10s")
            onClicked: EditorState.jumpSeconds(parent.jumpStep())
        }
    }

    Item { Layout.fillWidth: true; Layout.minimumWidth: 0 }

    Row {
        Layout.alignment: Qt.AlignVCenter
        spacing: Theme.spacingLg + Theme.spacingXs

        // Mask view replaces the picture with a black-and-white coverage map, which is
        // disorienting if you have forgotten it is on. Shown only while active, so it doubles
        // as the indicator and the way out.
        ThemedToggleButton {
            anchors.verticalCenter: parent.verticalCenter
            visible: EditorState.maskViewClipId !== ""
            text: qsTr("Cutout view")
            checked: true
            tooltip: qsTr("Showing what the cutouts cover. Exports are unaffected. Click to "
                          + "go back to the picture.")
            onClicked: EditorState.maskViewClipId = ""
        }

        // Replaces the Fit/Fill toggle, which only stretched the letterbox
        // rect — PreviewItem aspect-fits the frame inside it either way.
        Text {
            anchors.verticalCenter: parent.verticalCenter
            width: 44
            text: Math.round(toolbar.previewViewport.userZoom * 100) + "%"
            color: Theme.mutedForeground
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.fontSizeXs
            horizontalAlignment: Text.AlignHCenter

            ThemedToolTip {
                text: qsTr("Preview zoom — Ctrl+scroll over the preview to zoom, "
                           + "middle-drag to pan. Click to reset to 100%.")
                visible: zoomLabelMouse.containsMouse
            }

            MouseArea {
                id: zoomLabelMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: toolbar.previewViewport.resetView()
            }
        }

        Rectangle {
            width: Theme.borderWidth
            height: Theme.iconSizeBase
            color: Theme.panelBorder
            anchors.verticalCenter: parent.verticalCenter
        }

        ThemedComboBox {
            id: qualityCombo
            anchors.verticalCenter: parent.verticalCenter
            implicitHeight: Theme.controlHeightSm
            width: widestContentWidth + leftPadding + rightPadding
            leftPadding: Theme.spacingMd
            rightPadding: Theme.spacing2xl
            font.pixelSize: Theme.fontSizeXs
            // Display is capitalised; the engine stores the lowercase value.
            readonly property var values: ["full", "half", "quarter", "auto"]
            model: [qsTr("Full"), qsTr("Half"), qsTr("Quarter"), qsTr("Auto")]
            tooltip: qsTr("Preview quality — lower is smoother while editing.\n"
                          + "Full, Half and Quarter are fixed fractions of the project resolution: "
                          + "Full composites exactly what an export would.\n"
                          + "Auto renders only as many pixels as the preview actually shows, and "
                          + "lowers that further while playback cannot keep up.")
            currentIndex: Math.max(0, values.indexOf(EditorState.playback.previewQuality))
            onActivated: EditorState.playback.previewQuality = values[currentIndex]
        }

        ThemedComboBox {
            id: speedCombo
            anchors.verticalCenter: parent.verticalCenter
            implicitHeight: Theme.controlHeightSm
            width: widestContentWidth + leftPadding + rightPadding
            leftPadding: Theme.spacingMd
            rightPadding: Theme.spacing2xl
            font.pixelSize: Theme.fontSizeXs
            readonly property var values: [0.25, 0.5, 1.0, 1.5, 2.0, 4.0]
            model: ["0.25×", "0.5×", "1×", "1.5×", "2×", "4×"]
            // Quality mode steps one frame per completed render and never opens the audio sink, so
            // there is no real-time rate for a speed to be a multiple of.
            enabled: EditorState.playback.playbackMode !== "quality"
            tooltip: enabled
                     ? qsTr("Playback speed")
                     : qsTr("Playback speed applies to Fast mode; Quality mode is not real time")
            currentIndex: Math.max(0, values.indexOf(EditorState.playback.playbackRate))
            onActivated: EditorState.playback.playbackRate = values[currentIndex]
        }

        ThemedComboBox {
            id: playbackModeCombo
            anchors.verticalCenter: parent.verticalCenter
            implicitHeight: Theme.controlHeightSm
            width: widestContentWidth + leftPadding + rightPadding
            leftPadding: Theme.spacingMd
            rightPadding: Theme.spacing2xl
            font.pixelSize: Theme.fontSizeXs
            readonly property var values: ["fast", "quality"]
            model: [qsTr("Fast"), qsTr("Quality")]
            tooltip: qsTr("Playback mode\nFast: plays in real time with audio, skipping frames it "
                          + "cannot render in time — use this for general playback.\n"
                          + "Quality: renders and shows every single frame, silently and slower "
                          + "than real time — use this to check complex transitions frame by frame.")
            currentIndex: Math.max(0, values.indexOf(EditorState.playback.playbackMode))
            onActivated: EditorState.playback.playbackMode = values[currentIndex]
        }

        Rectangle {
            width: Theme.borderWidth
            height: Theme.iconSizeBase
            color: Theme.panelBorder
            anchors.verticalCenter: parent.verticalCenter
        }

        IconButton {
            glyph: Theme.icons.grid
            variant: "text"
            tooltip: qsTr("Toggle guides")
            anchors.verticalCenter: parent.verticalCenter
            active: EditorState.guidesEnabled
            onClicked: EditorState.guidesEnabled = !EditorState.guidesEnabled
        }

        IconButton {
            glyph: toolbar.panel.previewFullscreen ? Theme.icons.minimize : Theme.icons.maximize
            variant: "text"
            tooltip: toolbar.panel.previewFullscreen
                     ? qsTr("Exit fullscreen preview (Esc)")
                     : qsTr("Fullscreen preview")
            anchors.verticalCenter: parent.verticalCenter
            active: toolbar.panel.previewFullscreen
            // The window and the surrounding panels belong to Main, so the
            // toggle is requested rather than performed here.
            onClicked: toolbar.panel.fullscreenRequested()
        }
    }
    }
}
