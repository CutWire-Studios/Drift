import QtQuick
import Drift

// The timeline toolbar's action catalogue and layout: what each customisable action is
// called and drawn with, the default arrangement, and the stored layout cleaned against
// it. Shared by TimelineToolbar and the toolbar customizer in Settings.
QtObject {
    id: actions

    // Every customisable edit action, keyed by the id stored in the toolbar layout.
    // `tip` is the longer toolbar tooltip where the menu label alone would be too terse.
    readonly property var actionMeta: ({
        "select": { glyph: Theme.icons.mousePointer, label: qsTr("Select"),
                    tip: qsTr("Select — normal editing"), shortcut: "selectTool" },
        "cut": { glyph: Theme.icons.scissors, label: qsTr("Cut mode"),
                 tip: qsTr("Cut mode — click a clip to split it"), shortcut: "bladeTool" },
        "slip": { glyph: Theme.icons.chevronsRightLeft, label: qsTr("Slip mode"),
                  tip: qsTr("Slip mode — drag within clip boundaries to shift media (Alt+drag)"), shortcut: "slipTool" },
        "separateAudio": { glyph: Theme.icons.audioLines, label: qsTr("Show audio on separate track"),
                           shortcut: "separateAudio" },
        "unlink": { glyph: Theme.icons.unlink, label: qsTr("Unlink video and audio"),
                    shortcut: "unlink" },
        "link": { glyph: Theme.icons.link, label: qsTr("Link selected clips"), shortcut: "link" },
        "trimStart": { glyph: Theme.icons.trimStart, label: qsTr("Trim start"),
                       tip: qsTr("Trim start — click a clip to drop everything left of the cut") },
        "trimEnd": { glyph: Theme.icons.trimEnd, label: qsTr("Trim end"),
                     tip: qsTr("Trim end — click a clip to drop everything right of the cut") },
        "deleteLeft": { glyph: Theme.icons.trimStart, label: qsTr("Trim start to playhead"),
                        shortcut: "deleteLeft" },
        "deleteRight": { glyph: Theme.icons.trimEnd, label: qsTr("Trim end to playhead"),
                         shortcut: "deleteRight" },
        "undo": { glyph: Theme.icons.undo, label: qsTr("Undo"), shortcut: "undo" },
        "redo": { glyph: Theme.icons.redo, label: qsTr("Redo"), shortcut: "redo" },
        "delete": { glyph: Theme.icons.trash, label: qsTr("Delete clip"), shortcut: "delete" },
        "copy": { glyph: Theme.icons.copy, label: qsTr("Copy selection"), shortcut: "copy" },
        "paste": { glyph: Theme.icons.clipboardPaste, label: qsTr("Paste at current time"),
                   shortcut: "paste" },
        "duplicate": { glyph: Theme.icons.copyPlus, label: qsTr("Duplicate clip"),
                       shortcut: "duplicate" },
        "bookmark": { glyph: Theme.icons.bookmark, label: qsTr("Add/remove bookmark at current time"),
                      shortcut: "toggleBookmark" },
        "markIn": { glyph: Theme.icons.setStart, label: qsTr("Mark work area in"), shortcut: "markIn" },
        "markOut": { glyph: Theme.icons.setEnd, label: qsTr("Mark work area out"), shortcut: "markOut" },
        "loop": { glyph: Theme.icons.repeat, label: qsTr("Loop work area playback"),
                  shortcut: "toggleLoop" },
        "clearWorkArea": { glyph: Theme.icons.x, label: qsTr("Clear work area"),
                           shortcut: "clearInOut" },
        "merge": { glyph: Theme.icons.linkTwo, label: qsTr("Merge adjacent clips"), shortcut: "merge" },
        "freeze": { glyph: Theme.icons.snowflake, label: qsTr("Freeze frame at current time") },
        "adjustmentLayer": { glyph: Theme.icons.wand, label: qsTr("Add adjustment layer") },
        "transformLayer": { glyph: Theme.icons.group, label: qsTr("Add transform layer"),
                            tip: qsTr("Add transform layer — moves the selected clips' tracks as one"),
                            shortcut: "transformTogether" },
        "camera": { glyph: Theme.icons.video, label: qsTr("Add camera"),
                    tip: qsTr("Add camera — one viewpoint the whole timeline is seen through. With a camera already there, adds a new camera clip (a cut to a new framing) at the playhead") }
    })

    readonly property var defaultToolbarItems: [
        "select", "cut", "deleteLeft", "deleteRight", "separator", "undo", "redo", "delete", "separator",
        "separateAudio", "unlink", "link"
    ]
    readonly property var defaultMenuItems: [
        "trimStart", "trimEnd", "separator", "copy", "paste", "duplicate", "separator",
        "bookmark", "markIn", "markOut", "loop", "clearWorkArea", "separator",
        "merge", "freeze", "adjustmentLayer", "transformLayer", "camera"
    ]

    // The stored layout, cleaned: unknown or repeated ids are dropped and any action missing
    // from both lists (one added in a later version) lands at the end of the menu.
    readonly property var layout: {
        const storedToolbar = EditorState.preferences.timelineToolbarItems
        const storedMenu = EditorState.preferences.timelineMenuItems
        if (storedToolbar.length === 0 && storedMenu.length === 0)
            return { toolbar: defaultToolbarItems, menu: defaultMenuItems }
        const seen = {}
        const clean = (list) => list.filter((id) => {
            if (id === "separator")
                return true
            if (!actionMeta[id] || seen[id])
                return false
            seen[id] = true
            return true
        })
        const toolbarItems = clean(storedToolbar)
        const menuItems = clean(storedMenu)
        for (const id of Object.keys(actionMeta)) {
            if (!seen[id])
                menuItems.push(id)
        }
        return { toolbar: toolbarItems, menu: menuItems }
    }
}
