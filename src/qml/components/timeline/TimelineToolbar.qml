import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

// Timeline toolbar: transport, user-arranged edit actions with a More menu, timeline
// switcher (main / composites), snap/ripple toggles, zoom controls and the overview toggle. Zoom and the
// time readout are read from and written back to the owning TimelinePanel via `panel`.
// New tracks are added from the plus button above the track headers.
Item {
    id: toolbar

    // Owning TimelinePanel; provides zoom (read/write via setZoom), zoom bounds and formatTime.
    property var panel

    height: Theme.timelineToolbarHeight

    // Appends an action's current binding to its tooltip. Every action here has one,
    // but only the header's Save button used to show it, so the keyboard route to
    // anything on this toolbar was undiscoverable. Rebound keys follow automatically
    // because shortcutFor reads the live map.
    function withShortcut(label, actionId) {
        const key = EditorState.shortcutFor(actionId)
        return key.length > 0 ? qsTr("%1 (%2)").arg(label).arg(key) : label
    }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.panelBorder
    }

    // Every customisable edit action, keyed by the id stored in the toolbar layout.
    // `tip` is the longer toolbar tooltip where the menu label alone would be too terse.
    readonly property var actionMeta: ({
        "select": { glyph: Theme.icons.mousePointer, label: qsTr("Select"),
                    tip: qsTr("Select — normal editing"), shortcut: "selectTool" },
        "cut": { glyph: Theme.icons.scissors, label: qsTr("Cut mode"),
                 tip: qsTr("Cut mode — click a clip to split it"), shortcut: "bladeTool" },
        "separateAudio": { glyph: Theme.icons.audioLines, label: qsTr("Show audio on separate track"),
                           shortcut: "separateAudio" },
        "unlink": { glyph: Theme.icons.unlink, label: qsTr("Unlink video and audio"),
                    shortcut: "unlink" },
        "trimStart": { glyph: Theme.icons.trimStart, label: qsTr("Trim start"),
                       tip: qsTr("Trim start — click a clip to drop everything left of the cut") },
        "trimEnd": { glyph: Theme.icons.trimEnd, label: qsTr("Trim end"),
                     tip: qsTr("Trim end — click a clip to drop everything right of the cut") },
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
        "adjustmentLayer": { glyph: Theme.icons.wand, label: qsTr("Add adjustment layer") }
    })

    readonly property var defaultToolbarItems: [
        "select", "cut", "separator", "undo", "redo", "delete", "separator",
        "separateAudio", "unlink"
    ]
    readonly property var defaultMenuItems: [
        "trimStart", "trimEnd", "separator", "copy", "paste", "duplicate", "separator",
        "bookmark", "markIn", "markOut", "loop", "clearWorkArea", "separator",
        "merge", "freeze", "adjustmentLayer"
    ]

    // The stored layout, cleaned: unknown or repeated ids are dropped and any action missing
    // from both lists (one added in a later version) lands at the end of the menu.
    readonly property var layout: {
        const storedToolbar = EditorState.timelineToolbarItems
        const storedMenu = EditorState.timelineMenuItems
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

    function actionTooltip(id) {
        const meta = actionMeta[id]
        const label = meta.tip || meta.label
        return meta.shortcut ? withShortcut(label, meta.shortcut) : label
    }

    function triggerAction(id) {
        switch (id) {
        case "select": panel.timelineTool = ""; break
        case "cut": panel.timelineTool = panel.timelineTool === "split" ? "" : "split"; break
        case "trimStart": panel.timelineTool = panel.timelineTool === "trimStart" ? "" : "trimStart"; break
        case "trimEnd": panel.timelineTool = panel.timelineTool === "trimEnd" ? "" : "trimEnd"; break
        case "separateAudio": EditorState.separateAudioFromSelection(); break
        case "unlink": EditorState.unlinkSelectedClips(); break
        case "undo": EditorState.undo(); break
        case "redo": EditorState.redo(); break
        case "delete": EditorState.deleteSelectedClip(); break
        case "copy": EditorState.copySelection(); break
        case "paste": EditorState.pasteAtPlayhead(); break
        case "duplicate": EditorState.duplicateSelectedClip(); break
        case "bookmark": EditorState.toggleBookmarkAtPlayhead(); break
        case "markIn": EditorState.markWorkAreaIn(); break
        case "markOut": EditorState.markWorkAreaOut(); break
        case "loop": EditorState.toggleLoopWorkArea(); break
        case "clearWorkArea": EditorState.clearWorkArea(); break
        case "merge": EditorState.mergeSelectedClips(); break
        case "freeze": EditorState.freezeFrameAtPlayhead(); break
        case "adjustmentLayer": EditorState.addAdjustmentClip(-1, -1); break
        }
    }

    function actionEnabled(id) {
        switch (id) {
        case "separateAudio": return EditorState.separateAudioAvailable
        case "unlink": return EditorState.unlinkAvailable
        case "undo": return EditorState.undoAvailable
        case "redo": return EditorState.redoAvailable
        case "loop": return EditorState.workAreaActive
        case "clearWorkArea": return EditorState.workAreaInSeconds >= 0
                                     || EditorState.workAreaOutSeconds >= 0
        case "merge": return EditorState.mergeAvailable
        }
        return true
    }

    function actionActive(id) {
        switch (id) {
        case "select": return panel.timelineTool === ""
        case "cut": return panel.timelineTool === "split"
        case "trimStart": return panel.timelineTool === "trimStart"
        case "trimEnd": return panel.timelineTool === "trimEnd"
        case "markIn": return EditorState.workAreaInSeconds >= 0
        case "markOut": return EditorState.workAreaOutSeconds >= 0
        case "loop": return EditorState.loopWorkAreaEnabled
        }
        return false
    }

    // On the toolbar the A/V pair only appears when it applies to the selection, so a
    // selection shows one or the other rather than two mostly-disabled buttons.
    function actionShownOnToolbar(id) {
        if (id === "separator")
            return true
        if (id === "separateAudio")
            return EditorState.separateAudioAvailable
        if (id === "unlink")
            return EditorState.unlinkAvailable
        return true
    }

    // A separator only divides something: not at either end, and not next to another one
    // (which happens when the buttons between them are all contextually hidden).
    function separatorNeeded(list, i, shown) {
        var before = false
        for (var b = i - 1; b >= 0 && list[b] !== "separator"; b--) {
            if (shown(list[b])) { before = true; break }
        }
        if (!before)
            return false
        for (var a = i + 1; a < list.length; a++) {
            if (list[a] === "separator")
                return false
            if (shown(list[a]))
                return true
        }
        return false
    }

    Row {
        id: leftControls
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacingLg
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.spacingXs
        // Never runs under the right-hand controls; buttons past the
        // available width are clipped rather than overlapping.
        width: Math.max(0, rightControls.x - x - Theme.spacingLg)
        clip: true

        IconButton {
            glyph: EditorState.playing ? Theme.icons.pause : Theme.icons.play
            variant: "text"
            tooltip: EditorState.playing ? qsTr("Pause") : qsTr("Play")
            onClicked: EditorState.togglePlayback()
        }

        Text {
            text: toolbar.panel.formatTime(EditorState.playheadSeconds) + " / " + toolbar.panel.formatTime(EditorState.durationSeconds)
            color: Theme.mutedForeground
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.fontSizeXs
            anchors.verticalCenter: parent.verticalCenter
        }

        Rectangle {
            width: Theme.borderWidth
            height: Theme.spacing3xl
            color: Theme.panelBorder
            anchors.verticalCenter: parent.verticalCenter
        }

        Repeater {
            model: toolbar.layout.toolbar

            delegate: Item {
                id: slot
                required property string modelData
                required property int index
                readonly property bool isSeparator: modelData === "separator"

                visible: isSeparator
                         ? toolbar.separatorNeeded(toolbar.layout.toolbar, index,
                                                   toolbar.actionShownOnToolbar)
                         : toolbar.actionShownOnToolbar(modelData)
                width: isSeparator ? Theme.borderWidth : actionButton.width
                height: isSeparator ? Theme.spacing3xl : actionButton.height
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined

                Rectangle {
                    visible: slot.isSeparator
                    anchors.fill: parent
                    color: Theme.panelBorder
                }

                IconButton {
                    id: actionButton
                    visible: !slot.isSeparator
                    glyph: slot.isSeparator ? "" : toolbar.actionMeta[slot.modelData].glyph
                    variant: "text"
                    tooltip: slot.isSeparator ? "" : toolbar.actionTooltip(slot.modelData)
                    active: toolbar.actionActive(slot.modelData)
                    enabled: toolbar.actionEnabled(slot.modelData)
                    onClicked: toolbar.triggerAction(slot.modelData)
                }
            }
        }

        IconButton {
            id: overflowButton
            glyph: Theme.icons.ellipsis
            variant: "text"
            tooltip: qsTr("More edit actions")
            active: overflowMenu.opened
            onClicked: overflowMenu.opened ? overflowMenu.close()
                                           : overflowMenu.popup(0, overflowButton.height)

            ThemedContextMenu {
                id: overflowMenu
                implicitWidth: 260

                // Rebuilt on each open from the current layout. A Menu cannot host a
                // Repeater, and Instantiator cannot mix items with separators.
                onAboutToShow: toolbar.rebuildOverflowMenu()

                ThemedMenuSeparator { id: customizeSeparator }
                ThemedMenuItem {
                    text: qsTr("Customize toolbar…")
                    icon.name: Theme.icons.sliders
                    onTriggered: customizeDialog.open()
                }
            }
        }
    }

    Component {
        id: overflowItemComponent
        ThemedMenuItem {
            property string actionId
            text: toolbar.actionMeta[actionId].shortcut
                  ? toolbar.withShortcut(toolbar.actionMeta[actionId].label,
                                         toolbar.actionMeta[actionId].shortcut)
                  : toolbar.actionMeta[actionId].label
            icon.name: toolbar.actionMeta[actionId].glyph
            enabled: toolbar.actionEnabled(actionId)
            onTriggered: toolbar.triggerAction(actionId)
        }
    }

    Component {
        id: overflowSeparatorComponent
        ThemedMenuSeparator {}
    }

    function rebuildOverflowMenu() {
        // Everything before the fixed "Customize" tail is generated.
        while (overflowMenu.count > 2)
            overflowMenu.removeItem(overflowMenu.itemAt(0))
        const list = layout.menu
        const always = () => true
        var at = 0
        for (var i = 0; i < list.length; i++) {
            if (list[i] === "separator") {
                if (separatorNeeded(list, i, always))
                    overflowMenu.insertItem(at++, overflowSeparatorComponent.createObject(null))
                continue
            }
            overflowMenu.insertItem(at++, overflowItemComponent.createObject(null,
                                                                            { actionId: list[i] }))
        }
        customizeSeparator.visible = at > 0
    }

    TimelineToolbarCustomizeDialog {
        id: customizeDialog
        toolbar: toolbar
    }

    Rectangle {
        id: sceneBadge

        // Sits between the two button groups, which are anchored to the
        // toolbar edges and grow freely. Prefer dead centre, but slide
        // aside to stay clear of them, and drop out entirely once the
        // gap can no longer fit the badge.
        // leftControls is stretched up to rightControls so it can clip, so its buttons
        // end at implicitWidth, not width.
        readonly property real gapStart: leftControls.x
                                         + Math.min(leftControls.implicitWidth, leftControls.width) + 12
        readonly property real gapEnd: rightControls.x - 12

        x: Math.max(gapStart, Math.min((toolbar.width - width) / 2, gapEnd - width))
        anchors.verticalCenter: parent.verticalCenter
        visible: gapEnd - gapStart >= width
        width: sceneRow.implicitWidth + 20
        height: 26
        radius: Theme.radiusSm
        color: "transparent"
        border.width: 1
        border.color: Qt.rgba(Theme.panelForeground.r, Theme.panelForeground.g, Theme.panelForeground.b, 0.1)

        // The active timeline: "Main", or the open composite's name.
        readonly property string activeName: {
            const active = EditorState.activeSequenceId
            if (active === "")
                return qsTr("Main")
            const tabs = EditorState.sequenceTabs
            for (var i = 0; i < tabs.length; i++) {
                if (tabs[i].id === active)
                    return tabs[i].name
            }
            return qsTr("Composite")
        }

        Row {
            id: sceneRow
            anchors.centerIn: parent
            spacing: 6

            IconGlyph {
                glyph: EditorState.activeSequenceId === "" ? Theme.icons.film : Theme.icons.layers
                iconSize: 14
                iconColor: Theme.mutedForeground
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: sceneBadge.activeName
                color: Theme.panelForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                elide: Text.ElideRight
                width: Math.min(implicitWidth, 180)
                anchors.verticalCenter: parent.verticalCenter
            }
            IconGlyph {
                glyph: Theme.icons.chevronDown
                iconSize: 12
                iconColor: Theme.mutedForeground
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        MouseArea {
            id: sceneMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: sceneMenu.opened ? sceneMenu.close() : sceneMenu.popup(0, sceneBadge.height + 4)
        }

        ThemedToolTip {
            visible: sceneMouse.containsMouse && !sceneMenu.opened
            text: qsTr("Switch between the main timeline and composite clips")
        }

        ThemedContextMenu {
            id: sceneMenu
            implicitWidth: 240

            // Composites come and go with the project's assets, which have no notify of
            // their own here, so the list is read fresh on each open.
            onAboutToShow: toolbar.rebuildSceneMenu()
        }
    }

    Component {
        id: sceneItemComponent
        ThemedMenuItem {
            property string sequenceId
            property string sequenceName
            text: sequenceName
            icon.name: EditorState.activeSequenceId === sequenceId
                       ? Theme.icons.check
                       : (sequenceId === "" ? Theme.icons.film : Theme.icons.layers)
            onTriggered: EditorState.openSequence(sequenceId)
        }
    }

    Component {
        id: sceneEmptyComponent
        ThemedMenuItem {
            enabled: false
            text: qsTr("No composite clips yet")
            icon.name: Theme.icons.info
        }
    }

    function rebuildSceneMenu() {
        while (sceneMenu.count > 0)
            sceneMenu.removeItem(sceneMenu.itemAt(0))
        sceneMenu.addItem(sceneItemComponent.createObject(null,
            { sequenceId: "", sequenceName: qsTr("Main") }))
        sceneMenu.addItem(overflowSeparatorComponent.createObject(null))
        const composites = EditorState.compositeSequences()
        if (composites.length === 0)
            sceneMenu.addItem(sceneEmptyComponent.createObject(null))
        for (var i = 0; i < composites.length; i++) {
            sceneMenu.addItem(sceneItemComponent.createObject(null,
                { sequenceId: composites[i].id, sequenceName: composites[i].name }))
        }
    }

    Row {
        id: rightControls
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingLg
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.spacingSm

        IconButton {
            id: magnetButton
            glyph: Theme.icons.magnet
            variant: "text"
            tooltip: qsTr("Toggle snapping")
            active: EditorState.snapEnabled
            onClicked: EditorState.snapEnabled = !EditorState.snapEnabled
        }
        IconButton {
            id: rippleButton
            glyph: Theme.icons.foldHorizontal
            variant: "text"
            tooltip: qsTr("Close gaps when trimming")
            active: EditorState.rippleEnabled
            onClicked: EditorState.rippleEnabled = !EditorState.rippleEnabled
        }
        IconButton {
            id: overlapButton
            glyph: Theme.icons.option
            variant: "text"
            tooltip: qsTr("Allow clip overlap")
            active: EditorState.allowClipOverlap
            onClicked: EditorState.allowClipOverlap = !EditorState.allowClipOverlap
        }

        Rectangle {
            width: Theme.borderWidth
            height: Theme.spacing3xl
            color: Theme.panelBorder
            anchors.verticalCenter: parent.verticalCenter
        }

        IconButton {
            glyph: Theme.icons.zoomOut
            variant: "text"
            tooltip: qsTr("Zoom out")
            onClicked: toolbar.panel.setZoom(toolbar.panel.zoom / 1.5)
        }
        ThemedSlider {
            id: zoomSlider
            label: qsTr("Timeline zoom")
            width: 112
            anchors.verticalCenter: parent.verticalCenter
            // Logarithmic mapping so the wide zoom range stays controllable.
            from: 0
            to: 1
            value: Math.log(toolbar.panel.zoom / toolbar.panel.minZoom) / Math.log(toolbar.panel.maxZoom / toolbar.panel.minZoom)
            onMoved: toolbar.panel.setZoom(
                toolbar.panel.minZoom * Math.pow(toolbar.panel.maxZoom / toolbar.panel.minZoom, value))
            // There was no zoom readout anywhere, so the current level
            // was simply unknowable.
            valueFormatter: function () {
                return qsTr("Zoom %1×").arg(toolbar.panel.zoom.toFixed(2))
            }
        }

        // Numeric zoom level, and a click target to return to 1×.
        Text {
            anchors.verticalCenter: parent.verticalCenter
            width: 44
            text: toolbar.panel.zoom.toFixed(2) + "×"
            color: Theme.mutedForeground
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.fontSizeTick
            horizontalAlignment: Text.AlignHCenter

            ThemedToolTip {
                text: qsTr("Zoom level — click to reset to 1×. Ctrl+wheel over the timeline also zooms.")
                visible: zoomLabelMouse.containsMouse
            }

            MouseArea {
                id: zoomLabelMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: toolbar.panel.setZoom(1.0)
            }
        }
        IconButton {
            glyph: Theme.icons.zoomIn
            variant: "text"
            tooltip: qsTr("Zoom in")
            onClicked: toolbar.panel.setZoom(toolbar.panel.zoom * 1.5)
        }
        IconButton {
            glyph: Theme.icons.zoomFit
            variant: "text"
            tooltip: qsTr("Fit timeline in view")
            onClicked: toolbar.panel.fitZoom()
        }

        Rectangle {
            width: Theme.borderWidth
            height: Theme.spacing3xl
            color: Theme.panelBorder
            anchors.verticalCenter: parent.verticalCenter
        }

        IconButton {
            glyph: Theme.icons.panelTop
            variant: "text"
            tooltip: qsTr("Timeline overview — a minimap of the whole project; click or drag it to jump the view")
            active: EditorState.timelineOverviewVisible
            onClicked: EditorState.timelineOverviewVisible = !EditorState.timelineOverviewVisible
        }
    }
}
