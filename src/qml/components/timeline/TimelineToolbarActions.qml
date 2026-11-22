import QtQuick
import Drift

// The timeline toolbar's action catalogue and layout: what each customisable action is
// called and drawn with, the default arrangement, and the stored layout cleaned against
// it. Shared by TimelineToolbar and the toolbar customizer in Settings.
QtObject {
    id: actions

    readonly property Component illToolSelect: Component { ToolSelectIllustration { } }
    readonly property Component illToolCut: Component { ToolCutIllustration { } }
    readonly property Component illLinkClips: Component { LinkClipsIllustration { } }
    readonly property Component illTrimStart: Component { TrimStartIllustration { } }
    readonly property Component illTrimEnd: Component { TrimEndIllustration { } }
    readonly property Component illFreezeFrame: Component { FreezeFrameIllustration { } }
    readonly property Component illAdjustmentLayer: Component { AdjustmentLayerIllustration { } }
    readonly property Component illTransformLayer: Component { TransformLayerIllustration { } }

    // Every customisable edit action, keyed by the id stored in the toolbar layout.
    // `body` is the one-line explanation shown under the label in the toolbar tooltip.
    readonly property var actionMeta: ({
        "select": { glyph: Theme.icons.mousePointer, label: qsTr("Select"), shortcut: "selectTool",
                    body: qsTr("Click, drag and trim clips as normal. Switch back here after using Cut."),
                    illustration: illToolSelect },
        "cut": { glyph: Theme.icons.scissors, label: qsTr("Cut mode"), shortcut: "bladeTool",
                    body: qsTr("Click a clip to split it there. Fast for chopping out the parts you do not need."),
                    illustration: illToolCut },
        "separateAudio": { glyph: Theme.icons.audioLines, label: qsTr("Show audio on separate track"),
                           shortcut: "separateAudio",
                    body: qsTr("Pull the sound out of a video onto its own track to edit it separately.") },
        "unlink": { glyph: Theme.icons.unlink, label: qsTr("Unlink video and audio"),
                    shortcut: "unlink",
                    body: qsTr("Move or trim the picture and its sound separately.") },
        "link": { glyph: Theme.icons.link, label: qsTr("Link selected clips"), shortcut: "link",
                    body: qsTr("Keep the selected clips moving, trimming and deleting together."),
                    illustration: illLinkClips },
        "trimStart": { glyph: Theme.icons.trimStart, label: qsTr("Trim start"),
                    body: qsTr("Click a clip to drop everything before that point."),
                    shortcut: "trimStart",
                    illustration: illTrimStart },
        "trimEnd": { glyph: Theme.icons.trimEnd, label: qsTr("Trim end"),
                    body: qsTr("Click a clip to drop everything after that point."),
                    shortcut: "trimEnd",
                    illustration: illTrimEnd },
        "deleteLeft": { glyph: Theme.icons.trimStart, label: qsTr("Trim start to playhead"),
                        shortcut: "deleteLeft",
                    body: qsTr("Drop everything before the playhead from the selected clip."),
                    illustration: illTrimStart },
        "deleteRight": { glyph: Theme.icons.trimEnd, label: qsTr("Trim end to playhead"),
                         shortcut: "deleteRight",
                    body: qsTr("Drop everything after the playhead from the selected clip."),
                    illustration: illTrimEnd },
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
        "freeze": { glyph: Theme.icons.snowflake, label: qsTr("Freeze frame at current time"),
                    shortcut: "freezeFrame",
                    body: qsTr("Hold the current frame as a still. Good for emphasis or a title card."),
                    illustration: illFreezeFrame },
        "adjustmentLayer": { glyph: Theme.icons.wand, label: qsTr("Add adjustment layer"),
                    shortcut: "addAdjustmentLayer",
                    body: qsTr("An empty layer whose effects apply to everything under it."),
                    illustration: illAdjustmentLayer },
        "transformLayer": { glyph: Theme.icons.group, label: qsTr("Add transform layer"),
                            shortcut: "transformTogether",
                    body: qsTr("Move, scale and rotate the selected clips together as one."),
                    illustration: illTransformLayer },
        "camera": { glyph: Theme.icons.video, label: qsTr("Add camera"), shortcut: "addCamera",
                    body: qsTr("One viewpoint the whole timeline is seen through. Use it for pans and zooms.") }
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
