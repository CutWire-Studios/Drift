import QtQuick
import QtQuick.Controls.Basic
import Drift

Rectangle {
    property var editor
    property Item headerItem: null
    property Item cueList: null
    property alias cueText: cueText
    property alias startField: startField
    property alias endField: endField
    id: root
    anchors.bottom: parent.bottom
    anchors.left: parent.left
    anchors.right: parent.right
    readonly property real naturalHeight: panelCol.implicitHeight + 24
    readonly property real minListHeight: 80
    readonly property real minEditorHeight: 72
    readonly property real headerBlock: headerItem.height + headerItem.anchors.topMargin
                                       + cueList.anchors.topMargin
    height: Math.min(naturalHeight,
                     Math.max(minEditorHeight,
                              parent.height - headerBlock - minListHeight))
    color: Theme.c.subtitleEditorPanel.background

    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: Theme.c.subtitleEditorPanel.ruleWidth
        color: Theme.c.subtitleEditorPanel.border
    }

    Flickable {
        id: editorFlick
        anchors.fill: parent
        anchors.margins: 12
        contentWidth: width
        contentHeight: panelCol.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentHeight > height
        ScrollBar.vertical: AppScrollBar {
            policy: editorFlick.contentHeight > editorFlick.height
                    ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
        }

        Column {
            id: panelCol
            width: parent.width
            spacing: 8

            // Editor for the selected cue.
            Column {
                width: parent.width
                spacing: 8
                visible: editor.selectedCue !== null && !editor.compactEdit

                Row {
                    width: parent.width
                    spacing: 6

                    ThemedTextArea {
                        id: cueText
                        width: parent.width - applyButton.width - parent.spacing
                        height: 56
                        placeholderText: qsTr("Type subtitle…")
                        onEditingFinished: {
                            if (editor.selectedCueIndex >= 0)
                                editor.updateCue(editor.selectedCueIndex, { text: text })
                        }

                        // Enter ends this subtitle and starts the next one with whatever sits
                        // after the cursor — typing through a transcript is one subtitle after
                        // another, not one long block. Shift+Enter is the line break within a
                        // subtitle, which is what Enter used to do.
                        Keys.onPressed: (event) => {
                            if (event.key !== Qt.Key_Return && event.key !== Qt.Key_Enter)
                                return
                            if (event.modifiers & Qt.ShiftModifier)
                                return
                            event.accepted = true
                            if (editor.selectedCueIndex < 0)
                                return
                            editor.splitCueAtCursor(editor.selectedCueIndex,
                                                    cueText.cursorPosition, cueText.text)
                            // The panel reloads the field with the new cue's text; the caret
                            // belongs at its start, ready for the next line.
                            cueText.cursorPosition = 0
                        }
                    }

                    // Explicit "apply text" affordance (separate from adding a cue).
                    Rectangle {
                        id: applyButton
                        width: 40
                        height: 56
                        radius: Theme.c.subtitleEditorPanel.radius
                        readonly property bool dirty: editor.selectedCue !== null
                                                      && cueText.text !== (editor.selectedCue ? editor.selectedCue.text : "")
                        color: applyMouse.containsMouse ? Qt.lighter(Theme.c.subtitleEditorPanel.applyBackgroundHover, 1.08) : Theme.c.subtitleEditorPanel.applyBackground
                        opacity: (dirty || applyMouse.containsMouse) ? 1 : 0.5

                        // Was a raw "✓" glyph while every other button in the app
                        // uses IconGlyph + Theme.icons.
                        IconGlyph {
                            anchors.centerIn: parent
                            glyph: Theme.icons.check
                            iconSize: Theme.iconSizeBase
                            iconColor: Theme.c.subtitleEditorPanel.iconOnAccent
                        }

                        MouseArea {
                            id: applyMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (editor.selectedCueIndex >= 0)
                                    editor.updateCue(editor.selectedCueIndex, { text: cueText.text })
                            }
                        }

                        ThemedToolTip {
                            visible: applyMouse.containsMouse
                            text: qsTr("Apply text to this subtitle")
                        }
                    }
                }

                Row {
                    width: parent.width
                    spacing: 8

                    // Start
                    Column {
                        width: (parent.width - parent.spacing) / 2
                        spacing: 4
                        Text {
                            text: qsTr("Start")
                            color: Theme.c.subtitleEditorPanel.foregroundMuted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs
                        }
                        Row {
                            width: parent.width
                            spacing: 4
                            ThemedTextField {
                                id: startField
                                width: parent.width - startPh.width - parent.spacing
                                onEditingFinished: {
                                    const v = editor.parseCueTime(text)
                                    if (!isNaN(v) && editor.selectedCueIndex >= 0)
                                        editor.updateCue(editor.selectedCueIndex, { start: v })
                                }
                            }
                            IconButton {
                                id: startPh
                                glyph: Theme.icons.setStart
                                variant: "ghost"
                                enabled: editor.localPlayhead >= 0
                                tooltip: qsTr("Set start to current time")
                                onClicked: editor.setCueEdgeToPlayhead(editor.selectedCueIndex, "start")
                            }
                        }
                    }

                    // End
                    Column {
                        width: (parent.width - parent.spacing) / 2
                        spacing: 4
                        Text {
                            text: qsTr("End")
                            color: Theme.c.subtitleEditorPanel.foregroundMuted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs
                        }
                        Row {
                            width: parent.width
                            spacing: 4
                            ThemedTextField {
                                id: endField
                                width: parent.width - endPh.width - parent.spacing
                                onEditingFinished: {
                                    const v = editor.parseCueTime(text)
                                    if (!isNaN(v) && editor.selectedCueIndex >= 0)
                                        editor.updateCue(editor.selectedCueIndex, { end: v })
                                }
                            }
                            IconButton {
                                id: endPh
                                glyph: Theme.icons.setEnd
                                variant: "ghost"
                                enabled: editor.localPlayhead >= 0
                                tooltip: qsTr("Set end to current time")
                                onClicked: editor.setCueEdgeToPlayhead(editor.selectedCueIndex, "end")
                            }
                        }
                    }
                }

                ThemedButton {
                    text: qsTr("Delete caption")
                    variant: "destructive"
                    glyph: Theme.icons.trash
                    onClicked: {
                        if (editor.selectedCueIndex >= 0)
                            editor.removeCue(editor.selectedCueIndex)
                    }
                }

                Rectangle {
                    width: parent.width
                    height: Theme.c.subtitleEditorPanel.ruleWidth
                    color: Theme.c.subtitleEditorPanel.border
                    opacity: Theme.c.subtitleEditorPanel.ruleOpacity
                }
            }

            Text {
                width: parent.width
                text: editor.localPlayhead >= 0
                      ? qsTr("At %1").arg(editor.formatCueTime(editor.localPlayhead))
                      : qsTr("Move to a time inside this clip to add a subtitle")
                color: Theme.c.subtitleEditorPanel.foregroundMuted
                font.family: Theme.monoFontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            ThemedButton {
                width: parent.width
                variant: "primary"
                glyph: Theme.icons.plus
                enabled: editor.localPlayhead >= 0
                text: qsTr("Add subtitle at current time")
                onClicked: editor.addCueAtPlayhead()
            }
        }
    }
}
