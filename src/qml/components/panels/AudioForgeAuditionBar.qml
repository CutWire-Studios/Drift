pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Drift

// What the pedalboard is being heard on: the selected audio clip, a loop of its first seconds, and
// whether the board is valid.
Rectangle {
    id: root

    readonly property int problemCount: AudioForge.validation.length

    implicitHeight: column.implicitHeight + 2 * Theme.spacingMd
    color: Theme.c.forgePanel.headerBackground

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: Theme.c.forgePanel.borderWidth
        color: Theme.c.forgePanel.border
    }

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.margins: Theme.spacingMd
        spacing: Theme.spacingSm

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMd

            Text {
                Layout.fillWidth: true
                text: AudioForge.targetDescription
                elide: Text.ElideRight
                color: Theme.c.forgePanel.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
            }
            Text {
                text: root.problemCount === 0 ? qsTr("Valid") : qsTr("%n problem(s)", "", root.problemCount)
                color: root.problemCount === 0 ? Theme.c.forgePanel.success : Theme.c.forgePanel.error
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
        }

        Flow {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            ThemedButton {
                text: qsTr("Use selection")
                onClicked: AudioForge.useSelection()
            }
            ThemedButton {
                text: AudioForge.looping ? qsTr("Stop") : qsTr("Loop play")
                glyph: AudioForge.looping ? Theme.icons.pause : Theme.icons.play
                enabled: AudioForge.hasTarget
                onClicked: AudioForge.playLoop()
            }
            Row {
                spacing: Theme.spacingSm
                height: Theme.controlHeight

                ThemedSwitch {
                    anchors.verticalCenter: parent.verticalCenter
                    checked: AudioForge.followSelection
                    onToggled: AudioForge.followSelection = checked
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Follow selection")
                    color: Theme.c.forgePanel.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
            }
        }

        Repeater {
            model: AudioForge.validation

            Text {
                required property string modelData
                Layout.fillWidth: true
                text: modelData
                wrapMode: Text.WordWrap
                color: Theme.c.forgePanel.error
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
        }

        Text {
            Layout.fillWidth: true
            visible: !AudioForge.hasTarget
            text: qsTr("Pick an audio or video clip on the timeline to hear the pedalboard on it. Loop play repeats its first four seconds.")
            wrapMode: Text.WordWrap
            color: Theme.c.forgePanel.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }
}
