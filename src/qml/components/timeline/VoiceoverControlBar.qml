import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

// Dedicated voiceover recording control bar displayed on the right side of the timeline
// when recording is active. Provides live time readout, microphone device selection,
// input voice gain adjustment, live stereo VU meter, pause/resume, done and cancel controls.
Rectangle {
    id: root

    implicitHeight: 34
    implicitWidth: mainRow.implicitWidth + 20
    radius: Theme.radiusSm
    color: Qt.rgba(Theme.panelBackground.r, Theme.panelBackground.g, Theme.panelBackground.b, 0.96)
    border.color: EditorState.isAudioRecordingPaused ? "#eab308" : Theme.destructive
    border.width: 1

    // Drop shadow glow
    Rectangle {
        anchors.fill: parent
        anchors.margins: -1
        radius: root.radius + 1
        color: "transparent"
        border.color: EditorState.isAudioRecordingPaused ? Qt.rgba(0.9, 0.7, 0, 0.25) : Qt.rgba(0.9, 0.1, 0.1, 0.25)
        border.width: 1
        z: -1
    }

    Row {
        id: mainRow
        anchors.centerIn: parent
        spacing: 8

        // === 1. Status & Recording Timer ===
        Row {
            spacing: 6
            anchors.verticalCenter: parent.verticalCenter

            Rectangle {
                width: 8
                height: 8
                radius: 4
                color: EditorState.isAudioRecordingPaused ? "#eab308" : Theme.destructive
                anchors.verticalCenter: parent.verticalCenter

                SequentialAnimation on opacity {
                    running: root.visible && !EditorState.isAudioRecordingPaused
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.2; duration: 400 }
                    NumberAnimation { to: 1.0; duration: 400 }
                }
            }

            Text {
                text: EditorState.isAudioRecordingPaused ? qsTr("PAUSED") : qsTr("REC")
                font.bold: true
                font.pixelSize: 11
                color: EditorState.isAudioRecordingPaused ? "#eab308" : Theme.destructive
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                text: {
                    const secs = EditorState.audioRecordSeconds;
                    const m = Math.floor(secs / 60);
                    const s = Math.floor(secs % 60);
                    const ms = Math.floor((secs % 1) * 10);
                    return (m < 10 ? "0" + m : m) + ":" + (s < 10 ? "0" + s : s) + "." + ms;
                }
                font.family: Theme.monoFontFamily
                font.bold: true
                font.pixelSize: 11
                color: Theme.panelForeground
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        // Divider
        Rectangle {
            width: 1
            height: 18
            color: Theme.panelBorder
            anchors.verticalCenter: parent.verticalCenter
        }

        // === 2. Microphone Selector Dropdown ===
        Item {
            id: micPickerBtn
            width: Math.min(130, Math.max(70, micPickerRow.implicitWidth + 12))
            height: 24
            anchors.verticalCenter: parent.verticalCenter

            Rectangle {
                anchors.fill: parent
                radius: Theme.radiusSm
                color: micPickerMouse.containsMouse || micMenu.opened
                       ? Theme.popoverHover
                       : Qt.rgba(Theme.panelForeground.r, Theme.panelForeground.g, Theme.panelForeground.b, 0.05)
                border.color: Theme.panelBorder
                border.width: 1
            }

            Row {
                id: micPickerRow
                anchors.centerIn: parent
                spacing: 4

                IconGlyph {
                    glyph: Theme.icons.mic
                    iconSize: 12
                    iconColor: Theme.panelForeground
                    anchors.verticalCenter: parent.verticalCenter
                }

                Text {
                    text: EditorState.currentMicrophoneName || qsTr("Default Mic")
                    font.pixelSize: 11
                    color: Theme.panelForeground
                    elide: Text.ElideRight
                    width: Math.min(85, implicitWidth)
                    anchors.verticalCenter: parent.verticalCenter
                }

                IconGlyph {
                    glyph: Theme.icons.chevronDown
                    iconSize: 10
                    iconColor: Theme.mutedForeground
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            MouseArea {
                id: micPickerMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: micMenu.popup(0, micPickerBtn.height + 2)
            }

            ThemedContextMenu {
                id: micMenu
                implicitWidth: 220

                Instantiator {
                    model: EditorState.availableMicrophones
                    delegate: ThemedMenuItem {
                        required property var modelData
                        text: modelData.name
                        icon.name: modelData.name === EditorState.currentMicrophoneName ? Theme.icons.check : ""
                        onTriggered: EditorState.selectMicrophone(modelData.id)
                    }
                    onObjectAdded: (index, object) => micMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => micMenu.removeItem(object)
                }
            }

            ThemedToolTip {
                visible: micPickerMouse.containsMouse && !micMenu.opened
                text: qsTr("Input microphone: %1 (click to switch)").arg(EditorState.currentMicrophoneName)
            }
        }

        // Divider
        Rectangle {
            width: 1
            height: 18
            color: Theme.panelBorder
            anchors.verticalCenter: parent.verticalCenter
        }

        // === 3. Voice Input Gain (Volume) Slider ===
        Item {
            id: gainContainer
            width: gainRow.implicitWidth
            height: 24
            anchors.verticalCenter: parent.verticalCenter

            Row {
                id: gainRow
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4

                IconGlyph {
                    glyph: Theme.icons.volumeHigh
                    iconSize: 13
                    iconColor: Theme.mutedForeground
                    anchors.verticalCenter: parent.verticalCenter
                }

                ThemedSlider {
                    id: gainSlider
                    label: qsTr("Mic gain")
                    width: 65
                    anchors.verticalCenter: parent.verticalCenter
                    from: 0.0
                    to: 2.0
                    value: EditorState.audioRecordGain
                    onMoved: EditorState.setAudioRecordGain(value)
                    valueFormatter: function (v) {
                        return Math.round(v * 100) + "%"
                    }
                }

                Text {
                    text: Math.round(EditorState.audioRecordGain * 100) + "%"
                    font.family: Theme.monoFontFamily
                    font.pixelSize: 10
                    color: Theme.mutedForeground
                    width: 32
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            ThemedToolTip {
                text: qsTr("Voice input gain: %1% (adjust voice level)").arg(Math.round(EditorState.audioRecordGain * 100))
                visible: gainMouse.containsMouse
            }

            MouseArea {
                id: gainMouse
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.NoButton
            }
        }

        // Divider
        Rectangle {
            width: 1
            height: 18
            color: Theme.panelBorder
            anchors.verticalCenter: parent.verticalCenter
        }

        // === 4. Live VU Level Meter ===
        Item {
            width: 44
            height: 12
            anchors.verticalCenter: parent.verticalCenter

            Rectangle {
                anchors.fill: parent
                radius: 2
                color: Qt.rgba(0, 0, 0, 0.4)
                border.color: Theme.panelBorder
                border.width: 1
            }

            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.margins: 1
                width: Math.max(1, Math.min(parent.width - 2, (parent.width - 2) * EditorState.audioRecordLevel))
                radius: 1
                color: EditorState.audioRecordLevel > 0.85
                       ? Theme.destructive
                       : (EditorState.audioRecordLevel > 0.6 ? "#eab308" : "#22c55e")
            }

            ThemedToolTip {
                text: qsTr("Live voice level: %1%").arg(Math.round(EditorState.audioRecordLevel * 100))
                visible: vuMouse.containsMouse
            }

            MouseArea {
                id: vuMouse
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.NoButton
            }
        }

        // Divider
        Rectangle {
            width: 1
            height: 18
            color: Theme.panelBorder
            anchors.verticalCenter: parent.verticalCenter
        }

        // === 5. Transport Controls: Pause/Resume, Done, Cancel ===
        Row {
            spacing: 2
            anchors.verticalCenter: parent.verticalCenter

            IconButton {
                glyph: EditorState.isAudioRecordingPaused ? Theme.icons.play : Theme.icons.pause
                variant: "text"
                tooltip: EditorState.isAudioRecordingPaused ? qsTr("Resume recording") : qsTr("Pause recording")
                active: EditorState.isAudioRecordingPaused
                onClicked: EditorState.toggleAudioRecordingPause()
            }

            IconButton {
                glyph: Theme.icons.check
                variant: "text"
                tooltip: qsTr("Done — finish recording and save to track")
                onClicked: EditorState.stopAudioRecording()
            }

            IconButton {
                glyph: Theme.icons.trash
                variant: "text"
                tooltip: qsTr("Cancel — discard recording")
                onClicked: EditorState.cancelAudioRecording()
            }
        }
    }
}
