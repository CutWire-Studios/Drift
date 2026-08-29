import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import Drift
import ".."

Item {
    id: root

    property int clipDataRevision: 0
    readonly property var clipData: {
        void clipDataRevision
        return EditorState.selectedClipData
    }
    readonly property bool hasSelection: !!clipData && Object.keys(clipData).length > 0
    readonly property string clipKind: hasSelection ? (clipData.kind || "") : ""
    readonly property bool hasTextStyle: hasSelection
                                         && (clipKind === "text" || clipKind === "subtitle")
                                         && !!clipData.textStyle
    readonly property var textStyle: hasTextStyle ? clipData.textStyle : ({
                                                                       "fontFamily": "Inter",
                                                                       "pixelSize": 64,
                                                                       "fontWeight": 700,
                                                                       "italic": false,
                                                                       "color": "#ffffffff",
                                                                       "align": "center",
                                                                       "valign": "middle",
                                                                       "wordWrap": true,
                                                                       "lineHeight": 1.2,
                                                                       "letterSpacing": 0,
                                                                       "outlineEnabled": false,
                                                                       "outlineWidth": 2,
                                                                       "outlineColor": "#ff000000",
                                                                       "shadowEnabled": false,
                                                                       "shadowOffsetX": 0,
                                                                       "shadowOffsetY": 4,
                                                                       "shadowBlur": 8,
                                                                       "shadowOpacity": 0.6,
                                                                       "shadowColor": "#ff000000",
                                                                       "glowEnabled": false,
                                                                       "glowColor": "#ffffffff",
                                                                       "glowRadius": 18,
                                                                       "glowOpacity": 0.8,
                                                                       "boxEnabled": false,
                                                                       "boxColor": "#80000000",
                                                                       "boxPadding": 8,
                                                                       "boxRadius": 0,
                                                                       "packId": "",
                                                                       "wordHighlight": { "enabled": false, "color": "#ffe62828", "padding": 6, "radius": 4 },
                                                                       "underlineEnabled": false,
                                                                       "underlineColor": "#ffe62828",
                                                                       "underlineWidth": 6,
                                                                       "underlineOffset": 4,
                                                                       "accent": { "rule": "none", "n": 2, "phase": 0,
                                                                                   "colorEnabled": false, "color": "#ffffd640",
                                                                                   "sizeScale": 1, "outlineEnabled": false,
                                                                                   "outlineWidth": 0, "outlineColor": "#ff000000",
                                                                                   "highlight": { "enabled": false, "color": "#ffe62828", "padding": 6, "radius": 4 } },
                                                                       "animIn": { "kind": "none", "duration": 0.4, "ease": "easeOut" },
                                                                       "animOut": { "kind": "none", "duration": 0.4, "ease": "easeOut" }
                                                                   })

    // The selected family's real weight ladder — never an invented one. Single-weight display faces
    // (Anton, Bebas Neue, Pacifico...) expose exactly one entry and no italic.
    readonly property var fontFamilyInfo: {
        void clipDataRevision
        const catalog = EditorState.fontCatalog()
        for (let i = 0; i < catalog.length; ++i) {
            if (catalog[i].family === root.textStyle.fontFamily)
                return catalog[i]
        }
        return null
    }
    readonly property var availableWeights: fontFamilyInfo ? fontFamilyInfo.weights
                                                           : [100, 200, 300, 400, 500, 600, 700, 800, 900]
    readonly property bool familyHasItalic: fontFamilyInfo ? fontFamilyInfo.hasItalic : true

    readonly property var weightLabels: ({
                                             100: "Thin", 200: "ExtraLight", 300: "Light",
                                             400: "Regular", 500: "Medium", 600: "SemiBold",
                                             700: "Bold", 800: "ExtraBold", 900: "Black"
                                         })
    readonly property var animKinds: ["none", "fade", "slideUp", "slideDown", "slideLeft", "slideRight", "pop", "blur", "typewriter", "rise", "bounce", "wave"]
    readonly property var animKindLabels: ["None", "Fade", "Slide up", "Slide down", "Slide left", "Slide right", "Pop", "Blur", "Typewriter", "Rise", "Bounce", "Wave"]
    readonly property var easeKinds: ["linear", "easeOut", "easeInOut", "back"]
    readonly property var easeLabels: ["Linear", "Ease out", "Ease in-out", "Back"]
    readonly property var animUnits: ["block", "character", "word", "line"]
    readonly property var animUnitLabels: ["Whole block", "Character", "Word", "Line"]
    readonly property var animOrders: ["forward", "backward", "centerOut", "random"]
    readonly property var animOrderLabels: ["Forward", "Backward", "Center out", "Random"]
    readonly property var accentRules: ["none", "firstWord", "lastWord", "everyOther", "everyNth",
                                        "longestWord", "randomStable", "karaoke"]
    readonly property var accentRuleLabels: ["No accent", "First word", "Last word", "Every other word",
                                             "Every Nth word", "Longest word", "Random words",
                                             "Spoken word (karaoke)"]

    function setTextStyleKey(key, value) {
        const patch = {}
        patch[key] = value
        EditorState.setTextStyle(EditorState.selectedTrack, EditorState.selectedClip, patch)
    }

    function setOutlineEnabled(on) {
        const patch = { "outlineEnabled": on }
        // Turning on with a zero width would look like a no-op; seed a usable default.
        if (on && root.textStyle.outlineWidth <= 0)
            patch.outlineWidth = 2
        EditorState.setTextStyle(EditorState.selectedTrack, EditorState.selectedClip, patch)
    }


    // Nested style groups: the C++ side takes the same partial-patch shape one level down.
    function setTextGroupKey(group, key, value) {
        const inner = {}
        inner[key] = value
        const patch = {}
        patch[group] = inner
        EditorState.setTextStyle(EditorState.selectedTrack, EditorState.selectedClip, patch)
    }

    function setTextAccentHighlightKey(key, value) {
        const highlight = {}
        highlight[key] = value
        EditorState.setTextStyle(EditorState.selectedTrack, EditorState.selectedClip,
                                 { "accent": { "highlight": highlight } })
    }

    function setTextAnim(which, key, value) {
        const anim = {}
        anim[key] = value
        const patch = {}
        patch[which] = anim
        EditorState.setTextStyle(EditorState.selectedTrack, EditorState.selectedClip, patch)
    }

    // Reveal granularity / stagger / order are shared by the entrance and exit, so write both.
    function setTextReveal(key, value) {
        const anim = {}
        anim[key] = value
        const patch = { "animIn": anim, "animOut": anim }
        EditorState.setTextStyle(EditorState.selectedTrack, EditorState.selectedClip, patch)
    }

    height: contentCol.height
    implicitHeight: contentCol.height

    function refreshFields() {
        if (textContentField && !textContentField.activeFocus)
            textContentField.text = root.clipData.textContent || ""
        if (!root.hasTextStyle)
            return
        const s = root.textStyle
        if (pixelSizeField && !pixelSizeField.activeFocus)
            pixelSizeField.value = s.pixelSize
        if (textColorField && !textColorField.activeFocus)
            textColorField.text = s.color
        if (lineHeightField && !lineHeightField.activeFocus)
            lineHeightField.value = s.lineHeight
        if (letterSpacingField && !letterSpacingField.activeFocus)
            letterSpacingField.value = s.letterSpacing
        if (outlineWidthField && !outlineWidthField.activeFocus)
            outlineWidthField.value = s.outlineWidth
        if (shadowOffsetXField && !shadowOffsetXField.activeFocus)
            shadowOffsetXField.value = s.shadowOffsetX
        if (shadowOffsetYField && !shadowOffsetYField.activeFocus)
            shadowOffsetYField.value = s.shadowOffsetY
        if (shadowBlurField && !shadowBlurField.activeFocus)
            shadowBlurField.value = s.shadowBlur
        if (shadowOpacityField && !shadowOpacityField.activeFocus)
            shadowOpacityField.value = s.shadowOpacity
        if (boxPaddingField && !boxPaddingField.activeFocus)
            boxPaddingField.value = s.boxPadding
        if (boxRadiusField && !boxRadiusField.activeFocus)
            boxRadiusField.value = s.boxRadius
        if (glowRadiusField && !glowRadiusField.activeFocus)
            glowRadiusField.value = s.glowRadius
        if (glowOpacityField && !glowOpacityField.activeFocus)
            glowOpacityField.value = s.glowOpacity
        if (wordHighlightPaddingField && !wordHighlightPaddingField.activeFocus)
            wordHighlightPaddingField.value = s.wordHighlight.padding
        if (wordHighlightRadiusField && !wordHighlightRadiusField.activeFocus)
            wordHighlightRadiusField.value = s.wordHighlight.radius
        if (underlineWidthField && !underlineWidthField.activeFocus)
            underlineWidthField.value = s.underlineWidth
        if (underlineOffsetField && !underlineOffsetField.activeFocus)
            underlineOffsetField.value = s.underlineOffset
        if (accentEveryNField && !accentEveryNField.activeFocus)
            accentEveryNField.value = s.accent.n
        if (accentSizeScaleField && !accentSizeScaleField.activeFocus)
            accentSizeScaleField.value = s.accent.sizeScale
        if (accentOutlineWidthField && !accentOutlineWidthField.activeFocus)
            accentOutlineWidthField.value = s.accent.outlineWidth
        if (animInDurationField && !animInDurationField.activeFocus)
            animInDurationField.value = s.animIn.duration
        if (animOutDurationField && !animOutDurationField.activeFocus)
            animOutDurationField.value = s.animOut.duration
        if (animStaggerField && !animStaggerField.activeFocus)
            animStaggerField.value = s.animIn.stagger
    }

    Connections {
        target: EditorState
        function onSelectionChanged() { root.clipDataRevision++; root.refreshFields() }
        function onSelectedClipDataChanged() { root.clipDataRevision++; root.refreshFields() }
        function onTracksChanged() { root.clipDataRevision++; root.refreshFields() }
    }

    Component.onCompleted: refreshFields()

    Column {
        id: contentCol
        width: root.width
        spacing: Theme.spacingMd

        CollapsibleSection {
            width: parent.width
            title: qsTr("Content")
            collapsible: false
            showSeparator: false
            visible: root.clipKind === "text"

            ThemedTextArea {
                id: textContentField
                width: parent.width
                height: 80
                onTextChanged: {
                    if (root.clipKind !== "text")
                        return
                    if ((root.clipData.textContent || "") === text)
                        return
                    EditorState.previewSetClipTextContent(
                        EditorState.selectedTrack, EditorState.selectedClip, text)
                }
                onEditingFinished: EditorState.commitTextEdit(
                                       EditorState.selectedTrack, EditorState.selectedClip, text)
            }
        }

        CollapsibleSection {
            width: parent.width
            title: qsTr("Style")
            expanded: true
            visible: root.hasTextStyle

            Text {
                text: qsTr("Style pack")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            TextStylePackPicker {
                width: parent.width
                packId: root.textStyle.packId || ""
                onPackPicked: id => EditorState.applyTextPreset(
                                   EditorState.selectedTrack,
                                   EditorState.selectedClip,
                                   id)
            }

            ThemedButton {
                width: parent.width
                variant: "secondary"
                glyph: Theme.icons.save
                text: qsTr("Save style…")
                tooltip: qsTr("Save this text's look as a reusable style")
                onClicked: saveStyleDialog.openWith(qsTr("Save text style"),
                                                    qsTr("My style %1")
                                                        .arg(EditorState.userTextPresets().length + 1))
            }

            Text {
                text: qsTr("Font")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            FontPicker {
                width: parent.width
                family: root.textStyle.fontFamily
                onFamilyPicked: family => root.setTextStyleKey("fontFamily", family)
            }

            Row {
                width: parent.width
                spacing: 8

                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 4
                    Text {
                        text: qsTr("Weight")
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                    ThemedComboBox {
                        id: fontWeightBox
                        width: parent.width
                        model: root.availableWeights.map(
                                   w => root.weightLabels[w] || String(w))
                        currentIndex: Math.max(0, root.availableWeights.indexOf(root.textStyle.fontWeight))
                        onActivated: root.setTextStyleKey("fontWeight", root.availableWeights[currentIndex])
                    }
                }

                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 4
                    Text {
                        text: qsTr("Size")
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                    ThemedNumberField {
                        id: pixelSizeField
                        unit: "px"
                        width: parent.width
                        decimals: 0
                        step: 1
                        from: 1
                        to: 500
                        onEdited: v => root.setTextStyleKey("pixelSize", v)
                    }
                }
            }

            Row {
                width: parent.width
                spacing: 8

                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 4
                    Text {
                        text: qsTr("Color")
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                    Row {
                        spacing: 6
                        Rectangle {
                            width: Theme.spacing3xl
                            height: Theme.spacing3xl
                            radius: Theme.radiusSm
                            anchors.verticalCenter: parent.verticalCenter
                            color: root.textStyle.color
                            border.width: swatch_color.containsMouse ? Theme.borderWidthFocus : Theme.borderWidth
                            border.color: swatch_color.containsMouse ? Theme.primary : Theme.panelBorder

                            Behavior on border.color {
                                ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                            }

                            ThemedToolTip {
                                text: qsTr("Choose text colour")
                                visible: swatch_color.containsMouse
                            }

                            MouseArea {
                                id: swatch_color
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    styleColorDialog.targetStyleKey = "color"
                                    styleColorDialog.selectedColor = root.textStyle.color
                                    styleColorDialog.open()
                                }
                            }
                        }
                        ThemedTextField {
                            id: textColorField
                            width: 92
                            text: root.textStyle.color
                            color: Theme.panelForeground
                            font.family: Theme.monoFontFamily
                            font.pixelSize: Theme.fontSizeSm
                            readonly property bool validHex:
                                /^#([0-9a-fA-F]{6}|[0-9a-fA-F]{8})$/.test(text)
                            errorText: validHex || text.length === 0 ? "" : qsTr("Enter a color like #FF0000")
                            onEditingFinished: if (validHex) root.setTextStyleKey("color", text)
                        }
                    }
                }

                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 4
                    Text {
                        text: qsTr("Style")
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                    ThemedToggleButton {
                        width: 60
                        text: qsTr("Italic")
                        checked: root.textStyle.italic
                        enabled: root.familyHasItalic
                        tooltip: root.familyHasItalic
                                 ? qsTr("Italicise the text")
                                 : qsTr("%1 has no italic face").arg(root.textStyle.fontFamily)
                        onClicked: root.setTextStyleKey("italic", !root.textStyle.italic)
                    }
                }
            }
        }

        CollapsibleSection {
            width: parent.width
            title: qsTr("Layout")
            expanded: true
            visible: root.hasTextStyle

            Row {
                width: parent.width
                spacing: Theme.spacingMd

                Repeater {
                    model: [
                        { value: "left",   glyph: Theme.icons.alignLeft,   label: qsTr("Align left") },
                        { value: "center", glyph: Theme.icons.alignCenter, label: qsTr("Align centre") },
                        { value: "right",  glyph: Theme.icons.alignRight,  label: qsTr("Align right") }
                    ]
                    delegate: ThemedToggleButton {
                        required property var modelData
                        width: 34
                        glyph: modelData.glyph
                        checked: root.textStyle.align === modelData.value
                        tooltip: modelData.label
                        onClicked: root.setTextStyleKey("align", modelData.value)
                    }
                }

                Rectangle {
                    width: Theme.borderWidth
                    height: Theme.controlHeightSm
                    color: Theme.panelBorder
                }

                Repeater {
                    model: [
                        { value: "top",    glyph: Theme.icons.alignTop,    label: qsTr("Align top") },
                        { value: "middle", glyph: Theme.icons.alignMiddle, label: qsTr("Align middle") },
                        { value: "bottom", glyph: Theme.icons.alignBottom, label: qsTr("Align bottom") }
                    ]
                    delegate: ThemedToggleButton {
                        required property var modelData
                        width: 34
                        glyph: modelData.glyph
                        checked: root.textStyle.valign === modelData.value
                        tooltip: modelData.label
                        onClicked: root.setTextStyleKey("valign", modelData.value)
                    }
                }
            }

            Row {
                width: parent.width
                spacing: 8

                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 4
                    Text {
                        text: qsTr("Line height")
                        HoverHandler { id: tipHover761 }
                        ThemedToolTip { text: qsTr("Vertical spacing between lines, as a multiple of the font size"); visible: tipHover761.hovered }
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                    ThemedNumberField {
                        id: lineHeightField
                        width: parent.width
                        decimals: 2
                        step: 0.05
                        from: 0.5
                        to: 4
                        onEdited: v => root.setTextStyleKey("lineHeight", v)
                    }
                }

                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 4
                    Text {
                        text: qsTr("Letter spacing")
                        HoverHandler { id: tipHover781 }
                        ThemedToolTip { text: qsTr("Extra space between characters, in pixels"); visible: tipHover781.hovered }
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                    ThemedNumberField {
                        id: letterSpacingField
                        to: 200
                        from: -100
                        unit: "px"
                        width: parent.width
                        decimals: 1
                        step: 0.5
                        onEdited: v => root.setTextStyleKey("letterSpacing", v)
                    }
                }
            }

            ThemedToggleButton {
                width: 96
                text: qsTr("Word wrap")
                checked: root.textStyle.wordWrap
                tooltip: qsTr("Wrap long lines inside the text box instead of overflowing")
                onClicked: root.setTextStyleKey("wordWrap", !root.textStyle.wordWrap)
            }
        }

        CollapsibleSection {
            width: parent.width
            title: qsTr("Appearance")
            expanded: false
            visible: root.hasTextStyle

            CollapsibleSection {
                width: parent.width
                title: qsTr("Outline")
                tooltip: qsTr("Outline around each letter")
                collapsible: false
                showSeparator: false
                showSwitch: true
                switchChecked: root.textStyle.outlineEnabled
                switchTooltip: qsTr("Draw an outline around each letter")
                onSwitchToggled: on => root.setOutlineEnabled(on)

                Row {
                    width: parent.width
                    spacing: 8

                    Column {
                        width: (parent.width - parent.spacing) / 2
                        spacing: 4
                        Text {
                            text: qsTr("Width")
                            color: Theme.mutedForeground
                            font.pixelSize: Theme.fontSizeXs
                            font.family: Theme.fontFamily
                        }
                        ThemedNumberField {
                            id: outlineWidthField
                            to: 100
                            unit: "px"
                            width: parent.width
                            decimals: 1
                            step: 0.5
                            from: 0
                            onEdited: v => root.setTextStyleKey("outlineWidth", v)
                        }
                    }

                    Column {
                        width: (parent.width - parent.spacing) / 2
                        spacing: 4
                        Text {
                            text: qsTr("Color")
                            color: Theme.mutedForeground
                            font.pixelSize: Theme.fontSizeXs
                            font.family: Theme.fontFamily
                        }
                        ColorSwatchField {
                            hex: root.textStyle.outlineColor
                            tooltip: qsTr("Choose outline colour")
                            onEdited: value => root.setTextStyleKey("outlineColor", value)
                        }
                    }
                }
            }

            CollapsibleSection {
                width: parent.width
                title: qsTr("Shadow")
                tooltip: qsTr("Draw a drop shadow behind the text")
                collapsible: false
                showSeparator: false
                showSwitch: true
                switchChecked: root.textStyle.shadowEnabled
                switchTooltip: qsTr("Draw a drop shadow behind the text")
                onSwitchToggled: on => root.setTextStyleKey("shadowEnabled", on)

                Row {
                    width: parent.width
                    spacing: 8

                    Column {
                        width: (parent.width - parent.spacing) / 2
                        spacing: 4
                        Text {
                            text: qsTr("Offset X")
                            color: Theme.mutedForeground
                            font.pixelSize: Theme.fontSizeXs
                            font.family: Theme.fontFamily
                        }
                        ThemedNumberField {
                            id: shadowOffsetXField
                            to: 500
                            from: -500
                            unit: "px"
                            width: parent.width
                            decimals: 1
                            step: 1
                            onEdited: v => root.setTextStyleKey("shadowOffsetX", v)
                        }
                    }

                    Column {
                        width: (parent.width - parent.spacing) / 2
                        spacing: 4
                        Text {
                            text: qsTr("Offset Y")
                            color: Theme.mutedForeground
                            font.pixelSize: Theme.fontSizeXs
                            font.family: Theme.fontFamily
                        }
                        ThemedNumberField {
                            id: shadowOffsetYField
                            to: 500
                            from: -500
                            unit: "px"
                            width: parent.width
                            decimals: 1
                            step: 1
                            onEdited: v => root.setTextStyleKey("shadowOffsetY", v)
                        }
                    }
                }

                Row {
                    width: parent.width
                    spacing: 8

                    Column {
                        width: (parent.width - parent.spacing) / 2
                        spacing: 4
                        Text {
                            text: qsTr("Blur")
                            color: Theme.mutedForeground
                            font.pixelSize: Theme.fontSizeXs
                            font.family: Theme.fontFamily
                        }
                        ThemedNumberField {
                            id: shadowBlurField
                            to: 100
                            unit: "px"
                            width: parent.width
                            decimals: 1
                            step: 1
                            from: 0
                            onEdited: v => root.setTextStyleKey("shadowBlur", v)
                        }
                    }

                    Column {
                        width: (parent.width - parent.spacing) / 2
                        spacing: 4
                        Text {
                            text: qsTr("Opacity")
                            color: Theme.mutedForeground
                            font.pixelSize: Theme.fontSizeXs
                            font.family: Theme.fontFamily
                        }
                        ThemedNumberField {
                            id: shadowOpacityField
                            width: parent.width
                            decimals: 2
                            step: 0.05
                            from: 0
                            to: 1
                            onEdited: v => root.setTextStyleKey("shadowOpacity", v)
                        }
                    }
                }

                Column {
                    width: parent.width
                    spacing: 4
                    Text {
                        text: qsTr("Shadow colour")
                        color: Theme.mutedForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                    }
                    ColorSwatchField {
                        hex: root.textStyle.shadowColor
                        tooltip: qsTr("Choose shadow colour")
                        onEdited: value => root.setTextStyleKey("shadowColor", value)
                    }
                }
            }

            CollapsibleSection {
                width: parent.width
                title: qsTr("Background")
                tooltip: qsTr("Draw a filled box behind the text")
                collapsible: false
                showSeparator: false
                showSwitch: true
                switchChecked: root.textStyle.boxEnabled
                switchTooltip: qsTr("Draw a filled box behind the text")
                onSwitchToggled: on => root.setTextStyleKey("boxEnabled", on)

                ColorSwatchField {
                    hex: root.textStyle.boxColor
                    tooltip: qsTr("Choose background colour")
                    onEdited: value => root.setTextStyleKey("boxColor", value)
                }

                Row {
                    width: parent.width
                    spacing: 8

                    Column {
                        width: (parent.width - parent.spacing) / 2
                        spacing: 4
                        Text {
                            text: qsTr("Padding")
                            HoverHandler { id: tipHover1088 }
                            ThemedToolTip { text: qsTr("Space between the text and the edge of its background box"); visible: tipHover1088.hovered }
                            color: Theme.mutedForeground
                            font.pixelSize: Theme.fontSizeXs
                            font.family: Theme.fontFamily
                        }
                        ThemedNumberField {
                            id: boxPaddingField
                            to: 500
                            unit: "px"
                            width: parent.width
                            decimals: 1
                            step: 1
                            from: 0
                            onEdited: v => root.setTextStyleKey("boxPadding", v)
                        }
                    }

                    Column {
                        width: (parent.width - parent.spacing) / 2
                        spacing: 4
                        Text {
                            text: qsTr("Corner radius")
                            HoverHandler { id: tipHover1109 }
                            ThemedToolTip { text: qsTr("Roundness of the background box corners"); visible: tipHover1109.hovered }
                            color: Theme.mutedForeground
                            font.pixelSize: Theme.fontSizeXs
                            font.family: Theme.fontFamily
                        }
                        ThemedNumberField {
                            id: boxRadiusField
                            to: 500
                            unit: "px"
                            width: parent.width
                            decimals: 1
                            step: 1
                            from: 0
                            onEdited: v => root.setTextStyleKey("boxRadius", v)
                        }
                    }
                }
            }

            CollapsibleSection {
                width: parent.width
                title: qsTr("Glow")
                tooltip: qsTr("Coloured bloom around the letters, with no offset")
                collapsible: false
                showSeparator: false
                showSwitch: true
                switchChecked: root.textStyle.glowEnabled
                switchTooltip: qsTr("Coloured bloom around the letters, with no offset")
                onSwitchToggled: on => root.setTextStyleKey("glowEnabled", on)

                Row {
                    width: parent.width
                    spacing: 8

                    Column {
                        width: (parent.width - parent.spacing) / 2
                        spacing: 4
                        Text {
                            text: qsTr("Radius")
                            color: Theme.mutedForeground
                            font.pixelSize: Theme.fontSizeXs
                            font.family: Theme.fontFamily
                        }
                        ThemedNumberField {
                            id: glowRadiusField
                            to: 200
                            unit: "px"
                            width: parent.width
                            decimals: 1
                            step: 1
                            from: 0
                            onEdited: v => root.setTextStyleKey("glowRadius", v)
                        }
                    }

                    Column {
                        width: (parent.width - parent.spacing) / 2
                        spacing: 4
                        Text {
                            text: qsTr("Opacity")
                            color: Theme.mutedForeground
                            font.pixelSize: Theme.fontSizeXs
                            font.family: Theme.fontFamily
                        }
                        ThemedNumberField {
                            id: glowOpacityField
                            to: 1
                            width: parent.width
                            decimals: 2
                            step: 0.05
                            from: 0
                            onEdited: v => root.setTextStyleKey("glowOpacity", v)
                        }
                    }
                }

                Column {
                    width: parent.width
                    spacing: 4
                    Text {
                        text: qsTr("Glow colour")
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                    ColorSwatchField {
                        hex: root.textStyle.glowColor
                        tooltip: qsTr("Choose glow colour")
                        onEdited: value => root.setTextStyleKey("glowColor", value)
                    }
                }
            }

            CollapsibleSection {
                width: parent.width
                title: qsTr("Word highlight")
                tooltip: qsTr("Filled pill behind every word, sized to the word itself")
                collapsible: false
                showSeparator: false
                showSwitch: true
                switchChecked: root.textStyle.wordHighlight.enabled
                switchTooltip: qsTr("Filled pill behind every word, sized to the word itself")
                onSwitchToggled: on => root.setTextGroupKey("wordHighlight", "enabled", on)

                Row {
                    width: parent.width
                    spacing: 8

                    Column {
                        width: (parent.width - parent.spacing) / 2
                        spacing: 4
                        Text {
                            text: qsTr("Thickness")
                            HoverHandler { id: tipHoverHlPad }
                            ThemedToolTip { text: qsTr("How far the pill extends past the word"); visible: tipHoverHlPad.hovered }
                            color: Theme.mutedForeground
                            font.pixelSize: Theme.fontSizeXs
                            font.family: Theme.fontFamily
                        }
                        ThemedNumberField {
                            id: wordHighlightPaddingField
                            to: 200
                            unit: "px"
                            width: parent.width
                            decimals: 1
                            step: 1
                            from: 0
                            onEdited: v => root.setTextGroupKey("wordHighlight", "padding", v)
                        }
                    }

                    Column {
                        width: (parent.width - parent.spacing) / 2
                        spacing: 4
                        Text {
                            text: qsTr("Corner radius")
                            color: Theme.mutedForeground
                            font.pixelSize: Theme.fontSizeXs
                            font.family: Theme.fontFamily
                        }
                        ThemedNumberField {
                            id: wordHighlightRadiusField
                            to: 200
                            unit: "px"
                            width: parent.width
                            decimals: 1
                            step: 1
                            from: 0
                            onEdited: v => root.setTextGroupKey("wordHighlight", "radius", v)
                        }
                    }
                }

                Column {
                    width: parent.width
                    spacing: 4
                    Text {
                        text: qsTr("Highlight colour")
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                    ColorSwatchField {
                        hex: root.textStyle.wordHighlight.color
                        tooltip: qsTr("Choose highlight colour")
                        onEdited: value => root.setTextGroupKey("wordHighlight", "color", value)
                    }
                }
            }

            CollapsibleSection {
                width: parent.width
                title: qsTr("Underline")
                tooltip: qsTr("Draw a rule under each line of text")
                collapsible: false
                showSeparator: false
                showSwitch: true
                switchChecked: root.textStyle.underlineEnabled
                switchTooltip: qsTr("Draw a rule under each line of text")
                onSwitchToggled: on => root.setTextStyleKey("underlineEnabled", on)

                Row {
                    width: parent.width
                    spacing: 8

                    Column {
                        width: (parent.width - parent.spacing) / 2
                        spacing: 4
                        Text {
                            text: qsTr("Thickness")
                            color: Theme.mutedForeground
                            font.pixelSize: Theme.fontSizeXs
                            font.family: Theme.fontFamily
                        }
                        ThemedNumberField {
                            id: underlineWidthField
                            to: 100
                            unit: "px"
                            width: parent.width
                            decimals: 1
                            step: 0.5
                            from: 0
                            onEdited: v => root.setTextStyleKey("underlineWidth", v)
                        }
                    }

                    Column {
                        width: (parent.width - parent.spacing) / 2
                        spacing: 4
                        Text {
                            text: qsTr("Offset")
                            HoverHandler { id: tipHoverUnderlineOffset }
                            ThemedToolTip { text: qsTr("Gap between the baseline and the rule"); visible: tipHoverUnderlineOffset.hovered }
                            color: Theme.mutedForeground
                            font.pixelSize: Theme.fontSizeXs
                            font.family: Theme.fontFamily
                        }
                        ThemedNumberField {
                            id: underlineOffsetField
                            to: 200
                            unit: "px"
                            width: parent.width
                            decimals: 1
                            step: 1
                            from: -200
                            onEdited: v => root.setTextStyleKey("underlineOffset", v)
                        }
                    }
                }

                Column {
                    width: parent.width
                    spacing: 4
                    Text {
                        text: qsTr("Underline colour")
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                    ColorSwatchField {
                        hex: root.textStyle.underlineColor
                        tooltip: qsTr("Choose underline colour")
                        onEdited: value => root.setTextStyleKey("underlineColor", value)
                    }
                }
            }
        }

        CollapsibleSection {
            width: parent.width
            title: qsTr("Word accent")
            tooltip: qsTr("Style some words differently from the rest, chosen by rule")
            expanded: false
            visible: root.hasTextStyle

            Row {
                width: parent.width
                spacing: 8

                ThemedComboBox {
                    id: accentRuleBox
                    width: root.textStyle.accent.rule === "everyNth"
                           ? (parent.width - parent.spacing) * 0.66 : parent.width
                    model: root.accentRuleLabels
                    currentIndex: Math.max(0, root.accentRules.indexOf(root.textStyle.accent.rule))
                    onActivated: root.setTextGroupKey("accent", "rule",
                                                      root.accentRules[currentIndex])
                }

                ThemedNumberField {
                    id: accentEveryNField
                    visible: root.textStyle.accent.rule === "everyNth"
                    width: (parent.width - parent.spacing) * 0.34
                    to: 16
                    from: 1
                    decimals: 0
                    step: 1
                    onEdited: v => root.setTextGroupKey("accent", "n", v)
                }
            }

            Column {
                width: parent.width
                spacing: Theme.spacingMd
                visible: root.textStyle.accent.rule !== "none"

                CollapsibleSection {
                    width: parent.width
                    title: qsTr("Accent colour")
                    tooltip: qsTr("Recolour the words the rule picks out")
                    collapsible: false
                    showSeparator: false
                    showSwitch: true
                    switchChecked: root.textStyle.accent.colorEnabled
                    switchTooltip: qsTr("Recolour the words the rule picks out")
                    onSwitchToggled: on => root.setTextGroupKey("accent", "colorEnabled", on)

                    ColorSwatchField {
                        hex: root.textStyle.accent.color
                        tooltip: qsTr("Choose accent colour")
                        onEdited: value => root.setTextGroupKey("accent", "color", value)
                    }
                }

                Column {
                    width: (parent.width - 8) / 2
                    spacing: 4
                    Text {
                        text: qsTr("Accent size")
                        HoverHandler { id: tipHoverAccentSize }
                        ThemedToolTip {
                            text: qsTr("Size of the accented words relative to the rest of the line")
                            visible: tipHoverAccentSize.hovered
                        }
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                    ThemedNumberField {
                        id: accentSizeScaleField
                        to: 4
                        unit: "x"
                        width: parent.width
                        decimals: 2
                        step: 0.05
                        from: 0.25
                        onEdited: v => root.setTextGroupKey("accent", "sizeScale", v)
                    }
                }

                CollapsibleSection {
                    width: parent.width
                    title: qsTr("Accent outline")
                    tooltip: qsTr("Give the accented words their own outline")
                    collapsible: false
                    showSeparator: false
                    showSwitch: true
                    switchChecked: root.textStyle.accent.outlineEnabled
                    switchTooltip: qsTr("Give the accented words their own outline")
                    onSwitchToggled: on => root.setTextGroupKey("accent", "outlineEnabled", on)

                    ColorSwatchField {
                        hex: root.textStyle.accent.outlineColor
                        tooltip: qsTr("Choose accent outline colour")
                        onEdited: value => root.setTextGroupKey("accent", "outlineColor", value)
                    }

                    Column {
                        width: (parent.width - 8) / 2
                        spacing: 4
                        Text {
                            text: qsTr("Width")
                            color: Theme.mutedForeground
                            font.pixelSize: Theme.fontSizeXs
                            font.family: Theme.fontFamily
                        }
                        ThemedNumberField {
                            id: accentOutlineWidthField
                            to: 100
                            unit: "px"
                            width: parent.width
                            decimals: 1
                            step: 0.5
                            from: 0
                            onEdited: v => root.setTextGroupKey("accent", "outlineWidth", v)
                        }
                    }
                }

                CollapsibleSection {
                    width: parent.width
                    title: qsTr("Accent pill")
                    tooltip: qsTr("Highlight only the accented words, instead of every word")
                    collapsible: false
                    showSeparator: false
                    showSwitch: true
                    switchChecked: root.textStyle.accent.highlight.enabled
                    switchTooltip: qsTr("Highlight only the accented words, instead of every word")
                    onSwitchToggled: on => root.setTextAccentHighlightKey("enabled", on)

                    ColorSwatchField {
                        hex: root.textStyle.accent.highlight.color
                        tooltip: qsTr("Choose accent highlight colour")
                        onEdited: value => root.setTextAccentHighlightKey("color", value)
                    }
                }
            }
        }

        CollapsibleSection {
            width: parent.width
            title: qsTr("Animation")
            expanded: false
            visible: root.hasTextStyle

            Row {
                width: parent.width
                spacing: 6

                Text {
                    text: qsTr("In")
                    width: 20
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    anchors.verticalCenter: parent.verticalCenter
                }
                ThemedComboBox {
                    id: animInKindBox
                    width: Math.max(40, (parent.width - 20 - parent.spacing * 3 - 56) / 2)
                    model: root.animKindLabels
                    currentIndex: Math.max(0, root.animKinds.indexOf(root.textStyle.animIn.kind))
                    onActivated: root.setTextAnim("animIn", "kind", root.animKinds[currentIndex])
                }
                ThemedComboBox {
                    id: animInEaseBox
                    width: Math.max(40, (parent.width - 20 - parent.spacing * 3 - 56) / 2)
                    model: root.easeLabels
                    currentIndex: Math.max(0, root.easeKinds.indexOf(root.textStyle.animIn.ease))
                    onActivated: root.setTextAnim("animIn", "ease", root.easeKinds[currentIndex])
                }
                ThemedNumberField {
                    id: animInDurationField
                    to: 60
                    unit: "s"
                    width: 56
                    decimals: 2
                    step: 0.05
                    from: 0
                    onEdited: v => root.setTextAnim("animIn", "duration", v)
                }
            }

            Row {
                width: parent.width
                spacing: 6

                Text {
                    text: qsTr("Out")
                    width: 20
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    anchors.verticalCenter: parent.verticalCenter
                }
                ThemedComboBox {
                    id: animOutKindBox
                    width: Math.max(40, (parent.width - 20 - parent.spacing * 3 - 56) / 2)
                    model: root.animKindLabels
                    currentIndex: Math.max(0, root.animKinds.indexOf(root.textStyle.animOut.kind))
                    onActivated: root.setTextAnim("animOut", "kind", root.animKinds[currentIndex])
                }
                ThemedComboBox {
                    id: animOutEaseBox
                    width: Math.max(40, (parent.width - 20 - parent.spacing * 3 - 56) / 2)
                    model: root.easeLabels
                    currentIndex: Math.max(0, root.easeKinds.indexOf(root.textStyle.animOut.ease))
                    onActivated: root.setTextAnim("animOut", "ease", root.easeKinds[currentIndex])
                }
                ThemedNumberField {
                    id: animOutDurationField
                    to: 60
                    unit: "s"
                    width: 56
                    decimals: 2
                    step: 0.05
                    from: 0
                    onEdited: v => root.setTextAnim("animOut", "duration", v)
                }
            }

            Row {
                width: parent.width
                spacing: 6

                Text {
                    text: qsTr("By")
                    width: 20
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    anchors.verticalCenter: parent.verticalCenter
                }
                ThemedComboBox {
                    id: animUnitBox
                    width: Math.max(40, (parent.width - 20 - parent.spacing * 3 - 56) / 2)
                    model: root.animUnitLabels
                    currentIndex: Math.max(0, root.animUnits.indexOf(root.textStyle.animIn.unit))
                    onActivated: root.setTextReveal("unit", root.animUnits[currentIndex])
                }
                ThemedComboBox {
                    id: animOrderBox
                    width: Math.max(40, (parent.width - 20 - parent.spacing * 3 - 56) / 2)
                    model: root.animOrderLabels
                    enabled: root.textStyle.animIn.unit !== "block"
                    currentIndex: Math.max(0, root.animOrders.indexOf(root.textStyle.animIn.order))
                    onActivated: root.setTextReveal("order", root.animOrders[currentIndex])
                }
                ThemedNumberField {
                    id: animStaggerField
                    to: 2
                    unit: "s"
                    width: 56
                    decimals: 2
                    step: 0.02
                    from: 0
                    enabled: root.textStyle.animIn.unit !== "block"
                    onEdited: v => root.setTextReveal("stagger", v)
                }
            }
        }
    }


    NameDialog {
        id: saveStyleDialog
        onSubmitted: name => EditorState.saveTextStyleAsPreset(EditorState.selectedTrack,
                                                               EditorState.selectedClip, name)
    }

    ColorDialog {
        id: styleColorDialog
        title: qsTr("Select Color")
        property string targetStyleKey: ""

        function colorToHex(c) {
            var toHex = function(v) {
                var h = Math.round(v * 255).toString(16);
                return h.length === 1 ? "0" + h : h;
            }
            return "#" + toHex(c.a) + toHex(c.r) + toHex(c.g) + toHex(c.b);
        }

        onAccepted: {
            if (targetStyleKey !== "") {
                root.setTextStyleKey(targetStyleKey, colorToHex(selectedColor))
            }
        }
    }
}
