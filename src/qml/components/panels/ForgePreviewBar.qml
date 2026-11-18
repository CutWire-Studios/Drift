pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Drift

// What the live preview is aimed at and how to drive it: the target (selected clip, cut or the
// built-in sample), looping over the effect window, animation direction and length, and whether
// the graph currently compiles.
Rectangle {
    id: root

    readonly property int errorCount: Forge.compileErrors.length
    readonly property string hint: {
        switch (Forge.kind) {
        case "transition": return qsTr("Plays across the cut; Loop repeats the transition window.")
        case "animation": return qsTr("Loop repeats the in or out window of the clip.")
        }
        return qsTr("Shown on the target clip with the current slider values.")
    }

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
                text: Forge.previewTarget.node !== undefined
                      ? qsTr("Previewing a single node") : Forge.targetDescription
                elide: Text.ElideRight
                color: Theme.c.forgePanel.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
            }
            Text {
                text: root.errorCount === 0 ? qsTr("Compiles") : qsTr("%n error(s)", "", root.errorCount)
                color: root.errorCount === 0 ? Theme.c.forgePanel.success : Theme.c.forgePanel.error
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
        }

        Flow {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            ThemedButton {
                text: qsTr("Use selection")
                onClicked: Forge.useSelection()
            }
            ThemedButton {
                text: Forge.looping ? qsTr("Stop") : qsTr("Loop play")
                glyph: Forge.looping ? Theme.icons.pause : Theme.icons.play
                onClicked: Forge.playLoop()
            }
            ThemedButton {
                visible: Forge.previewTarget.node !== undefined
                text: qsTr("Preview output")
                onClicked: Forge.previewOutput()
            }
            Row {
                spacing: Theme.spacingSm
                height: Theme.controlHeight

                ThemedSwitch {
                    anchors.verticalCenter: parent.verticalCenter
                    checked: Forge.followSelection
                    onToggled: Forge.followSelection = checked
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

        RowLayout {
            visible: Forge.kind === "animation"
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            ThemedChip {
                text: qsTr("In")
                variant: "outline"
                selected: Forge.animDirection === "in"
                onClicked: Forge.animDirection = "in"
            }
            ThemedChip {
                text: qsTr("Out")
                variant: "outline"
                selected: Forge.animDirection === "out"
                onClicked: Forge.animDirection = "out"
            }
            Item { Layout.fillWidth: true }
            ThemedNumberField {
                Layout.preferredWidth: 96
                from: 50
                to: 10000
                step: 50
                decimals: 0
                unit: qsTr("ms")
                value: Forge.animDurationMs
                onEdited: (v) => Forge.animDurationMs = Math.round(v)
            }
        }

        Text {
            Layout.fillWidth: true
            text: root.hint
            wrapMode: Text.WordWrap
            color: Theme.c.forgePanel.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }
}
