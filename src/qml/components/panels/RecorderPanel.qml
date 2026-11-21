import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import Drift

PanelFrame {
    id: root
    SearchAnchor {
        searchId: "recorder-panel"
        title: qsTr("Recorder")
        keywords: "record voiceover microphone narration audio teleprompter noise"
    }

    readonly property bool recording: EditorState.isRecordingAudio
    readonly property bool paused: EditorState.isAudioRecordingPaused
    readonly property bool compact: body.height < 260

    AssetsPanelHeader {
        id: header
        width: parent.width
        height: Theme.panelHeaderHeight
        title: qsTr("Recorder")
    }

    Flickable {
        id: body
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        clip: true
        contentWidth: width
        contentHeight: Math.max(height, grid.implicitHeight + Theme.spacing2xl * 2)
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar { policy: ScrollBar.AsNeeded }

        GridLayout {
            id: grid
            readonly property real pad: Theme.spacing2xl
            x: (body.width - width) / 2
            y: Math.max(pad, (body.contentHeight - implicitHeight) / 2)
            width: root.compact ? body.width - pad * 2 : Math.min(420, body.width - pad * 2)
            columns: root.compact ? 3 : 1
            rowSpacing: Theme.spacing2xl
            columnSpacing: Theme.spacing3xl

            Item {
                id: statusBlock
                Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
                Layout.preferredWidth: statusCol.implicitWidth
                Layout.preferredHeight: statusCol.implicitHeight

                Column {
                    id: statusCol
                    spacing: Theme.spacingMd

                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: Theme.spacingMd

                        Rectangle {
                            id: dot
                            width: 10
                            height: 10
                            radius: 5
                            anchors.verticalCenter: parent.verticalCenter
                            color: !root.recording ? Theme.panelMuted
                                   : (root.paused ? Theme.c.voiceoverRecDot.paused : Theme.c.voiceoverRecDot.recording)

                            SequentialAnimation on opacity {
                                running: root.recording && !root.paused && root.visible
                                loops: Animation.Infinite
                                NumberAnimation { to: 0.2; duration: 400 }
                                NumberAnimation { to: 1.0; duration: 400 }
                            }
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: !root.recording ? qsTr("READY") : (root.paused ? qsTr("PAUSED") : qsTr("REC"))
                            font.family: Theme.fontFamily
                            font.bold: true
                            font.pixelSize: Theme.fontSizeBase
                            color: !root.recording ? Theme.panelMuted
                                   : (root.paused ? Theme.c.voiceoverRecLabel.paused : Theme.c.voiceoverRecLabel.recording)
                        }
                    }

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: {
                            const secs = root.recording ? EditorState.audioRecordSeconds : 0;
                            const m = Math.floor(secs / 60);
                            const s = Math.floor(secs % 60);
                            const t = Math.floor((secs % 1) * 10);
                            return (m < 10 ? "0" + m : m) + ":" + (s < 10 ? "0" + s : s) + "." + t;
                        }
                        font.family: Theme.monoFontFamily
                        font.bold: true
                        font.pixelSize: root.compact ? 32 : 56
                        color: root.recording ? Theme.panelForeground : Theme.panelMuted
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                spacing: Theme.spacingLg

                Item {
                    id: meter
                    Layout.fillWidth: true
                    Layout.preferredHeight: 16
                    opacity: root.recording ? 1.0 : 0.4

                    VoiceoverLevelMeter {
                        width: meter.width
                        height: meter.height
                    }
                }

                Item {
                    id: micPicker
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32

                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.c.voiceoverMicPicker.radius
                        color: micMouse.containsMouse || micMenu.opened
                               ? Theme.c.voiceoverMicPicker.backgroundHover
                               : Qt.rgba(Theme.c.voiceoverMicPicker.background.r, Theme.c.voiceoverMicPicker.background.g, Theme.c.voiceoverMicPicker.background.b, 0.05)
                        border.color: Theme.c.voiceoverMicPicker.border
                        border.width: Theme.c.voiceoverMicPicker.borderWidth
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacingLg
                        anchors.rightMargin: Theme.spacingLg
                        spacing: Theme.spacingMd

                        IconGlyph {
                            glyph: Theme.icons.mic
                            iconSize: 14
                            iconColor: Theme.c.voiceoverMicPicker.icon
                        }
                        Text {
                            Layout.fillWidth: true
                            text: EditorState.currentMicrophoneName || qsTr("Default Mic")
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeSm
                            color: Theme.c.voiceoverMicPicker.foreground
                            elide: Text.ElideRight
                        }
                        IconGlyph {
                            glyph: Theme.icons.chevronDown
                            iconSize: 12
                            iconColor: Theme.c.voiceoverMicPicker.iconMuted
                        }
                    }

                    MouseArea {
                        id: micMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: micMenu.popup(0, micPicker.height + 2)
                    }

                    ThemedContextMenu {
                        id: micMenu
                        implicitWidth: Math.max(220, micPicker.width)

                        Instantiator {
                            model: EditorState.availableMicrophones
                            delegate: VoiceoverMicMenuItem { }
                            onObjectAdded: (index, object) => micMenu.insertItem(index, object)
                            onObjectRemoved: (index, object) => micMenu.removeItem(object)
                        }
                    }

                    ThemedToolTip {
                        visible: micMouse.containsMouse && !micMenu.opened
                        text: qsTr("Input microphone: %1 (click to switch)").arg(EditorState.currentMicrophoneName)
                    }
                }

                Item {
                    id: gainBox
                    Layout.fillWidth: true
                    Layout.preferredHeight: 28

                    VoiceoverGainControl {
                        width: gainBox.width
                        height: gainBox.height
                        sliderWidth: Math.max(60, gainBox.width - 13 - 32 - 8)
                    }
                }
            }

            RowLayout {
                Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
                spacing: Theme.spacing2xl

                IconButton {
                    visible: root.recording
                    glyph: root.paused ? Theme.icons.play : Theme.icons.pause
                    tooltip: root.paused ? qsTr("Resume recording") : qsTr("Pause recording")
                    active: root.paused
                    buttonSize: 44
                    iconSize: 20
                    onClicked: EditorState.toggleAudioRecordingPause()
                }

                Rectangle {
                    id: bigBtn
                    Layout.preferredWidth: root.compact ? 56 : 76
                    Layout.preferredHeight: Layout.preferredWidth
                    radius: width / 2
                    color: Theme.destructive
                    opacity: bigMouse.pressed ? 0.8 : (bigMouse.containsMouse ? 0.92 : 1.0)
                    scale: bigMouse.pressed ? Theme.pressScale : 1.0
                    border.width: 3
                    border.color: Qt.rgba(1, 1, 1, 0.25)

                    Behavior on scale {
                        NumberAnimation { duration: Theme.durationPress; easing.type: Theme.easing }
                    }

                    IconGlyph {
                        anchors.centerIn: parent
                        glyph: root.recording ? Theme.icons.square : Theme.icons.mic
                        iconSize: bigBtn.width * 0.4
                        iconColor: "white"
                    }

                    MouseArea {
                        id: bigMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.recording ? EditorState.stopAudioRecording() : EditorState.startAudioRecording()
                    }

                    ThemedToolTip {
                        visible: bigMouse.containsMouse
                        text: root.recording ? qsTr("Stop - finish recording and save to track") : qsTr("Start recording")
                    }

                    Accessible.role: Accessible.Button
                    Accessible.name: root.recording ? qsTr("Stop recording") : qsTr("Start recording")
                }

                IconButton {
                    visible: root.recording
                    glyph: Theme.icons.trash
                    tooltip: qsTr("Cancel - discard recording")
                    buttonSize: 44
                    iconSize: 20
                    onClicked: EditorState.cancelAudioRecording()
                }
            }
        }
    }
}
