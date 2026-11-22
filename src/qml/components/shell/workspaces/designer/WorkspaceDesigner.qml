pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Shapes
import Drift
import "DesignerTree.js" as Tree

// Offline editor for one workspace. The draft is a deep copy; nothing reaches the live
// layout until save() hands it to Workspaces.
Item {
    id: root

    property var draft: null
    property string savedJson: ""
    property string selectedId: ""
    property bool creating: false
    property int winIndex: 0
    property var dragPanels: []
    // Drag target under the pointer: "slot", "palette", "empty" or "". Hit-tested by hand
    // against the registered slot blocks; DropArea delivery proved unreliable in the
    // settings dialog.
    property string hoverTarget: ""
    property var hoverSlot: null
    property string hoverZone: ""
    property var slotItems: []
    readonly property bool removing: hoverTarget === "palette" && dragPanels.some(p => placed[p.type])
    property string saveError: ""
    property var pendingAction: null
    property bool renaming: false
    readonly property real minSize: Tree.MIN_SIZE

    readonly property bool dirty: draft !== null && JSON.stringify(draft) !== savedJson
    readonly property string validation: draft ? Workspaces.validate(draft) : ""
    readonly property var windows: draft ? draft.windows : []
    readonly property var currentWindow: windows[Math.min(winIndex, windows.length - 1)] ?? null
    readonly property var currentRoot: currentWindow ? (currentWindow.root ?? null) : null
    readonly property bool onMain: winIndex === 0
    readonly property var placed: draft ? Tree.placedTypes(draft) : ({})
    readonly property var unplaced: Workspaces.panelTypes.filter(p => !placed[p.type])
    readonly property var screenNames: Qt.application.screens.map(s => s.name)

    readonly property var paletteGroups: [
        { id: "panel", title: qsTr("Panels") },
        { id: "assets", title: qsTr("Library tabs") },
        { id: "properties", title: qsTr("Inspector tabs") }
    ]

    implicitHeight: Math.max(sidebar.implicitHeight, main.implicitHeight)

    function tabsFor(type) {
        if (type !== "assets" && type !== "properties")
            return []
        return Workspaces.panelTypes.filter(p => p.group === type).map(p => ({ id: p.tab, label: p.label }))
    }

    function labelOf(type) {
        const found = Workspaces.panelTypes.find(p => p.type === type)
        return found ? found.fullLabel : type
    }

    function tabLabel(type, tab) {
        const found = root.tabsFor(type).find(t => t.id === tab)
        return found ? found.label : ""
    }

    function load(id) {
        const ws = Workspaces.workspace(id)
        if (!ws || !ws.id)
            return false
        draft = Tree.clone(ws)
        savedJson = JSON.stringify(draft)
        selectedId = id
        creating = false
        winIndex = 0
        saveError = ""
        renaming = false
        return true
    }

    function startNew() {
        draft = {
            id: "",
            name: qsTr("New workspace"),
            builtin: false,
            windows: [{ id: "main", root: { panels: [{ type: "preview" }], active: 0 } }]
        }
        savedJson = JSON.stringify(draft)
        selectedId = ""
        creating = true
        winIndex = 0
        saveError = ""
        renaming = true
    }

    function guard(action) {
        if (root.dirty)
            pendingAction = action
        else
            action()
    }

    function resolvePending(save) {
        const action = pendingAction
        if (save && !root.save())
            return
        pendingAction = null
        if (action)
            action()
    }

    function save() {
        saveError = ""
        const known = Workspaces.workspaces.map(w => w.id)
        const err = Workspaces.saveWorkspace(draft)
        if (err.length > 0) {
            saveError = err
            return false
        }
        if (creating) {
            const added = Workspaces.workspaces.find(w => !known.includes(w.id))
            load(added ? added.id : Workspaces.activeId)
        } else {
            load(selectedId)
        }
        return true
    }

    function cancel() {
        if (creating)
            selectFirst()
        else if (!load(selectedId))
            selectFirst()
        pendingAction = null
    }

    function selectFirst() {
        if (!load(Workspaces.activeId) && Workspaces.workspaces.length > 0)
            load(Workspaces.workspaces[0].id)
    }

    function setName(name) {
        const next = Tree.clone(draft)
        next.name = name.trim().length > 0 ? name.trim() : draft.name
        draft = next
        renaming = false
    }

    function setRoot(index, rootNode) {
        const next = Tree.clone(draft)
        next.windows[index].root = rootNode
        draft = next
    }

    function setWindowProp(index, key, value) {
        const next = Tree.clone(draft)
        if (value === "" || value === undefined)
            delete next.windows[index][key]
        else
            next.windows[index][key] = value
        draft = next
    }

    function addWindow() {
        const next = Tree.clone(draft)
        let n = next.windows.length
        while (next.windows.some(w => w.id === "w" + n))
            ++n
        next.windows.push({ id: "w" + n, title: qsTr("Window %1").arg(n), root: null })
        draft = next
        winIndex = next.windows.length - 1
    }

    function removeWindow(index) {
        if (index === 0)
            return
        const next = Tree.clone(draft)
        next.windows.splice(index, 1)
        draft = next
        winIndex = Math.min(winIndex, next.windows.length - 1)
    }

    function windowName(index) {
        return index === 0 ? qsTr("Main window") : (windows[index].title || qsTr("Window"))
    }

    function containingWindow(type) {
        for (let i = 0; i < windows.length; ++i) {
            if (Tree.typesOf(windows[i].root).includes(type))
                return i
        }
        return -1
    }

    function registerSlot(item) {
        slotItems.push(item)
    }

    function unregisterSlot(item) {
        const i = slotItems.indexOf(item)
        if (i >= 0)
            slotItems.splice(i, 1)
        if (hoverSlot === item)
            hoverSlot = null
    }

    function contains(item, p) {
        if (!item.visible)
            return false
        const local = item.mapFromItem(root, p.x, p.y)
        return local.x >= 0 && local.y >= 0 && local.x < item.width && local.y < item.height
    }

    function beginDrag(panels) {
        dragPanels = Tree.clone(panels)
        ghostLabel.text = panels.map(p => labelOf(p.type)).join(" + ")
        ghost.visible = true
    }

    function moveDrag(source, x, y) {
        const p = source.mapToItem(root, x, y)
        ghost.x = p.x
        ghost.y = p.y
        if (contains(palette, p)) {
            hoverSlot = null
            hoverTarget = "palette"
            return
        }
        if (currentRoot === null && contains(emptyTarget, p)) {
            hoverSlot = null
            hoverTarget = "empty"
            return
        }
        for (const slot of slotItems) {
            if (contains(slot, p)) {
                const local = slot.mapFromItem(root, p.x, p.y)
                hoverZone = slot.zoneAt(local.x, local.y)
                hoverSlot = slot
                hoverTarget = "slot"
                return
            }
        }
        hoverSlot = null
        hoverTarget = ""
    }

    function endDrag() {
        if (hoverTarget === "palette")
            dropOnPalette()
        else if (hoverTarget === "empty")
            dropOnEmpty()
        else if (hoverTarget === "slot" && hoverSlot && hoverSlot.dragAllowed)
            dropOnSlot(hoverSlot.targetType, hoverZone)
        cancelDrag()
    }

    function cancelDrag() {
        ghost.visible = false
        hoverTarget = ""
        hoverSlot = null
        hoverZone = ""
        dragPanels = []
    }

    function applyPlacement(targetType, zone) {
        const moved = Tree.clone(dragPanels)
        const sourceWindow = containingWindow(moved[0].type)
        if (sourceWindow >= 0 && sourceWindow !== winIndex)
            setRoot(sourceWindow, Tree.removePanels(windows[sourceWindow].root, moved.map(p => p.type)))
        setRoot(winIndex, Tree.place(currentRoot, moved, targetType, zone))
    }

    function dropOnSlot(targetType, zone) {
        applyPlacement(targetType, zone)
    }

    function dropOnEmpty() {
        applyPlacement("", "center")
    }

    function dropOnPalette() {
        const types = dragPanels.map(p => p.type)
        const next = Tree.clone(draft)
        for (const win of next.windows)
            win.root = Tree.removePanels(win.root, types)
        draft = next
    }

    function removePanel(type) {
        const next = Tree.clone(draft)
        for (const win of next.windows)
            win.root = Tree.removePanels(win.root, [type])
        draft = next
    }

    function activatePanel(type) {
        setRoot(winIndex, Tree.withActive(currentRoot, type))
    }

    function commitSizes(path, sizes) {
        setRoot(winIndex, Tree.withSizes(currentRoot, path, sizes))
    }

    function openMenu(type, item, x, y) {
        const slotPath = Tree.findSlotPath(currentRoot, type)
        const slot = Tree.get(currentRoot, slotPath)
        const panel = slot.panels.find(p => p.type === type)
        const entries = []
        const tabs = tabsFor(type)
        if (tabs.length > 0) {
            entries.push({ kind: "header", text: qsTr("Default tab") })
            for (const tab of tabs)
                entries.push({ kind: "tab", id: tab.id, text: tab.label, checked: panel.tab === tab.id })
        }
        if (slot.panels.length > 1 && slot.panels[slot.active].type !== type)
            entries.push({ kind: "active", text: qsTr("Make active tab") })
        entries.push({ kind: "remove", text: qsTr("Remove %1").arg(labelOf(type)) })
        menu.panelType = type
        menu.entries = entries
        menu.popup(item, x, y)
    }

    function runMenu(entry) {
        if (entry.kind === "tab")
            setRoot(winIndex, Tree.withTab(currentRoot, menu.panelType, entry.checked ? "" : entry.id))
        else if (entry.kind === "active")
            activatePanel(menu.panelType)
        else if (entry.kind === "remove")
            removePanel(menu.panelType)
    }

    Component.onCompleted: selectFirst()

    Connections {
        target: Workspaces

        function onWorkspacesChanged() {
            if (root.creating || root.dirty)
                return
            if (!root.load(root.selectedId))
                root.selectFirst()
        }
    }

    ThemedContextMenu {
        id: menu

        property string panelType: ""
        property var entries: []

        Instantiator {
            model: menu.entries
            onObjectAdded: (index, object) => menu.insertItem(index, object)
            onObjectRemoved: (index, object) => menu.removeItem(object)

            delegate: ThemedMenuItem {
                required property var modelData

                text: modelData.text
                sectionHeader: modelData.kind === "header"
                checkable: modelData.kind === "tab"
                checked: modelData.kind === "tab" && modelData.checked
                onTriggered: root.runMenu(modelData)
            }
        }
    }

    DesignerWorkspaceList {
        id: sidebar
        width: 220
        designer: root
    }

    Column {
        id: main
        x: sidebar.width + Theme.spacingXl
        width: root.width - x
        spacing: Theme.spacingMd
        visible: root.draft !== null

        Rectangle {
            width: parent.width
            height: visible ? pendingRow.implicitHeight + Theme.spacingLg * 2 : 0
            visible: root.pendingAction !== null
            radius: Theme.radiusMd
            color: Theme.panelAccent
            border.width: Theme.c.workspaceDesigner.borderWidth
            border.color: Theme.warning

            Flow {
                id: pendingRow
                anchors.fill: parent
                anchors.margins: Theme.spacingLg
                spacing: Theme.spacingMd

                Text {
                    height: Theme.controlHeightSm
                    verticalAlignment: Text.AlignVCenter
                    text: qsTr("Unsaved changes")
                    color: Theme.panelForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm
                }

                ThemedButton {
                    variant: "primary"
                    text: qsTr("Save")
                    enabled: root.validation.length === 0
                    onClicked: root.resolvePending(true)
                }

                ThemedButton {
                    variant: "secondary"
                    text: qsTr("Discard")
                    onClicked: root.resolvePending(false)
                }

                ThemedButton {
                    variant: "ghost"
                    text: qsTr("Keep editing")
                    onClicked: root.pendingAction = null
                }
            }
        }

        Flow {
            width: parent.width
            spacing: Theme.spacingSm

            Repeater {
                model: root.windows

                ThemedChip {
                    required property var modelData
                    required property int index

                    text: root.windowName(index)
                    selected: index === root.winIndex
                    onClicked: root.winIndex = index
                }
            }

            ThemedButton {
                variant: "ghost"
                text: qsTr("Window")
                glyph: Theme.icons.plus
                height: Theme.controlHeightSm
                onClicked: root.addWindow()
            }
        }

        Column {
            width: parent.width
            spacing: Theme.spacingMd
            visible: !root.onMain && root.currentWindow !== null

            Flow {
                width: parent.width
                spacing: Theme.spacingMd

                ThemedTextField {
                    width: 180
                    placeholderText: qsTr("Window title")
                    text: root.currentWindow?.title ?? ""
                    onEditingFinished: if (text !== (root.currentWindow?.title ?? ""))
                        root.setWindowProp(root.winIndex, "title", text)
                }

                ThemedComboBox {
                    id: screenBox

                    readonly property string saved: root.currentWindow?.screen ?? ""
                    readonly property var names: [qsTr("Any screen")].concat(
                        root.screenNames, saved.length > 0 && !root.screenNames.includes(saved) ? [saved] : [])

                    width: 200
                    model: names
                    currentIndex: saved.length > 0 ? Math.max(0, names.indexOf(saved)) : 0
                    onActivated: (index) => root.setWindowProp(root.winIndex, "screen", index === 0 ? "" : names[index])
                }

                ThemedSwitch {
                    text: qsTr("Fullscreen")
                    checked: root.currentWindow?.fullscreen ?? false
                    onToggled: root.setWindowProp(root.winIndex, "fullscreen", checked ? true : "")
                }

                ThemedButton {
                    variant: "destructive"
                    text: qsTr("Remove window")
                    glyph: Theme.icons.trash
                    onClicked: root.removeWindow(root.winIndex)
                }
            }

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("Use a Preview-only fullscreen window as a second-monitor output.")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
        }

        Rectangle {
            id: palette
            width: parent.width
            height: Math.max(Theme.controlHeight + Theme.spacingLg * 2, paletteFlow.implicitHeight + Theme.spacingLg * 2)
            radius: Theme.radiusMd
            color: root.removing ? Qt.alpha(Theme.c.workspaceDesigner.removing, Theme.c.workspaceDesigner.removingOpacity) : Theme.panelAccent
            border.width: Theme.c.workspaceDesigner.borderWidth
            border.color: root.removing ? Theme.destructive : Theme.panelBorder

            Column {
                id: paletteFlow
                anchors.fill: parent
                anchors.margins: Theme.spacingLg
                spacing: Theme.spacingMd

                Text {
                    visible: root.unplaced.length === 0
                    height: Theme.controlHeightSm
                    verticalAlignment: Text.AlignVCenter
                    text: qsTr("Every panel is placed. Drag one here to remove it.")
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                Repeater {
                    model: root.paletteGroups

                    Column {
                        id: section

                        required property var modelData
                        readonly property var chips: root.unplaced.filter(p => p.group === modelData.id)

                        width: paletteFlow.width
                        spacing: Theme.spacingXs
                        visible: chips.length > 0

                        Text {
                            text: section.modelData.title
                            color: Theme.mutedForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs
                        }

                        Flow {
                            width: parent.width
                            spacing: Theme.spacingSm

                            Repeater {
                                model: section.chips

                                ThemedChip {
                                    id: chip

                                    required property var modelData

                                    variant: "outline"
                                    text: modelData.label

                                    DesignerDragArea {
                                        anchors.fill: parent
                                        designer: root
                                        panels: [{ type: chip.modelData.type }]
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            id: canvas
            width: parent.width
            height: Math.min(width / 1.6, 380)
            radius: Theme.radiusLg
            color: Theme.panelBackground
            border.width: Theme.c.workspaceDesigner.borderWidth
            border.color: Theme.panelBorder

            DesignerNode {
                anchors.fill: parent
                anchors.margins: Theme.spacingSm
                designer: root
                node: root.currentRoot
                visible: root.currentRoot !== null
            }

            Item {
                id: emptyTarget
                anchors.fill: parent
                anchors.margins: Theme.spacingSm
                visible: root.currentRoot === null

                Shape {
                    anchors.fill: parent
                    preferredRendererType: Shape.CurveRenderer

                    ShapePath {
                        strokeColor: root.hoverTarget === "empty" ? Theme.accent : Theme.mutedForeground
                        strokeWidth: 2
                        strokeStyle: ShapePath.DashLine
                        dashPattern: [4, 3]
                        fillColor: root.hoverTarget === "empty" ? Qt.alpha(Theme.c.workspaceDesigner.emptyHover, Theme.c.workspaceDesigner.emptyHoverOpacity) : "transparent"
                        startX: 1
                        startY: 1
                        PathLine { x: canvas.width - Theme.spacingSm * 2 - 1; y: 1 }
                        PathLine { x: canvas.width - Theme.spacingSm * 2 - 1; y: canvas.height - Theme.spacingSm * 2 - 1 }
                        PathLine { x: 1; y: canvas.height - Theme.spacingSm * 2 - 1 }
                        PathLine { x: 1; y: 1 }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    text: qsTr("Drag a panel here")
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm
                }
            }
        }

        Row {
            width: parent.width
            spacing: Theme.spacingMd

            Text {
                width: parent.width - footerButtons.width - parent.spacing
                anchors.verticalCenter: parent.verticalCenter
                wrapMode: Text.WordWrap
                text: root.saveError.length > 0 ? root.saveError
                    : root.validation.length > 0 ? root.validation
                    : root.dirty ? qsTr("Changes apply when you save.") : qsTr("No changes.")
                color: root.saveError.length > 0 || root.validation.length > 0
                    ? Theme.destructive : Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            Row {
                id: footerButtons
                spacing: Theme.spacingSm

                ThemedButton {
                    variant: "secondary"
                    text: qsTr("Cancel")
                    enabled: root.dirty || root.creating
                    onClicked: root.cancel()
                }

                ThemedButton {
                    variant: "primary"
                    text: qsTr("Save")
                    enabled: root.dirty && root.validation.length === 0
                    onClicked: root.save()
                }
            }
        }
    }

    Item {
        id: ghost
        visible: false
        z: 1000
        width: 1
        height: 1

        Rectangle {
            x: 10
            y: 10
            width: ghostLabel.implicitWidth + Theme.spacingLg * 2
            height: Theme.controlHeightSm
            radius: Theme.radiusMd
            color: Theme.accent
            opacity: 0.9

            Text {
                id: ghostLabel
                anchors.centerIn: parent
                color: Theme.accentForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
        }
    }
}
