import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Drift
import ".."

// The preview's less-used settings, behind the OSD's "⋯": preview quality, playback speed, how
// video is decoded, and keeping mask handles up.
Popup {
    id: root

    // Open, or with the decode explanation up: either way the OSD should stay put.
    readonly property bool busy: opened || decodeGpuDialog.visible

    padding: Theme.spacingLg
    // Not modal, and pressing the parent button does not count as outside, so that button can
    // toggle the menu instead of closing and reopening it.
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

    background: Rectangle {
        color: Theme.panelBackground
        border.width: Theme.borderWidth
        border.color: Theme.panelBorder
        radius: Theme.radiusMd
    }

    component RowLabel: Text {
        color: Theme.mutedForeground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
        Layout.alignment: Qt.AlignVCenter
    }

    contentItem: GridLayout {
        columns: 2
        columnSpacing: Theme.spacingLg
        rowSpacing: Theme.spacingMd

        RowLabel { text: qsTr("Quality") }
        ThemedComboBox {
            id: qualityCombo
            Layout.fillWidth: true
            Layout.minimumWidth: widestContentWidth + leftPadding + rightPadding
            implicitHeight: Theme.controlHeightSm
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

        RowLabel { text: qsTr("Speed") }
        ThemedComboBox {
            Layout.fillWidth: true
            Layout.minimumWidth: widestContentWidth + leftPadding + rightPadding
            implicitHeight: Theme.controlHeightSm
            leftPadding: Theme.spacingMd
            rightPadding: Theme.spacing2xl
            font.pixelSize: Theme.fontSizeXs
            readonly property var values: [0.25, 0.5, 1.0, 1.5, 2.0, 4.0]
            model: ["0.25×", "0.5×", "1×", "1.5×", "2×", "4×"]
            tooltip: qsTr("Playback speed")
            currentIndex: Math.max(0, values.indexOf(EditorState.playback.playbackRate))
            onActivated: EditorState.playback.playbackRate = values[currentIndex]
        }

        RowLabel { text: qsTr("Decode") }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            ThemedComboBox {
                id: decodeCombo
                Layout.fillWidth: true
                Layout.minimumWidth: widestContentWidth + leftPadding + rightPadding
                implicitHeight: Theme.controlHeightSm
                leftPadding: Theme.spacingMd
                rightPadding: Theme.spacing2xl
                font.pixelSize: Theme.fontSizeXs
                // Populated from the engine: the hardware entries are the backends whose device
                // actually opens here, so anything listed is something that runs. Rows carry
                // `warn`/`note` for a backend that decodes on a GPU other than the one drawing,
                // which the delegate marks and the dialog below explains.
                readonly property var modes: EditorState.playback.decodeModes
                readonly property var values: modes.map(function (m) { return m.id })
                model: modes
                textRole: "label"
                tooltip: qsTr("How video is decoded for preview.\n"
                              + "Auto picks per clip: hardware for high-quality 4K, software otherwise.\n"
                              + "Software is smoother for most clips. It uses more CPU.\n"
                              + "Hardware is better for high-quality 4K, and forces one GPU decoder.\n"
                              + "If playback stutters, try another.")
                currentIndex: Math.max(0, values.indexOf(EditorState.playback.decodeMode))
                onActivated: {
                    const mode = modes[currentIndex]
                    if (mode && mode.warn)
                        decodeGpuDialog.confirm(mode)
                    else
                        EditorState.playback.decodeMode = mode.id
                }
            }

            // The combo's list is shut most of the time, so the chosen row's warning needs
            // somewhere to live on the closed control too.
            IconGlyph {
                id: decodeWarnGlyph
                glyph: Theme.icons.warning
                iconSize: Theme.iconSizeSm
                iconColor: Theme.warning
                readonly property var mode: decodeCombo.modes[decodeCombo.currentIndex]
                visible: mode !== undefined && mode.warn === true

                HoverHandler { id: decodeWarnHover }
                ThemedToolTip {
                    text: decodeWarnGlyph.mode ? decodeWarnGlyph.mode.note : ""
                    visible: decodeWarnGlyph.visible && decodeWarnHover.hovered
                }
            }
        }

        ThemedSwitch {
            Layout.columnSpan: 2
            Layout.fillWidth: true
            text: qsTr("Keep mask handles up")
            tooltip: qsTr("Keep mask handles on the preview while another clip is selected")
            checked: EditorState.preview.maskEditMode
            onToggled: EditorState.preview.maskEditMode = checked
        }
    }

    // Picking a decoder that runs on the other GPU is a legitimate choice — some codecs only the
    // discrete card decodes — so this explains the cost rather than blocking it, and offers the
    // change that would actually fix it where Drift can make one.
    ThemedDialog {
        id: decodeGpuDialog

        property var mode: null

        function confirm(pending) {
            mode = pending
            open()
        }

        title: qsTr("Decoding on a different graphics card")
        preferredWidth: Theme.dialogWidthMd
        acceptText: qsTr("Use anyway")
        rejectText: qsTr("Cancel")
        // Destructive-ish in the sense that matters here: Enter should not commit a choice the
        // user opened this dialog to understand.
        acceptOnReturn: false

        onAccepted: if (mode) EditorState.playback.decodeMode = mode.id
        // The combo already moved its own highlight, so put it back on what is still in use.
        onRejected: decodeCombo.currentIndex =
            Math.max(0, decodeCombo.values.indexOf(EditorState.playback.decodeMode))

        contentItem: Column {
            spacing: Theme.spacingLg

            ThemedLabel {
                width: parent.width
                tone: "default"
                size: "sm"
                text: decodeGpuDialog.mode ? decodeGpuDialog.mode.note : ""
            }

            ThemedLabel {
                width: parent.width
                visible: !EditorState.preferences.gpuPreferenceSupported && Qt.platform.os === "linux"
                text: qsTr("Launching Drift with prime-run (or DRI_PRIME=1) puts OpenGL on the "
                           + "same card as the decoder.")
            }

            ThemedButton {
                visible: EditorState.preferences.gpuPreferenceSupported
                text: qsTr("Run Drift on the high-performance graphics card")
                variant: "secondary"
                // Leaves the decode mode alone: this is the other way out, not a confirmation.
                onClicked: {
                    EditorState.preferences.preferredGpu = "discrete"
                    decodeGpuDialog.reject()
                }
            }

            ThemedLabel {
                width: parent.width
                visible: EditorState.preferences.gpuPreferenceInSystemSettings
                text: qsTr("Set Drift to High performance in Windows Settings > Display > Graphics, "
                           + "then restart Drift.")
            }

            ThemedButton {
                visible: EditorState.preferences.gpuPreferenceInSystemSettings
                text: qsTr("Open graphics settings")
                variant: "secondary"
                onClicked: {
                    Qt.openUrlExternally("ms-settings:display-advancedgraphics")
                    decodeGpuDialog.reject()
                }
            }
        }
    }
}
