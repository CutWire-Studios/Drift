import QtQuick
import QtQuick.Controls.Basic
import Drift
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

    // "" shows the tabbed inspector. "style", "settings" and "animate" show just that slice with
    // the tab bar hidden, for the phone tool pages.
    property string mode: ""
    readonly property bool showContent: mode === "" && textTab === "text"
    readonly property bool showFont: (mode === "" && textTab === "text") || mode === "style" || mode === "settings"
    readonly property bool showSpacing: (mode === "" && textTab === "text") || mode === "settings"
    readonly property bool showStyle: mode === "style" || (mode === "" && textTab === "style")
    readonly property bool showAnimate: mode === "animate" || (mode === "" && textTab === "animate")

    // The content field's user-dragged height and the id of the clip its text was loaded from,
    // so a commit can never land on a clip selected after the typing began.
    property real contentFieldHeight: 96
    property string contentClipId: ""
    readonly property bool contentDirty: root.clipKind === "text"
                                         && textPage.contentField.text !== (root.clipData.textContent || "")

    readonly property bool animPreviewRunning: previewStopTimer.running
    function stopAnimPreview() {
        previewStopTimer.stop()
        EditorState.playing = false
    }
    function openSaveStyle() {
        saveStyleDialog.openWith(qsTr("Save text style"),
                                 qsTr("My style %1").arg(EditorState.userTextPresets().length + 1))
    }
    function openVoiceover() {
        voiceoverDialog.openFor(EditorState.selectedTrack, EditorState.selectedClip)
    }

    function applyText() {
        if (!root.hasSelection || root.clipKind !== "text" || root.clipData.id !== root.contentClipId)
            return
        EditorState.commitTextEdit(EditorState.selectedTrack, EditorState.selectedClip, textPage.contentField.text)
    }

    // Lands a freshly added text clip in its content field, where the inline editor used to open.
    function focusContent() {
        root.textTab = "text"
        textPage.contentField.forceActiveFocus()
        textPage.contentField.selectAll()
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
                textPage.contentField.text = root.clipData.textContent || ""
            } else if (!textPage.contentField.activeFocus) {
                textPage.contentField.text = root.clipData.textContent || ""
            }
        }
        if (!root.hasTextStyle)
            return
        const s = root.textStyle
        if (stylePage.boxPaddingField && !stylePage.boxPaddingField.activeFocus)
            stylePage.boxPaddingField.value = s.boxPadding
        if (stylePage.boxRadiusField && !stylePage.boxRadiusField.activeFocus)
            stylePage.boxRadiusField.value = s.boxRadius
        if (stylePage.wordHighlightPaddingField && !stylePage.wordHighlightPaddingField.activeFocus)
            stylePage.wordHighlightPaddingField.value = s.wordHighlight.padding
        if (stylePage.wordHighlightRadiusField && !stylePage.wordHighlightRadiusField.activeFocus)
            stylePage.wordHighlightRadiusField.value = s.wordHighlight.radius
        if (stylePage.underlineWidthField && !stylePage.underlineWidthField.activeFocus)
            stylePage.underlineWidthField.value = s.underlineWidth
        if (stylePage.underlineOffsetField && !stylePage.underlineOffsetField.activeFocus)
            stylePage.underlineOffsetField.value = s.underlineOffset
        if (stylePage.accentEveryNField && !stylePage.accentEveryNField.activeFocus)
            stylePage.accentEveryNField.value = s.accent.n
        if (stylePage.accentSizeScaleField && !stylePage.accentSizeScaleField.activeFocus)
            stylePage.accentSizeScaleField.value = s.accent.sizeScale
        if (stylePage.accentOutlineWidthField && !stylePage.accentOutlineWidthField.activeFocus)
            stylePage.accentOutlineWidthField.value = s.accent.outlineWidth
    }

    Connections {
        target: EditorState
        function onSelectionChanged() { root.clipDataRevision++; root.refreshFields() }
        function onSelectedClipDataChanged() { root.clipDataRevision++; root.refreshFields() }
        function onTracksChanged() { root.clipDataRevision++; root.refreshFields() }
    }

    Component.onCompleted: refreshFields()
    // The Properties panel unloads this tab when another is picked; land typed text first.
    Component.onDestruction: {
        if (contentDirty)
            applyText()
    }

    Timer {
        id: previewStopTimer
        repeat: false
        onTriggered: EditorState.playing = false
    }

    Column {
        id: contentCol
        width: root.width
        spacing: Theme.spacingMd

        TextInspectorTabBar {
            width: parent.width
            visible: root.mode === ""
            textTab: root.textTab
            onTabSelected: function(tabId) { root.textTab = tabId }
        }

        TextInspectorTextPage {
            id: textPage
            width: parent.width
            inspector: root
        }

        TextInspectorStylePage {
            id: stylePage
            width: parent.width
            inspector: root
        }

        TextInspectorAnimatePage {
            id: animatePage
            width: parent.width
            inspector: root
        }
    }

    NameDialog {
        id: saveStyleDialog
        onSubmitted: name => EditorState.saveTextStyleAsPreset(EditorState.selectedTrack,
                                                               EditorState.selectedClip, name)
    }

    VoiceoverDialog {
        id: voiceoverDialog
    }
}
