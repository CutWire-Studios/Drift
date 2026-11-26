pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Drift

// Scripted text: a Lua script that returns the words for every frame (timecode burn-in, counters,
// tickers, clocks). The script runs in the same sandbox as procedural scenes; what it says at the
// playhead, or the Lua error, is shown under the editor. The text style below applies to the result.
CollapsibleSection {
    id: root
    property var inspector

    readonly property var presets: EditorState.textScriptPresets()
    readonly property bool scripted: inspector.hasSelection && inspector.clipKind === "text"
                                     && (inspector.clipData.textScript || "").length > 0
    property var status: ({})
    property string loadedClipId: ""

    title: qsTr("Script")
    tooltip: qsTr("Make this text the result of a Lua script, evaluated for every frame.")
    collapsible: false
    showSeparator: false
    showSwitch: true
    switchChecked: scripted
    switchTooltip: qsTr("Generate the text with a script instead of typing it")

    SearchAnchor {
        searchId: "text-script"
        title: qsTr("Scripted text")
        keywords: "lua script timecode counter ticker clock stopwatch frame number burn-in"
    }

    function refresh() {
        if (!inspector.hasSelection || inspector.clipKind !== "text") {
            status = ({})
            return
        }
        const wanted = inspector.clipData.textScript || ""
        if (loadedClipId !== inspector.clipData.id || (!editor.activeFocus && editor.text !== wanted)) {
            loadedClipId = inspector.clipData.id
            editor.text = wanted
        }
        status = scripted ? EditorState.textScriptStatus(EditorState.selectedTrack, EditorState.selectedClip) : ({})
    }

    function commit() {
        commitTimer.stop()
        if (inspector.hasSelection && inspector.clipKind === "text" && inspector.clipData.id === loadedClipId)
            EditorState.setTextScript(EditorState.selectedTrack, EditorState.selectedClip, editor.text)
    }

    onSwitchToggled: checked => {
        if (checked) {
            editor.text = presets.length > 0 ? presets[0].script : "return \"\""
        } else {
            editor.text = ""
        }
        commit()
    }

    Connections {
        target: root.inspector
        function onClipDataChanged() { root.refresh() }
    }
    Connections {
        target: EditorState
        function onPlayheadSecondsChanged() { if (root.scripted) root.refresh() }
    }
    Component.onCompleted: refresh()
    Component.onDestruction: if (commitTimer.running) commit()

    Timer {
        id: commitTimer
        interval: 500
        onTriggered: root.commit()
    }

    Column {
        width: parent.width
        spacing: Theme.spacingSm

        ThemedComboBox {
            width: parent.width
            model: root.presets.map(p => p.label)
            currentIndex: -1
            displayText: qsTr("Start from a preset…")
            tooltip: qsTr("Replace the script with a ready-made one")
            onActivated: index => {
                editor.text = root.presets[index].script
                root.commit()
            }
        }

        Rectangle {
            width: parent.width
            height: 150
            color: Theme.c.proceduralEditorPanel.background
            radius: Theme.radiusSm
            clip: true

            Flickable {
                id: flick
                anchors.fill: parent
                contentWidth: editor.contentWidth + editor.leftPadding + editor.rightPadding
                contentHeight: editor.contentHeight + editor.topPadding + editor.bottomPadding
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: AppScrollBar { }
                ScrollBar.horizontal: AppScrollBar { }

                TextArea.flickable: TextArea {
                    id: editor
                    wrapMode: TextArea.NoWrap
                    color: Theme.c.proceduralEditorPanel.foreground
                    selectionColor: Theme.c.proceduralEditorPanel.selection
                    selectedTextColor: Theme.c.proceduralEditorPanel.selectionForeground
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.fontSizeSm
                    selectByMouse: true
                    persistentSelection: true
                    background: null
                    onTextChanged: if (activeFocus) commitTimer.restart()
                    onActiveFocusChanged: if (!activeFocus && commitTimer.running) root.commit()
                }
            }

            ProceduralHighlighter {
                textDocument: editor.textDocument
                language: "lua"
                keywordColor: Theme.c.proceduralEditorPanel.syntaxKeyword
                typeColor: Theme.c.proceduralEditorPanel.syntaxType
                builtinColor: Theme.c.proceduralEditorPanel.syntaxBuiltin
                stringColor: Theme.c.proceduralEditorPanel.syntaxString
                commentColor: Theme.c.proceduralEditorPanel.syntaxComment
                numberColor: Theme.c.proceduralEditorPanel.syntaxNumber
            }
        }

        Row {
            width: parent.width
            spacing: Theme.spacingXs
            visible: !!root.status.error && root.status.error.length > 0
            IconGlyph {
                glyph: Theme.icons.warning
                iconSize: 12
                iconColor: Theme.c.proceduralInspector.iconError
                anchors.top: parent.top
                anchors.topMargin: 2
            }
            ThemedLabel {
                width: parent.width - 12 - parent.spacing
                wrapMode: Text.Wrap
                color: Theme.c.proceduralInspector.foregroundError
                text: root.status.error || ""
            }
        }

        ThemedLabel {
            width: parent.width
            wrapMode: Text.Wrap
            opacity: 0.8
            visible: !!root.status.text && root.status.text.length > 0 && !(root.status.error && root.status.error.length > 0)
            text: qsTr("Now: %1").arg(root.status.text || "")
        }

        ThemedLabel {
            width: parent.width
            wrapMode: Text.Wrap
            opacity: 0.6
            text: qsTr("Return a string. Read ctx.time, ctx.timeline, ctx.progress, ctx.frame, ctx.fps, ctx.source and ctx.now; helpers: timecode, lerp, ease, pad, commas.")
        }
    }
}
