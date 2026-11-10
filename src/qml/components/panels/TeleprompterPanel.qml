import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import Drift

PanelFrame {
    id: root

    property bool reading: EditorState.teleprompter.playing

    component SliderField: RowLayout {
        id: field
        property string label
        property alias slider: slider
        property string readout
        spacing: Theme.spacingMd

        Text {
            text: field.label
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            color: Theme.c.teleprompterPanel.foregroundMuted
        }
        ThemedSlider {
            id: slider
            Layout.preferredWidth: 100
            label: field.label
        }
        Text {
            Layout.preferredWidth: 36
            text: field.readout
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.fontSizeXs
            color: Theme.c.teleprompterPanel.foregroundMuted
        }
    }

    Connections {
        target: EditorState.teleprompter
        function onPlayingChanged() {
            if (EditorState.teleprompter.playing)
                root.reading = true
        }
    }

    AssetsPanelHeader {
        id: header
        width: parent.width
        height: Theme.panelHeaderHeight
        title: qsTr("Teleprompter")
    }

    Flow {
        id: toolbar
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingMd
        spacing: Theme.spacingMd

        IconButton {
            glyph: root.reading ? Theme.icons.pencil : Theme.icons.bookOpen
            tooltip: root.reading ? qsTr("Edit script") : qsTr("Read mode")
            onClicked: {
                if (root.reading)
                    EditorState.teleprompter.playing = false
                root.reading = !root.reading
            }
        }
        IconButton {
            glyph: EditorState.teleprompter.playing ? Theme.icons.pause : Theme.icons.play
            tooltip: EditorState.teleprompter.playing ? qsTr("Pause scrolling") : qsTr("Start scrolling")
            active: EditorState.teleprompter.playing
            enabled: EditorState.teleprompter.script.length > 0
            onClicked: {
                root.reading = true
                EditorState.teleprompter.toggle()
            }
        }
        IconButton {
            glyph: Theme.icons.reset
            tooltip: qsTr("Back to top")
            onClicked: EditorState.teleprompter.reset()
        }
        SliderField {
            label: qsTr("Speed")
            readout: EditorState.teleprompter.speed.toFixed(1)
            slider.from: 0.1
            slider.to: 5
            slider.stepSize: 0.1
            slider.valueFormatter: (v) => Number(v).toFixed(1) + " " + qsTr("lines/s")
            slider.value: EditorState.teleprompter.speed
            slider.onMoved: EditorState.teleprompter.speed = slider.value
        }
        SliderField {
            label: qsTr("Size")
            readout: EditorState.teleprompter.fontSize
            slider.from: 12
            slider.to: 96
            slider.stepSize: 1
            slider.valueFormatter: (v) => Math.round(v) + " px"
            slider.value: EditorState.teleprompter.fontSize
            slider.onMoved: EditorState.teleprompter.fontSize = Math.round(slider.value)
        }
        ThemedCheckBox {
            text: qsTr("Follow recording")
            tooltip: qsTr("Scrolling starts, pauses and stops with recording")
            checked: EditorState.teleprompter.followRecording
            onToggled: EditorState.teleprompter.followRecording = checked
        }
        ThemedCheckBox {
            text: qsTr("Show over preview")
            checked: EditorState.teleprompter.overlayEnabled
            onToggled: EditorState.teleprompter.overlayEnabled = checked
        }
    }

    Item {
        anchors.top: toolbar.bottom
        anchors.topMargin: Theme.spacingMd
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right

        Flickable {
            id: editFlick
            anchors.fill: parent
            anchors.margins: Theme.spacingMd
            visible: !root.reading
            clip: true
            contentWidth: width
            contentHeight: scriptEdit.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar { policy: ScrollBar.AsNeeded }

            ThemedTextArea {
                id: scriptEdit
                width: editFlick.width
                height: Math.max(implicitHeight, editFlick.height)
                placeholderText: qsTr("Paste or type your script")
                text: EditorState.teleprompter.script
                onTextChanged: {
                    if (text !== EditorState.teleprompter.script)
                        EditorState.teleprompter.script = text
                }
            }
        }

        TeleprompterView {
            anchors.fill: parent
            visible: root.reading
        }
    }
}
