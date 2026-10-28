import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift

// Results for the header search field. The field keeps focus and forwards Up/Down/Enter here;
// the popup never takes focus itself. The index is rebuilt on every open, so addon installs and
// new presets show up without listening to each catalog's change signal.
Popup {
    id: root

    // [{label, sub, run}] for things only the header can do (export, downloads, language…).
    property var headerCommands: []
    property int highlightIndex: 0
    property var results: []

    signal finished()

    padding: Theme.spacingMd
    modal: false
    dim: false
    focus: false
    closePolicy: Popup.CloseOnPressOutsideParent

    property var _index: []
    readonly property GlobalSearchControls _controls: GlobalSearchControls { }

    readonly property var _kindLabels: ({
        "action": qsTr("Action"),
        "command": qsTr("Command"),
        "panel": qsTr("Panel"),
        "effect": qsTr("Effect"),
        "template": qsTr("Template"),
        "stack": qsTr("Effect stack"),
        "audioEffect": qsTr("Audio FX"),
        "transition": qsTr("Transition"),
        "mask": qsTr("Mask"),
        "shape": qsTr("Shape"),
        "sticker": qsTr("Sticker"),
        "textStyle": qsTr("Text style"),
        "addon": qsTr("Add-on"),
        "control": qsTr("Control")
    })

    // Actions that would do nothing in the current state. Ids not listed are always offered.
    function _actionEnabled(id) {
        const hasClip = EditorState.selectedClip >= 0
        switch (id) {
        case "undo": return EditorState.undoAvailable
        case "redo": return EditorState.redoAvailable
        case "merge": return EditorState.mergeAvailable
        case "separateAudio": return EditorState.separateAudioAvailable
        case "transformTogether": return EditorState.transformTogetherAvailable
        case "unlink": return EditorState.unlinkAvailable
        case "link": return EditorState.linkAvailable
        case "pasteAttributes": return EditorState.canPasteAttributes()
        case "delete":
        case "copy":
        case "cut":
        case "duplicate":
        case "copyEffects":
        case "pasteEffects":
        case "deleteLeft":
        case "deleteRight":
        case "nudgeLeft":
        case "nudgeRight":
        case "speedUp":
        case "speedDown":
            return hasClip
        }
        return true
    }

    function _workspaceRows() {
        const workspaces = Workspaces.workspaces
        const rows = []
        for (let i = 0; i < workspaces.length; ++i) {
            const ws = workspaces[i]
            rows.push({ kind: "command", label: qsTr("Workspace: %1").arg(ws.name),
                        sub: ws.id === Workspaces.activeId ? qsTr("Current") : "",
                        keywords: "workspace layout " + ws.id, shortcut: "", enabled: true,
                        run: () => { Workspaces.activeId = ws.id } })
        }
        return rows
    }

    function buildIndex() {
        // Popup is not an Item, so the Window attached property has to come from its parent.
        const win = ShellBridge.main
        const c = root._controls
        const rows = []
        const selectClip = qsTr("Select a clip first")
        const t = EditorState.selectedTrack
        const ci = EditorState.selectedClip
        const hasClip = ci >= 0
        const clipKind = hasClip ? (EditorState.selectedClipData.kind || "") : ""
        const applyHint = qsTr("Apply to selected clip")
        const addHint = qsTr("Add at playhead")

        const actions = EditorState.actions || []
        for (let i = 0; i < actions.length; ++i) {
            const a = actions[i]
            if (a.id === "search")
                continue
            const enabled = root._actionEnabled(a.id)
            rows.push({ kind: "action", label: a.label, sub: enabled ? "" : qsTr("Not available now"),
                        keywords: a.id, shortcut: a.shortcut ? Theme.shortcutDisplay(a.shortcut) : "",
                        enabled: enabled, run: () => win.dispatchAction(a.id) })
        }

        const commands = root.headerCommands.concat([
            { label: qsTr("Settings"), keywords: "preferences options", run: () => win.openSettings() },
            { label: qsTr("Extras"), keywords: "add-ons addons packs", run: () => win.openExtras() },
            { label: qsTr("Add-on manager"), keywords: "addons install packs extras", run: () => win.openAddonManager() },
            { label: qsTr("Debug info…"), keywords: "diagnostics", run: () => win.openDebugInfo() },
            { label: qsTr("Light theme"), keywords: "appearance theme", run: () => Theme.setDarkMode(false) },
            { label: qsTr("Dark theme"), keywords: "appearance theme", run: () => Theme.setDarkMode(true) },
            { label: qsTr("System theme"), keywords: "appearance theme", run: () => ThemeManager.select("system") },
        ].concat((ThemeManager.themes || []).filter((theme) => theme && !theme.builtIn).map((theme) => ({
            label: qsTr("Theme: %1").arg(theme.name),
            keywords: "appearance theme " + (theme.name || ""),
            run: () => {
                if (ThemeManager.select(theme.id) && ThemeManager.lastWarning)
                    Toasts.warning(ThemeManager.lastWarning)
            }
        }))))
        for (let i = 0; i < commands.length; ++i) {
            const cmd = commands[i]
            rows.push({ kind: "command", label: cmd.label, sub: cmd.sub || "", keywords: cmd.keywords || "",
                        shortcut: "", enabled: true, run: cmd.run })
        }

        rows.push(...root._workspaceRows())

        const tabs = ["media", "market", "text", "subtitles", "speech", "stickers", "shapes", "masks",
                      "scenes", "effects", "templates", "transitions", "sounds", "shortcuts"]
        for (let i = 0; i < tabs.length; ++i) {
            const tabId = tabs[i]
            const label = win.assetsTabLabel(tabId)
            rows.push({ kind: "panel", label: label, sub: c.assets + " › " + label, keywords: tabId,
                        shortcut: "", enabled: true, run: () => win.showAssetsTab(tabId) })
        }

        const effects = EditorState.effectCatalog()
        for (let i = 0; i < effects.length; ++i) {
            const e = effects[i]
            rows.push({ kind: "effect", label: e.displayName || e.label, sub: e.categoryLabel || "",
                        hint: hasClip ? applyHint : qsTr("Add as adjustment clip"), keywords: e.id,
                        shortcut: "", enabled: true,
                        run: () => {
                            if (EditorState.selectedClip < 0)
                                EditorState.addAdjustmentClipWithEffect(e.id, -1, -1)
                            else
                                EditorState.addEffect(EditorState.selectedTrack, EditorState.selectedClip, e.id)
                        } })
        }

        const templates = EditorState.effectTemplateCatalog()
        for (let i = 0; i < templates.length; ++i) {
            const e = templates[i]
            rows.push({ kind: "template", label: e.displayName || e.label, sub: e.categoryLabel || "",
                        hint: hasClip ? applyHint : selectClip, keywords: e.id, shortcut: "", enabled: hasClip,
                        run: () => EditorState.applyEffectTemplate(t, ci, e.id) })
        }

        const stacks = EditorState.userEffectPresets()
        for (let i = 0; i < stacks.length; ++i) {
            const e = stacks[i]
            rows.push({ kind: "stack", label: e.label, sub: (e.labels || []).join(", "),
                        hint: hasClip ? applyHint : selectClip, keywords: "preset", shortcut: "",
                        enabled: hasClip, run: () => EditorState.applyEffectPreset(t, ci, e.id) })
        }

        const audioEffects = EditorState.audioEffectCatalog()
        for (let i = 0; i < audioEffects.length; ++i) {
            const e = audioEffects[i]
            rows.push({ kind: "audioEffect", label: e.displayName || e.label, sub: e.categoryLabel || "",
                        hint: hasClip ? applyHint : selectClip, keywords: e.id, shortcut: "", enabled: hasClip,
                        run: () => EditorState.addAudioEffect(t, ci, e.id) })
        }

        // A transition goes on the cut after the selected clip.
        const canTransition = hasClip && EditorState.hasTransitionPartner(t, ci)
        const fitting = {}
        if (canTransition) {
            const kinds = EditorState.transitionKindsForTrack(t)
            for (let i = 0; i < kinds.length; ++i)
                fitting[kinds[i].kind] = true
        }
        const transitions = EditorState.transitionKinds()
        for (let i = 0; i < transitions.length; ++i) {
            const e = transitions[i]
            const enabled = canTransition && fitting[e.kind] === true
            rows.push({ kind: "transition", label: e.label, sub: "",
                        hint: enabled ? qsTr("Add after selected clip")
                                      : (canTransition ? qsTr("Not available on this track")
                                                       : c.transitionClip),
                        keywords: e.kind, shortcut: "", enabled: enabled,
                        run: () => EditorState.addTransition(t, ci, e.kind, EditorState.preferences.transitionDurationSeconds) })
        }

        // Same rule as MasksTab: audio has no picture, text and subtitles are styled, not masked.
        const maskable = hasClip && clipKind !== "audio" && clipKind !== "text" && clipKind !== "subtitle"
        const masks = EditorState.maskCatalog()
        for (let i = 0; i < masks.length; ++i) {
            const e = masks[i]
            rows.push({ kind: "mask", label: e.label, sub: "",
                        hint: maskable ? applyHint : c.visualClip, keywords: e.id, shortcut: "",
                        enabled: maskable, run: () => EditorState.addMaskToClip(t, ci, e.id) })
        }

        const shapes = EditorState.builtinShapes()
        for (let i = 0; i < shapes.length; ++i) {
            const e = shapes[i]
            rows.push({ kind: "shape", label: e.label, sub: "", hint: addHint, keywords: e.id, shortcut: "",
                        enabled: true, run: () => EditorState.addShapeClip(e.id, -1) })
        }

        const stickers = EditorState.builtinStickers()
        for (let i = 0; i < stickers.length; ++i) {
            const e = stickers[i]
            rows.push({ kind: "sticker", label: e.label, sub: "", hint: addHint, keywords: e.id, shortcut: "",
                        enabled: true, run: () => EditorState.addStickerClip(e.id, -1) })
        }

        const textStyles = EditorState.textPresets().concat(EditorState.userTextPresets())
        for (let i = 0; i < textStyles.length; ++i) {
            const e = textStyles[i]
            rows.push({ kind: "textStyle", label: e.label, sub: "", hint: addHint, keywords: "text title",
                        shortcut: "", enabled: true, run: () => EditorState.addTextClip("", -1, e.id) })
        }

        const addons = Addons.catalog || []
        for (let i = 0; i < addons.length; ++i) {
            const e = addons[i]
            if (e.state !== "installed" && e.state !== "update-available")
                continue
            const place = c.addonPlaces[e.kind] || c.addonFallback
            rows.push({ kind: "addon", label: e.name, sub: place.join(" › "), hint: "",
                        keywords: (e.kind || "") + " " + (e.description || ""), shortcut: "",
                        enabled: true, run: null })
        }

        const controls = c.entries
        for (let i = 0; i < controls.length; ++i) {
            const e = controls[i]
            rows.push({ kind: "control", label: e.label, sub: e.path.concat([e.label]).join(" › "),
                        hint: e.hint, keywords: e.keywords, shortcut: "", enabled: true,
                        run: e.page ? () => win.openSettings(e.page)
                                    : (e.workspace ? () => {
                                           Workspaces.activeId = e.workspace
                                           if (e.colorTab !== undefined)
                                               ShellBridge.colorTabRequest = ({ tab: e.colorTab })
                                       } : null) })
        }

        // Lowercased once here so each keystroke only scores; _h is the whole-row haystack for the word pass.
        for (let i = 0; i < rows.length; ++i) {
            const r = rows[i]
            r._l = (r.label || "").toLowerCase()
            r._k = (r.keywords || "").toLowerCase()
            r._s = (r.sub || "").toLowerCase()
            r._h = (r.label + " " + r.keywords + " " + r.sub).toLowerCase()
        }

        root._index = rows
    }

    // Exact > prefix > word start > substring > in-order letters. -1 is no match.
    // "s" is already lowercased (see buildIndex) and "q" is the lowercased query.
    function _score(s, q) {
        if (s.length === 0)
            return -1
        if (s === q)
            return 1000
        const at = s.indexOf(q)
        if (at === 0)
            return 800 - s.length
        if (at > 0)
            return (/[\s\-_(›/]/.test(s[at - 1]) ? 600 : 400) - at
        let pos = 0
        let gaps = 0
        for (let i = 0; i < q.length; ++i) {
            const found = s.indexOf(q[i], pos)
            if (found < 0)
                return -1
            gaps += found - pos
            pos = found + 1
        }
        return Math.max(1, 200 - gaps * 5)
    }

    function search(query) {
        const q = query.trim().toLowerCase()
        if (q.length === 0) {
            results = _workspaceRows()
            highlightIndex = 0
            list.positionViewAtBeginning()
            if (!opened)
                open()
            return
        }
        if (_index.length === 0)
            buildIndex()
        const words = q.split(/\s+/)
        const scored = []
        for (let i = 0; i < _index.length; ++i) {
            const row = _index[i]
            let score = Math.max(_score(row._l, q),
                                 _score(row._k, q) * 0.5,
                                 _score(row._s, q) * 0.4)
            // "blur gaussian" or "transform opacity": every word somewhere in the row.
            if (score < 0 && words.length > 1) {
                if (words.every(w => row._h.indexOf(w) >= 0))
                    score = 300
            }
            if (score < 0)
                continue
            if (row.enabled && row.run)
                score += 50
            scored.push({ row: row, score: score })
        }
        scored.sort((a, b) => b.score - a.score)
        results = scored.slice(0, 50).map(s => s.row)
        highlightIndex = 0
        list.positionViewAtBeginning()
        if (!opened)
            open()
    }

    function moveHighlight(step) {
        if (results.length === 0)
            return
        highlightIndex = (highlightIndex + step + results.length) % results.length
        list.positionViewAtIndex(highlightIndex, ListView.Contain)
    }

    function activate(index) {
        const row = results[index]
        if (!row || !row.run || !row.enabled)
            return
        close()
        row.run()
        root.finished()
    }

    onClosed: _index = []

    background: Rectangle {
        color: Theme.c.globalSearchPopup.background
        border.width: Theme.c.globalSearchPopup.borderWidth
        border.color: Theme.c.globalSearchPopup.border
        radius: Theme.c.globalSearchPopup.radius
    }

    contentItem: Item {
        implicitHeight: root.results.length > 0 ? Math.min(list.contentHeight, 420) : empty.implicitHeight

        Text {
            id: empty
            visible: root.results.length === 0
            width: parent.width
            padding: Theme.spacingLg
            text: qsTr("No matches")
            color: Theme.c.globalSearchPopup.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
        }

        ListView {
            id: list
            anchors.fill: parent
            clip: true
            spacing: 2
            boundsBehavior: Flickable.StopAtBounds
            model: root.results
            ScrollBar.vertical: AppScrollBar { }

            delegate: GlobalSearchResultRow {
                highlightIndex: root.highlightIndex
                kindLabels: root._kindLabels
                onActivated: (i) => root.activate(i)
            }
        }
    }
}
