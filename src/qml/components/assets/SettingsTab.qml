import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import QtQuick.Dialogs
import Drift
import ".."

// Settings tab: project canvas size, preview guides and background fill.
Item {
    id: root

    // The whole settings page scrolls, so short panel heights no
    // longer push the keybindings list out of reach.
    Flickable {
        anchors.fill: parent
        contentHeight: settingsColumn.height + Theme.spacing3xl
        clip: true
        // Bumped by ThemedSlider while a handle is dragged, so the page
        // doesn't steal the drag.
        property int dragLocks: 0
        interactive: dragLocks === 0
        ScrollBar.vertical: AppScrollBar { }

        Column {
            id: settingsColumn
            x: Theme.pagePadding
            width: parent.width - Theme.pagePadding * 2
            spacing: Theme.spacingLg
            topPadding: Theme.pagePadding

            // Live project size, re-read whenever the timeline changes so
            // undo/redo of a crop is reflected back into these fields.
            property int canvasW: { void EditorState.tracks; return EditorState.projectWidth() }
            property int canvasH: { void EditorState.tracks; return EditorState.projectHeight() }

            readonly property var canvasPresets: [
                { label: qsTr("Custom"), w: 0, h: 0 },
                { label: "1920×1080 (16:9)", w: 1920, h: 1080 },
                { label: "3840×2160 (4K)", w: 3840, h: 2160 },
                { label: "1080×1920 (9:16)", w: 1080, h: 1920 },
                { label: "1080×1080 (1:1)", w: 1080, h: 1080 },
                { label: "1440×1080 (4:3)", w: 1440, h: 1080 }
            ]

            Text {
                text: qsTr("Video size")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            ThemedButton {
                width: parent.width
                variant: "secondary"
                glyph: Theme.icons.ratio
                text: qsTr("Choose layout…")
                tooltip: qsTr("Pick a platform template (YouTube, Instagram, TikTok, …) and quality")
                enabled: !EditorState.canvasCropMode
                onClicked: {
                    if (typeof Window !== "undefined" && Window.window && Window.window.openLayoutChooser)
                        Window.window.openLayoutChooser()
                }
            }

            ThemedComboBox {
                width: parent.width
                enabled: !EditorState.canvasCropMode
                model: settingsColumn.canvasPresets.map(function (p) { return p.label })
                tooltip: qsTr("Change the video size. Clips keep their current size and position.")
                currentIndex: {
                    const presets = settingsColumn.canvasPresets
                    for (var i = 1; i < presets.length; ++i) {
                        if (presets[i].w === settingsColumn.canvasW
                                && presets[i].h === settingsColumn.canvasH)
                            return i
                    }
                    return 0
                }
                onActivated: {
                    const preset = settingsColumn.canvasPresets[currentIndex]
                    if (preset.w > 0)
                        EditorState.setProjectResolution(preset.w, preset.h)
                }
            }

            Row {
                width: parent.width
                spacing: Theme.spacingLg

                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: Theme.spacingSm
                    ThemedLabel { text: qsTr("Width") }
                    ThemedNumberField {
                        width: parent.width
                        enabled: !EditorState.canvasCropMode
                        from: 16
                        to: 7680
                        step: 2
                        unit: "px"
                        value: settingsColumn.canvasW
                        onEdited: v => EditorState.setProjectResolution(v, settingsColumn.canvasH)
                    }
                }

                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: Theme.spacingSm
                    ThemedLabel { text: qsTr("Height") }
                    ThemedNumberField {
                        width: parent.width
                        enabled: !EditorState.canvasCropMode
                        from: 16
                        to: 4320
                        step: 2
                        unit: "px"
                        value: settingsColumn.canvasH
                        onEdited: v => EditorState.setProjectResolution(settingsColumn.canvasW, v)
                    }
                }
            }

            ThemedButton {
                width: parent.width
                variant: EditorState.canvasCropMode ? "primary" : "secondary"
                glyph: Theme.icons.crop
                text: EditorState.canvasCropMode ? qsTr("Cancel crop") : qsTr("Crop video size")
                tooltip: qsTr("Drag the preview edges to change what’s included")
                onClicked: EditorState.canvasCropMode = !EditorState.canvasCropMode
            }

            Text {
                text: qsTr("Changing size doesn’t shrink your clips — anything outside the new edges is cut off.")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                opacity: 0.7
                width: settingsColumn.width
                wrapMode: Text.WordWrap
            }

            Text {
                text: qsTr("Preview guides")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                topPadding: Theme.spacingMd
            }

            Row {
                spacing: Theme.spacingLg
                ThemedCheckBox {
                    anchors.verticalCenter: parent.verticalCenter
                    checked: EditorState.guidesEnabled
                    text: qsTr("Enabled")
                    tooltip: qsTr("Show alignment guides over the preview")
                    onToggled: EditorState.guidesEnabled = checked
                }
                ThemedComboBox {
                    anchors.verticalCenter: parent.verticalCenter
                    textRole: "label"
                    valueRole: "id"
                    model: [
                        { id: "thirds", label: qsTr("Rule of thirds") },
                        { id: "crosshair", label: qsTr("Center cross") },
                        { id: "safe", label: qsTr("Safe margins") }
                    ]
                    enabled: EditorState.guidesEnabled
                    tooltip: qsTr("Which guide to show")
                    currentIndex: {
                        for (var i = 0; i < model.length; ++i) {
                            if (model[i].id === EditorState.guideType)
                                return i
                        }
                        return 0
                    }
                    onActivated: EditorState.guideType = model[currentIndex].id
                }
            }

            Text {
                text: qsTr("Background")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                topPadding: Theme.spacingMd
            }

            ThemedComboBox {
                id: bgKindCombo
                textRole: "label"
                valueRole: "id"
                model: [
                    { id: "color", label: qsTr("Solid color") },
                    { id: "blur", label: qsTr("Blur") }
                ]
                tooltip: qsTr("Fill behind clips that don’t cover the whole screen")
                currentIndex: {
                    for (var i = 0; i < model.length; ++i) {
                        if (model[i].id === EditorState.background.kind)
                            return i
                    }
                    return 0
                }
                onActivated: EditorState.setBackground({ kind: model[currentIndex].id })
            }


            Row {
                spacing: Theme.spacingMd
                visible: EditorState.background.kind === "color"

                Rectangle {
                    width: Theme.spacing3xl
                    height: Theme.spacing3xl
                    radius: Theme.radiusSm
                    anchors.verticalCenter: parent.verticalCenter
                    color: EditorState.background.color || "#ff000000"
                    border.width: swatchMouse.containsMouse ? Theme.borderWidthFocus : Theme.borderWidth
                    border.color: swatchMouse.containsMouse ? Theme.primary : Theme.panelBorder

                    Behavior on border.color {
                        ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                    }

                    ThemedToolTip {
                        text: qsTr("Choose background colour")
                        visible: swatchMouse.containsMouse
                    }

                    MouseArea {
                        id: swatchMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            canvasColorDialog.selectedColor = EditorState.background.color || "#ff000000"
                            canvasColorDialog.open()
                        }
                    }
                }

                ThemedTextField {
                    id: canvasHexField
                    width: 92
                    text: EditorState.background.color || "#ff000000"
                    // Rejects malformed input instead of silently passing
                    // a typo through to the compositor.
                    readonly property bool valid: /^#([0-9a-fA-F]{6}|[0-9a-fA-F]{8})$/.test(text)
                    errorText: valid || text.length === 0 ? "" : qsTr("Enter a color like #FF0000")
                    onEditingFinished: {
                        if (valid)
                            EditorState.setBackground({ kind: "color", color: text })
                    }
                }
            }

            Column {
                width: parent.width
                spacing: Theme.spacingSm
                visible: EditorState.background.kind === "blur"

                Text {
                    text: qsTr("Blur strength")
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
                ThemedSlider {
                    width: parent.width
                    label: qsTr("Blur strength")
                    from: 1
                    to: 100
                    stepSize: 1
                    valueFormatter: function (v) { return Math.round(v) }
                    value: EditorState.background.blurStrength || 20
                    onPressedChanged: {
                        if (!pressed)
                            EditorState.setBackground({ kind: "blur", blurStrength: value })
                    }
                }
            }

            // Hidden entirely on builds a package manager updates (Flatpak, Arch): there is no
            // switch to offer when the check is compiled out.
            Text {
                text: qsTr("Updates")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                topPadding: Theme.spacingMd
                visible: Updates.supported
            }

            ThemedSwitch {
                visible: Updates.supported
                checked: Updates.enabled
                text: qsTr("Check on startup")
                tooltip: qsTr("Ask GitHub once a day whether a newer Drift has been released")
                onToggled: Updates.enabled = checked
            }

            Row {
                width: parent.width
                spacing: Theme.spacingMd
                visible: Updates.supported

                ThemedButton {
                    id: checkNowButton
                    variant: "secondary"
                    glyph: Theme.icons.refresh
                    text: Updates.checking ? qsTr("Checking…") : qsTr("Check now")
                    enabled: !Updates.checking
                    onClicked: Updates.checkNow()
                }

                ThemedLabel {
                    width: Math.max(0, parent.width - checkNowButton.width - parent.spacing)
                    anchors.verticalCenter: parent.verticalCenter
                    text: Updates.status.length > 0
                          ? Updates.status
                          : qsTr("Drift %1").arg(Updates.currentVersion)
                }
            }

            Text {
                text: qsTr("Extra packs")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                topPadding: Theme.spacingMd
            }

            ThemedSwitch {
                checked: Addons.remindEssential
                text: qsTr("Remind about essential packs")
                tooltip: qsTr("Pulse the Extras icon when the video, transitions, and audio packs are not installed")
                onToggled: Addons.remindEssential = checked
            }

            ThemedSwitch {
                checked: Addons.remindUpdates
                text: qsTr("Remind about pack updates")
                tooltip: qsTr("Pulse the Extras icon when updates are available for packs you already have installed")
                onToggled: Addons.remindUpdates = checked
            }

            Text {
                text: qsTr("Language")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                topPadding: Theme.spacingMd
            }

            ThemedComboBox {
                width: parent.width
                textRole: "label"
                valueRole: "id"
                model: EditorState.uiLanguages
                tooltip: qsTr("Language for menus and labels. Takes effect immediately.")
                currentIndex: {
                    const langs = EditorState.uiLanguages
                    for (var i = 0; i < langs.length; ++i) {
                        if (langs[i].id === EditorState.uiLanguage)
                            return i
                    }
                    return 0
                }
                onActivated: {
                    const langs = EditorState.uiLanguages
                    if (currentIndex >= 0 && currentIndex < langs.length)
                        EditorState.uiLanguage = langs[currentIndex].id
                }
            }

            Text {
                text: qsTr("Startup")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                topPadding: Theme.spacingMd
            }

            ThemedSwitch {
                checked: EditorState.reopenLastProject
                text: qsTr("Reopen last project on startup")
                tooltip: qsTr("Automatically restore the last open project. Unsaved work is kept in a side snapshot and never overwrites your save file.")
                onToggled: EditorState.reopenLastProject = checked
            }

            Text {
                text: qsTr("Agent access")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                topPadding: Theme.spacingMd
            }

            Rectangle {
                width: parent.width
                height: mcpWarning.implicitHeight + Theme.spacingLg * 2
                radius: Theme.radiusSm
                color: Theme.panelSecondaryBg
                border.width: Theme.borderWidth
                border.color: Theme.warning

                Text {
                    id: mcpWarning
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins: Theme.spacingLg
                    wrapMode: Text.WordWrap
                    color: Theme.panelSecondaryForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    text: qsTr("Lets local agents (Cursor, Claude Code) control this editor: import media, edit the timeline, and capture frames. Any process on this computer with the token can do the same. Off at every launch. Turn it off when you are done.")
                }
            }

            ThemedSwitch {
                checked: EditorState.mcpEnabled
                text: qsTr("Enable for this session")
                tooltip: qsTr("Start a localhost MCP server. Not saved. Stops when Drift quits or you turn this off.")
                onToggled: EditorState.mcpEnabled = checked
            }

            ThemedLabel {
                width: parent.width
                visible: EditorState.mcpError.length > 0
                text: EditorState.mcpError
                color: Theme.destructive
            }

            Column {
                width: parent.width
                spacing: Theme.spacingMd
                visible: EditorState.mcpRunning

                ThemedLabel {
                    width: parent.width
                    text: qsTr("Listening on %1").arg(EditorState.mcpUrl)
                    size: "sm"
                    tone: "default"
                }

                ThemedLabel {
                    width: parent.width
                    text: qsTr("Token (shown once this session)")
                }

                Text {
                    width: parent.width
                    wrapMode: Text.WrapAnywhere
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.fontSizeXs
                    color: Theme.panelForeground
                    text: EditorState.mcpToken
                    textFormat: Text.PlainText
                }

                Row {
                    spacing: Theme.spacingMd
                    width: parent.width

                    ThemedButton {
                        variant: "secondary"
                        glyph: Theme.icons.copy
                        text: qsTr("Copy Cursor config")
                        tooltip: qsTr("Copy an mcp.json snippet with this session’s URL and token")
                        onClicked: EditorState.copyMcpCursorSnippet()
                    }

                    ThemedButton {
                        variant: "secondary"
                        glyph: Theme.icons.copy
                        text: qsTr("Copy Claude command")
                        tooltip: qsTr("Copy a claude mcp add command for this session")
                        onClicked: EditorState.copyMcpClaudeCommand()
                    }
                }

                ThemedButton {
                    variant: "ghost"
                    glyph: Theme.icons.copy
                    text: qsTr("Copy stdio attach (one-time setup)")
                    tooltip: qsTr("Add this once to mcp.json. drift --mcp-stdio talks to whichever session is running. Agent access still has to be turned on in Drift.")
                    onClicked: EditorState.copyMcpStdioSnippet()
                }

                ThemedButton {
                    variant: "ghost"
                    glyph: Theme.icons.copy
                    text: qsTr("Copy agent guide")
                    tooltip: qsTr("Copy workflow, conventions, and toolbox list for agents")
                    onClicked: EditorState.copyMcpAgentGuide()
                }

                ThemedLabel {
                    width: parent.width
                    text: qsTr("Pinned endpoints: /mcp/media, /mcp/timeline, /mcp/canvas, /mcp/playback, /mcp/text, /mcp/effects, /mcp/project")
                    size: "sm"
                    tone: "muted"
                    wrapMode: Text.WordWrap
                }

                Rectangle {
                    width: parent.width
                    radius: Theme.radiusSm
                    color: Theme.panelSecondaryBg
                    border.width: Theme.borderWidth
                    border.color: Theme.border
                    implicitHeight: mcpWorkflowColumn.implicitHeight + Theme.spacingLg * 2

                    Column {
                        id: mcpWorkflowColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.margins: Theme.spacingLg
                        spacing: Theme.spacingSm

                        Text {
                            width: parent.width
                            wrapMode: Text.WordWrap
                            color: Theme.panelSecondaryForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs
                            text: qsTr("Agent workflow")
                            font.weight: Font.DemiBold
                        }

                        Repeater {
                            model: [
                                qsTr("1. Enable agent access for this session."),
                                qsTr("2. Connect Cursor or Claude with the copied config."),
                                qsTr("3. Call catalog, then toolbox, then apply with batched ops."),
                                qsTr("4. Use inspect({clips:true}) for clip ids; capture() to verify frames.")
                            ]
                            Text {
                                width: parent.width
                                wrapMode: Text.WordWrap
                                color: Theme.panelSecondaryForeground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeXs
                                text: modelData
                            }
                        }
                    }
                }
            }
        }
    }

    ColorDialog {
        id: canvasColorDialog
        title: qsTr("Choose background colour")

        function colorToHex(c) {
            var toHex = function(v) {
                var h = Math.round(v * 255).toString(16);
                return h.length === 1 ? "0" + h : h;
            }
            return "#" + toHex(c.a) + toHex(c.r) + toHex(c.g) + toHex(c.b);
        }

        onAccepted: {
            EditorState.setBackground({ kind: "color", color: colorToHex(selectedColor) })
        }
    }
}
