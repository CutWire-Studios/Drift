pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Drift

// The GLSL the graph compiles to, one block per pass, and anything that went wrong compiling it.
Item {
    id: root

    Flickable {
        anchors.fill: parent
        contentHeight: column.implicitHeight + 2 * Theme.spacingMd
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar { }

        ColumnLayout {
            id: column
            x: Theme.spacingMd
            y: Theme.spacingMd
            width: parent.width - 2 * Theme.spacingMd
            spacing: Theme.spacingSm

            Repeater {
                model: Forge.compileErrors

                Text {
                    required property var modelData
                    Layout.fillWidth: true
                    text: modelData.message
                    wrapMode: Text.WordWrap
                    color: Theme.c.forgePanel.error
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
            }

            Repeater {
                model: Forge.passes

                ColumnLayout {
                    id: pass
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: Theme.spacingXs

                    RowLayout {
                        Layout.fillWidth: true

                        Text {
                            Layout.fillWidth: true
                            text: pass.modelData.file
                            color: Theme.c.forgePanel.foregroundMuted
                            font.family: Theme.monoFontFamily
                            font.pixelSize: Theme.fontSizeXs
                        }
                        ThemedButton {
                            variant: "ghost"
                            glyph: Theme.icons.copy
                            text: qsTr("Copy")
                            onClicked: {
                                code.selectAll()
                                code.copy()
                                code.deselect()
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: code.implicitHeight + 2 * Theme.spacingMd
                        radius: Theme.c.forgePanel.borderWidth + 4
                        color: Theme.c.forgePanel.codeBackground

                        TextEdit {
                            id: code
                            x: Theme.spacingMd
                            y: Theme.spacingMd
                            width: parent.width - 2 * Theme.spacingMd
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextEdit.NoWrap
                            textFormat: TextEdit.PlainText
                            text: pass.modelData.source
                            color: Theme.c.forgePanel.foreground
                            font.family: Theme.monoFontFamily
                            font.pixelSize: Theme.fontSizeXs
                        }
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                visible: Forge.passes.length === 0
                text: qsTr("Nothing compiled yet.")
                color: Theme.c.forgePanel.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
            }
        }
    }
}
