import QtQuick
import QtQuick.Controls.Basic
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
                                                                       "layers": [],
                                                                       "lookId": "",
                                                                       "lookParams": ({}),
                                                                       "pathBend": 0,
                                                                       "align": "center",
                                                                       "valign": "middle",
                                                                       "wordWrap": true,
                                                                       "lineHeight": 1.2,
                                                                       "letterSpacing": 0,
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
                                                                       "animation": { "in": { "preset": "", "enabled": true, "custom": false },
                                                                                      "out": { "preset": "", "enabled": true, "custom": false },
                                                                                      "loop": { "preset": "", "enabled": true, "custom": false },
                                                                                      "custom": false }
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
    readonly property var accentRules: ["none", "firstWord", "lastWord", "everyOther", "everyNth",
                                        "longestWord", "randomStable", "karaoke"]
    readonly property var accentRuleLabels: ["No accent", "First word", "Last word", "Every other word",
                                             "Every Nth word", "Longest word", "Random words",
                                             "Spoken word (karaoke)"]

    // ----- Layers / looks --------------------------------------------------
    readonly property var layers: (textStyle && textStyle.layers) || []
    // The fill the Type section's colour swatch edits: the front-most enabled fill, or the
    // front-most fill at all when every fill is hidden.
    readonly property var frontFill: {
        let fallback = null
        for (let i = root.layers.length - 1; i >= 0; --i) {
            const l = root.layers[i]
            if (l.kind !== "fill")
                continue
            if (l.enabled !== false)
                return l
            if (!fallback)
                fallback = l
        }
        return fallback
    }
    readonly property string frontFillPaintKind: frontFill && frontFill.paint ? (frontFill.paint.kind || "solid") : "solid"
    readonly property string frontFillColor: {
        if (!root.frontFill || !root.frontFill.paint)
            return "#ffffffff"
        const p = root.frontFill.paint
        if (root.frontFillPaintKind === "gradient") {
            const stops = p.gradient && p.gradient.stops
            return stops && stops.length > 0 ? stops[0].color : "#ffffffff"
        }
        return p.color || "#ffffffff"
    }
    readonly property var looks: EditorState.textLooks()
    readonly property var selectedLook: {
        for (let i = 0; i < root.looks.length; ++i) {
            if (root.looks[i].id === root.textStyle.lookId)
                return root.looks[i]
        }
        return null
    }
    readonly property string lookSampleText: {
        const words = String((root.hasSelection && root.clipData.textContent) || "").trim().split(/\s+/)
        const sample = words.length > 0 ? words[0] : ""
        return sample.length > 0 ? sample : qsTr("Aa")
    }
    property var expandedLayerIds: ({})

    // ----- Animation ---------------------------------------------------------
    property string animSlot: "in"
    property string animCategory: ""
    readonly property var animPresets: {
        void root.animSlot
        return EditorState.textAnimationPresets(root.animSlot)
    }
    readonly property var animCategories: {
        void root.animSlot
        return EditorState.textAnimationCategories(root.animSlot)
    }
    readonly property var animTileModel: {
        const filtered = root.animCategory.length === 0
                         ? root.animPresets
                         : root.animPresets.filter(p => p.category === root.animCategory)
        return [{ "id": "", "label": qsTr("None") }].concat(filtered)
    }
    readonly property var animation: (textStyle && textStyle.animation) || ({})
    readonly property var slotData: animation[animSlot] || ({ "preset": "", "enabled": true, "custom": false })
    readonly property bool slotHasPreset: !!slotData.preset && slotData.preset.length > 0
    readonly property var selectedPreset: {
        for (let i = 0; i < root.animPresets.length; ++i) {
            if (root.animPresets[i].id === root.slotData.preset)
                return root.animPresets[i]
        }
        return null
    }
    readonly property var presetFlags: (selectedPreset && selectedPreset.flags) || ({})
    readonly property var commonAnimParamIds: ["duration", "stagger", "unit", "order", "ease", "period"]
    readonly property var presetOwnParams: selectedPreset
                                           ? (selectedPreset.params || []).filter(p => root.commonAnimParamIds.indexOf(p.id) < 0)
                                           : []
    readonly property var animUnits: ["block", "character", "word", "line"]
    readonly property var animUnitLabels: [qsTr("Whole block"), qsTr("Character"), qsTr("Word"), qsTr("Line")]
    readonly property var animOrders: ["forward", "backward", "centerOut", "random"]
    readonly property var animOrderLabels: [qsTr("Forward"), qsTr("Backward"), qsTr("Center out"), qsTr("Random")]
    readonly property var easeChips: [
        { "id": "linear", "label": qsTr("Linear") },
        { "id": "easeInOut", "label": qsTr("Smooth") },
        { "id": "easeOut", "label": qsTr("Snappy") },
        { "id": "back", "label": qsTr("Back") },
        { "id": "bounce", "label": qsTr("Bounce") }
    ]

    // Sub-page of the inspector: "text", "style" or "animate". Survives selection changes so a
    // pass over several clips stays on the page being worked in.
    property string textTab: "text"

    // The content field's user-dragged height and the id of the clip its text was loaded from,
    // so a commit can never land on a clip selected after the typing began.
    property real contentFieldHeight: 96
    property string contentClipId: ""
    readonly property bool contentDirty: root.clipKind === "text"
                                         && textContentField.text !== (root.clipData.textContent || "")

    function applyText() {
        if (!root.hasSelection || root.clipKind !== "text" || root.clipData.id !== root.contentClipId)
            return
        EditorState.commitTextEdit(EditorState.selectedTrack, EditorState.selectedClip, textContentField.text)
    }

    // Lands a freshly added text clip in its content field, where the inline editor used to open.
    function focusContent() {
        root.textTab = "text"
        textContentField.forceActiveFocus()
        textContentField.selectAll()
    }

    function setTextStyleKey(key, value) {
        const patch = {}
        patch[key] = value
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

    function setLayerExpanded(id, on) {
        const next = Object.assign({}, root.expandedLayerIds)
        if (on)
            next[id] = true
        else
            delete next[id]
        root.expandedLayerIds = next
    }

    function addLayer(kind) {
        const id = EditorState.addTextLayer(EditorState.selectedTrack, EditorState.selectedClip, kind)
        if (id && id.length > 0)
            root.setLayerExpanded(id, true)
    }

    function setSlot(patch) {
        EditorState.setTextAnimationSlot(EditorState.selectedTrack, EditorState.selectedClip, root.animSlot, patch)
    }
    function previewSlot(patch) {
        EditorState.previewSetTextAnimationSlot(EditorState.selectedTrack, EditorState.selectedClip, root.animSlot, patch)
    }
    function slotParamPatch(id, value) {
        const params = {}
        params[id] = value
        return { "params": params }
    }
    function seekToSlotStart() {
        EditorState.playheadSeconds = EditorState.textAnimationSlotStartSeconds(
                    EditorState.selectedTrack, EditorState.selectedClip, root.animSlot)
    }
    function pickPreset(id) {
        if (id.length === 0) {
            EditorState.clearTextAnimationSlot(EditorState.selectedTrack, EditorState.selectedClip, root.animSlot)
            return
        }
        root.setSlot({ "preset": id })
        root.seekToSlotStart()
    }
    // Plays the slot from its start and stops once the last unit has finished.
    function previewAnimation() {
        root.seekToSlotStart()
        const s = root.slotData
        const length = root.animSlot === "loop" ? (Number(s.period) || 1) : (Number(s.duration) || 0.4)
        previewStopTimer.interval = Math.round((length + 4 * (Number(s.stagger) || 0) + 0.5) * 1000)
        EditorState.playing = true
        previewStopTimer.restart()
    }
    function revertCustomAnimation() {
        const slots = ["in", "out", "loop"]
        for (let i = 0; i < slots.length; ++i) {
            const s = root.animation[slots[i]]
            if (s && s.custom)
                EditorState.clearTextAnimationSlot(EditorState.selectedTrack, EditorState.selectedClip, slots[i])
        }
    }

    height: contentCol.height
    implicitHeight: contentCol.height

    // Keyframed style scalars ride the generic keyframe API as "text.<key>"; the key list comes
    // back inside textStyle.keyframes with times already on the timeline, like a mask's. `def`
    // tracks the static scalar so an unkeyed row shows the value the clip actually renders with.
    function textKeyframes(key) {
        const keys = root.textStyle && root.textStyle.keyframes
        const entry = keys && keys[key]
        return (entry && entry.points) || []
    }
    function textProp(key, label, decimals) {
        return { "key": "text." + key, "label": label, "def": Number(root.textStyle[key]),
                 "decimals": decimals }
    }

    function refreshFields() {
        if (root.hasSelection) {
            // A different clip replaces the field outright, unapplied typing included — the same
            // rule as the subtitle cue editor. The same clip's edits leave focused typing alone.
            const id = root.clipData.id || ""
            if (id !== root.contentClipId) {
                root.contentClipId = id
                textContentField.text = root.clipData.textContent || ""
            } else if (!textContentField.activeFocus) {
                textContentField.text = root.clipData.textContent || ""
            }
        }
        if (!root.hasTextStyle)
            return
        const s = root.textStyle
        if (boxPaddingField && !boxPaddingField.activeFocus)
            boxPaddingField.value = s.boxPadding
        if (boxRadiusField && !boxRadiusField.activeFocus)
            boxRadiusField.value = s.boxRadius
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
    }

    Connections {
        target: EditorState
        function onSelectionChanged() { root.clipDataRevision++; root.refreshFields() }
        function onSelectedClipDataChanged() { root.clipDataRevision++; root.refreshFields() }
        function onTracksChanged() { root.clipDataRevision++; root.refreshFields() }
    }

    Component.onCompleted: refreshFields()

    Timer {
        id: previewStopTimer
        repeat: false
        onTriggered: EditorState.playing = false
    }

    Column {
        id: contentCol
        width: root.width
        spacing: Theme.spacingMd

        Row {
            id: textTabRow
            width: parent.width
            spacing: Theme.spacingSm

            Repeater {
                model: [
                    { "id": "text", "label": qsTr("Text") },
                    { "id": "style", "label": qsTr("Style") },
                    { "id": "animate", "label": qsTr("Animate") }
                ]
                delegate: ThemedToggleButton {
                    required property var modelData
                    width: (textTabRow.width - textTabRow.spacing * 2) / 3
                    text: modelData.label
                    checked: root.textTab === modelData.id
                    onClicked: root.textTab = modelData.id
                }
            }
        }

        Column {
            width: parent.width
            spacing: Theme.spacingMd
            visible: root.textTab === "text"

            CollapsibleSection {
                width: parent.width
                title: qsTr("Text")
                collapsible: false
                showSeparator: false
                visible: root.clipKind === "text"

                ThemedTextArea {
                    id: textContentField
                    width: parent.width
                    height: root.contentFieldHeight
                    clip: true
                    placeholderText: qsTr("Type your text…")
                    onEditingFinished: root.applyText()
                }

                // Drag grip: the field grows and shrinks with it.
                Item {
                    width: parent.width
                    height: 10

                    Rectangle {
                        anchors.centerIn: parent
                        width: 32
                        height: 3
                        radius: 1.5
                        color: gripArea.pressed || gripArea.containsMouse ? Theme.mutedForeground : Theme.panelBorder
                    }

                    MouseArea {
                        id: gripArea
                        anchors.fill: parent
                        anchors.topMargin: -4
                        anchors.bottomMargin: -4
                        hoverEnabled: true
                        preventStealing: true
                        cursorShape: Qt.SizeVerCursor
                        property real pressY: 0
                        property real pressHeight: 0
                        // Measured against the inspector, not the grip: the grip moves as the field grows.
                        onPressed: mouse => {
                            pressY = mapToItem(root, mouse.x, mouse.y).y
                            pressHeight = root.contentFieldHeight
                        }
                        onPositionChanged: mouse => {
                            if (!pressed)
                                return
                            const dy = mapToItem(root, mouse.x, mouse.y).y - pressY
                            root.contentFieldHeight = Math.max(56, Math.min(400, pressHeight + dy))
                        }
                    }
                }

                ThemedButton {
                    width: parent.width
                    variant: "primary"
                    glyph: Theme.icons.check
                    text: qsTr("Apply")
                    enabled: root.contentDirty
                    tooltip: qsTr("Apply the text to this clip")
                    onClicked: root.applyText()
                }
            }

            CollapsibleSection {
                width: parent.width
                title: qsTr("Font")
                collapsible: false
                visible: root.hasTextStyle

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

                    PropertyKeyframeRow {
                        width: (parent.width - parent.spacing) / 2
                        propDef: root.textProp("pixelSize", qsTr("Size"), 0)
                        keyframeList: root.textKeyframes("pixelSize")
                        useSlider: true
                        sliderFrom: 1
                        sliderTo: 500
                        unit: "px"
                    }
                }

                Row {
                    width: parent.width
                    spacing: 8

                    Column {
                        width: (parent.width - parent.spacing) / 2
                        spacing: 4
                        Text {
                            text: qsTr("Colour")
                            color: Theme.mutedForeground
                            font.pixelSize: Theme.fontSizeXs
                            font.family: Theme.fontFamily
                        }
                        ColorSwatchField {
                            visible: root.frontFillPaintKind === "solid" || root.frontFillPaintKind === "gradient"
                            hex: root.frontFillColor
                            tooltip: root.frontFillPaintKind === "gradient"
                                     ? qsTr("Choose the gradient's first colour")
                                     : qsTr("Choose text colour")
                            onEdited: value => root.setTextStyleKey("color", value)
                        }
                        Text {
                            visible: root.frontFillPaintKind === "gradient"
                            width: parent.width
                            text: qsTr("Edits the first gradient stop")
                            wrapMode: Text.WordWrap
                            color: Theme.mutedForeground
                            font.pixelSize: Theme.fontSizeXs
                            font.family: Theme.fontFamily
                        }
                        ThemedChip {
                            visible: root.frontFillPaintKind === "texture" || root.frontFillPaintKind === "effect"
                            text: qsTr("Edit in Style")
                            tooltip: root.frontFillPaintKind === "texture"
                                     ? qsTr("The text is painted with an image; change it on the Style page")
                                     : qsTr("The text is painted with an effect; change it on the Style page")
                            onClicked: {
                                root.textTab = "style"
                                if (root.frontFill)
                                    root.setLayerExpanded(root.frontFill.id, true)
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
            }

            CollapsibleSection {
                width: parent.width
                title: qsTr("Spacing")
                tooltip: qsTr("Line height, letter spacing, wrapping and bend")
                expanded: false
                visible: root.hasTextStyle

                Row {
                    width: parent.width
                    spacing: 8

                    PropertyKeyframeRow {
                        width: (parent.width - parent.spacing) / 2
                        propDef: root.textProp("lineHeight", qsTr("Line height"), 2)
                        keyframeList: root.textKeyframes("lineHeight")
                        useSlider: true
                        sliderFrom: 0.5
                        sliderTo: 4
                    }

                    PropertyKeyframeRow {
                        width: (parent.width - parent.spacing) / 2
                        propDef: root.textProp("letterSpacing", qsTr("Letter spacing"), 1)
                        keyframeList: root.textKeyframes("letterSpacing")
                        useSlider: true
                        sliderFrom: -100
                        sliderTo: 200
                        unit: "px"
                    }
                }

                Row {
                    width: parent.width
                    spacing: 8

                    Column {
                        width: (parent.width - parent.spacing) / 2
                        spacing: 4
                        Text {
                            text: qsTr("Wrapping")
                            color: Theme.mutedForeground
                            font.pixelSize: Theme.fontSizeXs
                            font.family: Theme.fontFamily
                        }
                        ThemedToggleButton {
                            width: 96
                            text: qsTr("Word wrap")
                            checked: root.textStyle.wordWrap
                            tooltip: qsTr("Wrap long lines inside the text box instead of overflowing")
                            onClicked: root.setTextStyleKey("wordWrap", !root.textStyle.wordWrap)
                        }
                    }

                    PropertyKeyframeRow {
                        width: (parent.width - parent.spacing) / 2
                        propDef: root.textProp("pathBend", qsTr("Bend"), 0)
                        keyframeList: root.textKeyframes("pathBend")
                        useSlider: true
                        sliderFrom: -100
                        sliderTo: 100
                    }
                }
            }
        }

        Column {
            width: parent.width
            spacing: Theme.spacingMd
            visible: root.textTab === "style" && root.hasTextStyle

            CollapsibleSection {
                width: parent.width
                title: qsTr("Preset")
                tooltip: qsTr("A whole text style — font, colour and effect — applied in one tap")
                collapsible: false
                showSeparator: false
                visible: root.hasTextStyle

                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: qsTr("Font, colour and effect in one tap. Save your own to reuse it.")
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
                    tooltip: qsTr("Save this text's style as a reusable preset")
                    onClicked: saveStyleDialog.openWith(qsTr("Save text style"),
                                                        qsTr("My style %1")
                                                            .arg(EditorState.userTextPresets().length + 1))
                }

                Row {
                    width: parent.width
                    spacing: Theme.spacingSm
                    visible: root.clipKind === "subtitle"

                    ThemedButton {
                        width: (parent.width - parent.spacing) * 0.6
                        variant: "secondary"
                        glyph: Theme.icons.captions
                        text: qsTr("Apply to all captions")
                        tooltip: qsTr("Copy this style to every other caption on this track")
                        onClicked: EditorState.applyTextStyleToCaptions(
                                       EditorState.selectedTrack, EditorState.selectedClip, "track")
                    }
                    ThemedButton {
                        width: (parent.width - parent.spacing) * 0.4
                        variant: "secondary"
                        text: qsTr("…every track")
                        tooltip: qsTr("Copy this style to every caption in the project")
                        onClicked: EditorState.applyTextStyleToCaptions(
                                       EditorState.selectedTrack, EditorState.selectedClip, "project")
                    }
                }
            }

            CollapsibleSection {
                width: parent.width
                title: qsTr("Effect")
                tooltip: qsTr("Shadow, outline, neon and friends — a recipe that builds the layers below")
                collapsible: false
                visible: root.hasTextStyle

                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: qsTr("Shadow, outline, neon… built as layers you can fine-tune below.")
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                TextLookPicker {
                    width: parent.width
                    lookId: root.textStyle.lookId || ""
                    sampleText: root.lookSampleText
                    fontFamily: root.textStyle.fontFamily || ""
                    fontWeight: Math.round(Number(root.textStyle.fontWeight) || 400)
                    italic: root.textStyle.italic === true
                    onLookPicked: id => EditorState.applyTextLook(EditorState.selectedTrack,
                                                                  EditorState.selectedClip, id)
                }

                TextParamSlots {
                    width: parent.width
                    visible: !!root.selectedLook && (root.selectedLook.params || []).length > 0
                    specs: root.selectedLook ? (root.selectedLook.params || []) : []
                    values: root.textStyle.lookParams || ({})
                    onChanged: (id, value) => EditorState.setTextLookParam(
                                   EditorState.selectedTrack, EditorState.selectedClip, id, value)
                    onPreviewChanged: (id, value) => EditorState.previewSetTextLookParam(
                                          EditorState.selectedTrack, EditorState.selectedClip, id, value)
                    onDragStarted: EditorState.beginPreviewDrag(qsTr("Adjust text look"))
                    onDragEnded: EditorState.commitPreviewDrag()
                }

                Row {
                    width: parent.width
                    spacing: Theme.spacingSm

                    Text {
                        width: parent.width - addLayerButton.width - parent.spacing
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Layers")
                        color: Theme.panelForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSm
                        font.weight: Font.DemiBold
                    }
                    ThemedButton {
                        id: addLayerButton
                        variant: "secondary"
                        glyph: Theme.icons.plus
                        text: qsTr("Add layer")
                        tooltip: qsTr("Add a fill, stroke, shadow, glow or extrude layer")
                        onClicked: addLayerMenu.popup()

                        ThemedContextMenu {
                            id: addLayerMenu
                            ThemedMenuItem { text: qsTr("Fill"); onTriggered: root.addLayer("fill") }
                            ThemedMenuItem { text: qsTr("Stroke"); onTriggered: root.addLayer("stroke") }
                            ThemedMenuItem { text: qsTr("Shadow"); onTriggered: root.addLayer("shadow") }
                            ThemedMenuItem { text: qsTr("Glow"); onTriggered: root.addLayer("glow") }
                            ThemedMenuItem { text: qsTr("Extrude"); onTriggered: root.addLayer("extrude") }
                        }
                    }
                }

                Text {
                    visible: root.layers.length === 0
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: qsTr("No layers. Pick an effect above or add a fill to start.")
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                // Integer model so rows survive the fresh QVariantList every edit produces; the list
                // is walked from the back so the front-most layer sits on top.
                Repeater {
                    model: root.layers.length
                    delegate: TextLayerRow {
                        required property int index
                        width: parent.width
                        layerData: root.layers[root.layers.length - 1 - index] || ({})
                        textStyle: root.textStyle
                        position: index
                        count: root.layers.length
                        expanded: root.expandedLayerIds[layerId] === true
                        onToggleRequested: root.setLayerExpanded(layerId, !expanded)
                    }
                }

                CollapsibleSection {
                    width: parent.width
                    title: qsTr("Decorations")
                    tooltip: qsTr("Boxes and rules drawn around the text rather than on it")
                    expanded: false

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
        }

        Column {
            width: parent.width
            spacing: Theme.spacingMd
            visible: root.textTab === "animate" && root.hasTextStyle

            Row {
                spacing: Theme.spacingSm

                Repeater {
                    model: [
                        { "id": "in", "label": qsTr("In") },
                        { "id": "out", "label": qsTr("Out") },
                        { "id": "loop", "label": qsTr("Loop") }
                    ]
                    delegate: ThemedToggleButton {
                        required property var modelData
                        width: 64
                        text: modelData.label
                        checked: root.animSlot === modelData.id
                        onClicked: {
                            root.animSlot = modelData.id
                            root.animCategory = ""
                        }
                    }
                }
            }

            Text {
                visible: root.clipKind === "subtitle"
                width: parent.width
                text: qsTr("Plays for every caption")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            Flickable {
                width: parent.width
                height: Theme.controlHeightSm
                contentWidth: categoryRow.width
                flickableDirection: Flickable.HorizontalFlick
                boundsBehavior: Flickable.StopAtBounds
                clip: true
                interactive: contentWidth > width
                visible: root.animCategories.length > 1

                Row {
                    id: categoryRow
                    height: parent.height
                    spacing: Theme.spacingSm

                    ThemedChip {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("All")
                        variant: "secondary"
                        selected: root.animCategory.length === 0
                        onClicked: root.animCategory = ""
                    }

                    Repeater {
                        model: root.animCategories
                        delegate: ThemedChip {
                            required property var modelData
                            required property int index
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.label
                            variant: "secondary"
                            accentColor: Theme.categoryColor(index)
                            selected: root.animCategory === modelData.id
                            onClicked: root.animCategory = modelData.id
                        }
                    }
                }
            }

            Grid {
                id: presetGrid
                width: parent.width
                readonly property real gap: Theme.spacingLg
                columns: Theme.touchUi
                         ? Math.max(2, Math.min(3, Math.floor((width + gap) / (104 + gap))))
                         : Math.max(2, Math.floor((width + gap) / (104 + gap)))
                columnSpacing: gap
                rowSpacing: gap
                readonly property real tileWidth: Math.floor((width - gap * (columns - 1)) / columns)
                Repeater {
                    model: root.animTileModel
                    delegate: TextAnimPresetTile {
                        required property var modelData
                        required property int index
                        slot: root.animSlot
                        presetId: modelData.id
                        label: modelData.label
                        tileWidth: presetGrid.tileWidth
                        selected: modelData.id.length === 0
                                  ? (!root.slotHasPreset && !root.slotData.custom)
                                  : root.slotData.preset === modelData.id
                        playing: root.textTab === "animate" && root.visible && !EditorState.playing
                                 && modelData.id.length > 0
                                 && (Theme.touchUi ? selected : hovered)
                        onClicked: root.pickPreset(modelData.id)
                    }
                }
            }

            // Selected preset's controls.
            Column {
                width: parent.width
                spacing: Theme.spacingMd
                visible: root.slotHasPreset && !!root.selectedPreset
                enabled: !root.slotData.custom

                Row {
                    width: parent.width
                    spacing: Theme.spacingSm

                    Text {
                        width: parent.width - previewButton.width - parent.spacing
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.selectedPreset ? root.selectedPreset.label : ""
                        elide: Text.ElideRight
                        color: Theme.panelForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSm
                        font.weight: Font.DemiBold
                    }
                    IconButton {
                        id: previewButton
                        glyph: EditorState.playing && previewStopTimer.running ? Theme.icons.pause : Theme.icons.play
                        variant: "ghost"
                        buttonSize: Theme.controlHeightSm
                        iconSize: 14
                        tooltip: qsTr("Preview this animation")
                        onClicked: {
                            if (EditorState.playing && previewStopTimer.running) {
                                previewStopTimer.stop()
                                EditorState.playing = false
                            } else {
                                root.previewAnimation()
                            }
                        }
                    }
                }

                Column {
                    width: parent.width
                    spacing: 4
                    visible: root.animSlot === "loop" || !root.presetFlags.durationLocked

                    Row {
                        width: parent.width
                        spacing: 6
                        Text {
                            text: root.animSlot === "loop" ? qsTr("Period") : qsTr("Duration")
                            color: Theme.mutedForeground
                            font.pixelSize: Theme.fontSizeXs
                            font.family: Theme.fontFamily
                        }
                        Text {
                            text: Number(durationSlider.value).toFixed(2) + " s"
                            color: Theme.mutedForeground
                            font.pixelSize: Theme.fontSizeXs
                            font.family: Theme.monoFontFamily
                        }
                    }
                    ThemedSlider {
                        id: durationSlider
                        width: parent.width
                        label: root.animSlot === "loop" ? qsTr("Period") : qsTr("Duration")
                        from: root.animSlot === "loop" ? 0.1 : 0
                        to: root.animSlot === "loop" ? 10 : 5
                        stepSize: 0.01
                        valueFormatter: function (v) { return Number(v).toFixed(2) + " s" }
                        Binding on value {
                            when: !durationSlider.pressed
                            value: root.animSlot === "loop"
                                   ? (Number(root.slotData.period) || 1)
                                   : (Number(root.slotData.duration) || 0)
                        }
                        onMoved: {
                            const patch = {}
                            patch[root.animSlot === "loop" ? "period" : "duration"] = value
                            if (pressed)
                                root.previewSlot(patch)
                            else
                                root.setSlot(patch)
                        }
                        onPressedChanged: {
                            if (pressed)
                                EditorState.beginPreviewDrag(qsTr("Edit text animation"))
                            else
                                EditorState.commitPreviewDrag()
                        }
                    }
                }

                Row {
                    width: parent.width
                    spacing: 8

                    Column {
                        width: (parent.width - parent.spacing) / 2
                        spacing: 4
                        visible: !root.presetFlags.unitLocked
                        Text {
                            text: qsTr("By")
                            color: Theme.mutedForeground
                            font.pixelSize: Theme.fontSizeXs
                            font.family: Theme.fontFamily
                        }
                        ThemedComboBox {
                            width: parent.width
                            model: root.animUnitLabels
                            currentIndex: Math.max(0, root.animUnits.indexOf(root.slotData.unit || "block"))
                            onActivated: root.setSlot({ "unit": root.animUnits[currentIndex] })
                        }
                    }

                    Column {
                        width: (parent.width - parent.spacing) / 2
                        spacing: 4
                        Text {
                            text: root.animSlot === "loop" ? qsTr("Phase") : qsTr("Stagger")
                            HoverHandler { id: staggerHover }
                            ThemedToolTip {
                                text: root.animSlot === "loop"
                                      ? qsTr("Delay between one unit and the next along the cycle")
                                      : qsTr("Delay between one unit starting and the next")
                                visible: staggerHover.hovered
                            }
                            color: Theme.mutedForeground
                            font.pixelSize: Theme.fontSizeXs
                            font.family: Theme.fontFamily
                        }
                        ThemedNumberField {
                            id: staggerField
                            width: parent.width
                            decimals: 0
                            step: 10
                            from: 0
                            to: 2000
                            unit: "ms"
                            enabled: (root.slotData.unit || "block") !== "block"
                            Binding on value {
                                when: !staggerField.activeFocus
                                value: Math.round((Number(root.slotData.stagger) || 0) * 1000)
                            }
                            onEdited: v => root.setSlot({ "stagger": v / 1000 })
                        }
                    }
                }

                Column {
                    width: (parent.width - 8) / 2
                    spacing: 4
                    visible: !root.presetFlags.orderLocked
                    Text {
                        text: qsTr("Order")
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                    ThemedComboBox {
                        width: parent.width
                        model: root.animOrderLabels
                        enabled: (root.slotData.unit || "block") !== "block"
                        currentIndex: Math.max(0, root.animOrders.indexOf(root.slotData.order || "forward"))
                        onActivated: root.setSlot({ "order": root.animOrders[currentIndex] })
                    }
                }

                Column {
                    width: parent.width
                    spacing: 4
                    visible: !root.presetFlags.easeLocked
                    Text {
                        text: qsTr("Ease")
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                    Flow {
                        width: parent.width
                        spacing: Theme.spacingSm
                        Repeater {
                            model: root.easeChips
                            delegate: ThemedChip {
                                required property var modelData
                                text: modelData.label
                                selected: (root.slotData.ease || "") === modelData.id
                                onClicked: root.setSlot({ "ease": modelData.id })
                            }
                        }
                    }
                }

                TextParamSlots {
                    width: parent.width
                    visible: root.presetOwnParams.length > 0
                    specs: root.presetOwnParams
                    values: root.slotData.params || ({})
                    onChanged: (id, value) => root.setSlot(root.slotParamPatch(id, value))
                    onPreviewChanged: (id, value) => root.previewSlot(root.slotParamPatch(id, value))
                    onDragStarted: EditorState.beginPreviewDrag(qsTr("Edit text animation"))
                    onDragEnded: EditorState.commitPreviewDrag()
                }
            }

            CollapsibleSection {
                width: parent.width
                title: qsTr("Advanced")
                expanded: false
                visible: root.animation.custom === true

                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: qsTr("Custom animator (set via MCP). Preset controls are disabled.")
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
                ThemedButton {
                    variant: "secondary"
                    glyph: Theme.icons.reset
                    text: qsTr("Revert to preset")
                    tooltip: qsTr("Drop the custom animators and go back to picking presets")
                    onClicked: root.revertCustomAnimation()
                }
            }
        }
    }

    NameDialog {
        id: saveStyleDialog
        onSubmitted: name => EditorState.saveTextStyleAsPreset(EditorState.selectedTrack,
                                                               EditorState.selectedClip, name)
    }
}
