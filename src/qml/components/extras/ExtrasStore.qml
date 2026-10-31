import QtQuick
import QtQuick.Controls.Basic
import Drift

// The Extras store: search, a section sidebar (chips on phones), a grid of cards, a detail page
// pushed over the grid, multi-select and one install bar. The desktop window and the phone page
// both host this item.
//
// open(target): "" | <category id> | "addon:<id>" | "kind:<kind>" | "installed" | "updates" | "custom"
Item {
    id: root

    property bool compact: false

    property string section: "discover"
    property string searchText: ""
    property var selection: ({})
    property var detailStack: []
    property string detailId: ""
    // id -> { fraction, phase }. Rebuilt wholesale on each signal so bindings re-evaluate.
    property var transfers: ({})

    property string pendingRemovalId: ""
    property string pendingRemovalName: ""
    property var pendingUserPackage: ({})
    property var pendingAddonFile: ({})

    signal showRequested()

    readonly property var rows: Addons.catalog
    readonly property string query: searchText.trim().toLowerCase()
    readonly property bool searching: query.length > 0
    readonly property var selectedIds: Object.keys(selection)
    readonly property bool selecting: selectedIds.length > 0
    readonly property bool detailOpen: detailStack.length > 0
    readonly property real sidePadding: compact ? Theme.androidPagePadding : Theme.pagePadding
    readonly property real gridGap: Theme.spacingXl

    readonly property var byId: {
        const map = {}
        for (const r of rows)
            map[r.id] = r
        return map
    }
    readonly property var updateRows: rows.filter(r => r.state === "update-available")
    readonly property var customRows: rows.filter(r => !!r.custom)
    readonly property var categoryList: (Addons.categories || []).filter(c =>
        rows.some(r => (r.categories || []).indexOf(c.id) !== -1))

    readonly property var sections: {
        const list = [
            { id: "discover", title: qsTr("Discover"), glyph: Theme.icons.sparkles },
            { id: "installed", title: qsTr("Installed"), glyph: Theme.icons.success },
            { id: "updates", title: qsTr("Updates"), glyph: Theme.icons.download, count: updateRows.length }
        ]
        const tail = []
        for (const c of categoryList) {
            if (c.id === "engines-runtimes" || c.id === "other")
                continue
            list.push({ id: c.id, title: c.title, icon: c.icon || "" })
        }
        for (const id of ["engines-runtimes", "other"]) {
            const c = categoryList.find(x => x.id === id)
            if (c)
                tail.push({ id: c.id, title: c.title, icon: c.icon || "" })
        }
        if (customRows.length > 0)
            tail.push({ id: "custom", title: qsTr("Custom"), glyph: Theme.icons.puzzle })
        if (tail.length > 0)
            list.push({ divider: true })
        return list.concat(tail)
    }

    readonly property var viewRows: {
        if (searching)
            return rows.filter(r => matches(r, query))
        switch (section) {
        case "discover": return rows.filter(r => !r.custom)
        case "installed": return rows.filter(r => r.state === "installed" || r.state === "update-available")
        case "updates": return updateRows
        case "custom": return customRows
        }
        return rows.filter(r => (r.categories || []).indexOf(section) !== -1)
    }

    readonly property var shelves: {
        if (searching || section !== "discover")
            return []
        const out = []
        for (const c of (Addons.collections || [])) {
            const items = (c.addons || []).map(id => byId[id]).filter(r => !!r)
            if (items.length > 0)
                out.push({ id: c.id, title: c.title, rows: items })
        }
        return out
    }

    readonly property var headerInfo: {
        if (searching)
            return { title: qsTr("Results for “%1”").arg(searchText.trim()), subtitle: "" }
        switch (section) {
        case "discover": return { title: qsTr("Discover"), subtitle: qsTr("Add-ons that make Drift do more.") }
        case "installed": return { title: qsTr("Installed"), subtitle: qsTr("Everything you have downloaded.") }
        case "updates": return { title: qsTr("Updates"), subtitle: qsTr("Newer versions of what you have installed.") }
        case "custom": return { title: qsTr("Custom"), subtitle: qsTr("Packs you installed from a file.") }
        }
        const c = (Addons.categories || []).find(x => x.id === section)
        return c ? { title: c.title, subtitle: c.subtitle || "" } : { title: "", subtitle: "" }
    }

    readonly property int selectableInView: viewRows.filter(r => isSelectable(r)).length
    readonly property int selectedInView: viewRows.filter(r => !!selection[r.id]).length

    function isSelectable(row) {
        return row.state === "available" || row.state === "failed" || row.state === "update-available"
    }

    function matches(row, q) {
        const fields = [row.name, row.tagline, row.description]
        for (const k of (row.keywords || []))
            fields.push(k)
        for (const id of (row.categories || [])) {
            const c = (Addons.categories || []).find(x => x.id === id)
            if (c)
                fields.push(c.title)
        }
        return fields.some(f => f && String(f).toLowerCase().indexOf(q) !== -1)
    }

    function toggleSelected(id) {
        const next = Object.assign({}, selection)
        if (next[id])
            delete next[id]
        else
            next[id] = true
        selection = next
    }

    function clearSelection() {
        selection = ({})
    }

    function selectAll() {
        const next = Object.assign({}, selection)
        for (const r of viewRows) {
            if (isSelectable(r))
                next[r.id] = true
        }
        selection = next
    }

    function openDetail(id) {
        if (!id || id.length === 0)
            return
        detailStack = detailStack.concat([id])
        detailId = id
    }

    function selectSection(id) {
        detailStack = []
        searchText = ""
        section = id
    }

    function refreshAll() {
        Addons.rescanCustomAddons()
        Addons.refresh(true)
    }

    function open(target) {
        refreshAll()
        detailStack = []
        searchText = ""
        target = target || ""
        if (target.indexOf("addon:") === 0) {
            section = "discover"
            openDetail(target.slice(6))
        } else if (target.indexOf("kind:") === 0) {
            section = "discover"
            openDetail(Addons.firstAddonForKind(target.slice(5)))
        } else if (target === "installed" || target === "updates" || target === "custom") {
            section = target
        } else if ((Addons.categories || []).some(c => c.id === target)) {
            section = target
        } else {
            section = "discover"
        }
    }

    function handleBack() {
        if (detailStack.length > 0) {
            detailStack = detailStack.slice(0, -1)
            if (detailStack.length > 0)
                detailId = detailStack[detailStack.length - 1]
            return true
        }
        if (selecting) {
            clearSelection()
            return true
        }
        return false
    }

    function requestRemoval(id, name) {
        pendingRemovalId = id
        pendingRemovalName = name
        confirmRemoval.open()
    }

    function importUserPackage(url) {
        if (!url || String(url) === "")
            url = FileDialogs.openFile(qsTr("Install Addon"),
                                       [qsTr("Drift addons (*.driftpkg *.driftfx *.zip)")])
        if (!url || String(url) === "")
            return
        const info = Addons.inspectAddonFile(url)
        if (info.error) {
            Toasts.error(info.error)
            return
        }
        if (!root.visible)
            root.showRequested()
        if (info.type === "driftfx") {
            root.pendingUserPackage = info
            confirmUserPackage.open()
            return
        }
        root.pendingAddonFile = info
        Addons.installAddonFile(false)
    }

    onSectionChanged: scroller.contentY = 0
    onSearchTextChanged: scroller.contentY = 0

    Connections {
        target: Addons
        function onProgressChanged(id, fraction, phase) {
            const next = Object.assign({}, root.transfers)
            next[id] = { fraction: fraction, phase: phase }
            root.transfers = next
        }
        function onCatalogChanged() {
            const next = {}
            for (const id of Object.keys(root.selection)) {
                if (root.byId[id] && root.isSelectable(root.byId[id]))
                    next[id] = true
            }
            if (Object.keys(next).length !== Object.keys(root.selection).length)
                root.selection = next
        }
        function onAddonFileNeedsConfirmation() {
            confirmUnverified.open()
        }
        function onUserPackageInstalled(name, error) {
            if (error.length > 0)
                Toasts.error(qsTr("Could not install “%1”: %2").arg(name).arg(error))
            else
                Toasts.success(qsTr("Installed “%1”").arg(name))
        }
    }

    ThemedDialog {
        id: confirmRemoval
        title: qsTr("Remove this pack?")
        acceptText: qsTr("Remove")
        acceptVariant: "destructive"
        preferredWidth: Theme.dialogWidthSm
        acceptOnReturn: false

        contentItem: ThemedLabel {
            width: parent ? parent.width : Theme.dialogWidthSm
            wrapMode: Text.WordWrap
            size: "sm"
            text: qsTr("“%1” and its downloaded data will be deleted. You can install it again later.")
                  .arg(root.pendingRemovalName)
        }

        onAccepted: {
            Addons.uninstall(root.pendingRemovalId)
            root.pendingRemovalId = ""
        }
        onRejected: root.pendingRemovalId = ""
    }

    ThemedDialog {
        id: confirmUnverified
        title: qsTr("Install an unofficial addon?")
        acceptText: qsTr("Install anyway")
        acceptVariant: root.pendingAddonFile.nativeCode ? "destructive" : "primary"
        preferredWidth: Theme.dialogWidthSm
        acceptOnReturn: false

        contentItem: ThemedLabel {
            width: parent ? parent.width : Theme.dialogWidthSm
            wrapMode: Text.WordWrap
            size: "sm"
            text: {
                const p = root.pendingAddonFile
                const by = p.author ? qsTr("“%1” by %2").arg(p.name).arg(p.author) : qsTr("“%1”").arg(p.name)
                const lines = [qsTr("%1 is not signed by the Drift team. Only install files you trust.").arg(by)]
                if (p.nativeCode)
                    lines.push(qsTr("It contains code that runs on your computer."))
                if (p.replaces)
                    lines.push(qsTr("It will replace “%1”.").arg(p.replaces))
                return lines.join("\n\n")
            }
        }

        onAccepted: Addons.installAddonFile(true)
        onRejected: {
            Addons.discardAddonFile()
            root.pendingAddonFile = {}
        }
    }

    ThemedDialog {
        id: confirmUserPackage
        title: root.pendingUserPackage.kind === "transitions" ? qsTr("Install this transition?")
               : root.pendingUserPackage.kind === "audio-effects" ? qsTr("Install this audio effect?")
                                                                   : qsTr("Install this effect?")
        acceptText: qsTr("Install")
        preferredWidth: Theme.dialogWidthSm

        contentItem: ThemedLabel {
            width: parent ? parent.width : Theme.dialogWidthSm
            wrapMode: Text.WordWrap
            size: "sm"
            text: {
                const p = root.pendingUserPackage
                const by = p.author ? qsTr("“%1” by %2").arg(p.name).arg(p.author) : qsTr("“%1”").arg(p.name)
                return qsTr("%1 was made by a user, not the Drift team, and nothing has checked it. Only install files you trust.").arg(by)
            }
        }

        onAccepted: {
            Addons.installUserPackage()
            root.pendingUserPackage = {}
        }
        onRejected: root.pendingUserPackage = {}
    }

    component SearchBox: ThemedTextField {
        id: box

        placeholderText: qsTr("Search extras")
        leftPadding: Theme.spacingLg + Theme.iconSizeBase + Theme.spacingMd
        rightPadding: clearButton.visible ? Theme.spacingLg + clearButton.width : Theme.spacingLg
        font.family: Theme.fontFamily

        IconGlyph {
            x: Theme.spacingLg
            anchors.verticalCenter: parent.verticalCenter
            glyph: Theme.icons.search
            iconSize: Theme.iconSizeBase
        }

        IconButton {
            id: clearButton
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingXs
            anchors.verticalCenter: parent.verticalCenter
            visible: box.text.length > 0
            glyph: Theme.icons.x
            buttonSize: Theme.iconButtonSize
            iconSize: Theme.iconSizeSm
            tooltip: qsTr("Clear search")
            onClicked: {
                box.text = ""
                box.textEdited()
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.c.extrasStore.background
    }

    Item {
        id: body
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: installBar.top
        clip: true

        Rectangle {
            id: sidebar
            visible: !root.compact
            width: root.compact ? 0 : 220
            height: parent.height
            color: Theme.c.extrasStore.sidebarBackground

            Rectangle {
                anchors.right: parent.right
                width: Theme.c.extrasStore.borderWidth
                height: parent.height
                color: Theme.c.extrasStore.sidebarBorder
            }

            SearchBox {
                id: sidebarSearch
                x: Theme.spacingLg
                y: Theme.spacingLg
                width: parent.width - Theme.spacingLg * 2 - Theme.c.extrasStore.borderWidth
                text: root.searchText
                onTextEdited: root.searchText = text
            }

            Flickable {
                anchors.top: sidebarSearch.bottom
                anchors.topMargin: Theme.spacingLg
                anchors.bottom: sidebarFooter.top
                anchors.left: parent.left
                anchors.right: parent.right
                contentHeight: sideColumn.height
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                Column {
                    id: sideColumn
                    x: Theme.spacingLg
                    width: sidebar.width - Theme.spacingLg * 2 - Theme.c.extrasStore.borderWidth
                    spacing: Theme.spacingXs

                    Repeater {
                        model: root.sections

                        delegate: Item {
                            id: side
                            required property var modelData
                            readonly property bool picked: !side.modelData.divider && !root.searching
                                                           && root.section === side.modelData.id

                            width: parent.width
                            height: side.modelData.divider ? Theme.spacingXl : Theme.controlHeight

                            Rectangle {
                                visible: !!side.modelData.divider
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width
                                height: Theme.c.extrasStore.borderWidth
                                color: Theme.c.extrasStore.sidebarBorder
                            }

                            Rectangle {
                                visible: !side.modelData.divider
                                anchors.fill: parent
                                radius: Theme.c.extrasStore.chipRadius
                                color: side.picked ? Theme.c.extrasStore.itemSelected
                                       : sideHover.hovered ? Theme.c.extrasStore.itemHover : "transparent"

                                Row {
                                    anchors.left: parent.left
                                    anchors.leftMargin: Theme.spacingLg
                                    anchors.right: parent.right
                                    anchors.rightMargin: Theme.spacingLg
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: Theme.spacingLg

                                    IconGlyph {
                                        anchors.verticalCenter: parent.verticalCenter
                                        visible: !!side.modelData.glyph
                                        glyph: side.modelData.glyph || ""
                                        iconColor: side.picked ? Theme.c.extrasStore.foreground
                                                               : Theme.c.extrasStore.foregroundMuted
                                    }
                                    ExtrasIcon {
                                        anchors.verticalCenter: parent.verticalCenter
                                        visible: !side.modelData.glyph
                                        name: side.modelData.icon || ""
                                        iconColor: side.picked ? Theme.c.extrasStore.foreground
                                                               : Theme.c.extrasStore.foregroundMuted
                                    }
                                    ThemedLabel {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: parent.width - Theme.iconSizeBase - parent.spacing
                                               - (badge.visible ? badge.width + parent.spacing : 0)
                                        text: side.modelData.title || ""
                                        tone: side.picked ? "default" : "muted"
                                        size: "sm"
                                        elide: Text.ElideRight
                                    }
                                    Rectangle {
                                        id: badge
                                        anchors.verticalCenter: parent.verticalCenter
                                        visible: (side.modelData.count || 0) > 0
                                        width: Math.max(height, badgeText.implicitWidth + Theme.spacingMd * 2)
                                        height: Theme.spacingXl + Theme.spacingXs
                                        radius: Theme.c.extrasStore.pillRadius
                                        color: Theme.c.extrasStore.pillBackground

                                        Text {
                                            id: badgeText
                                            anchors.centerIn: parent
                                            text: side.modelData.count || 0
                                            color: Theme.c.extrasStore.pillForeground
                                            font.family: Theme.fontFamily
                                            font.pixelSize: Theme.fontSizeTiny
                                        }
                                    }
                                }

                                HoverHandler {
                                    id: sideHover
                                    cursorShape: Qt.PointingHandCursor
                                }
                                TapHandler {
                                    onTapped: root.selectSection(side.modelData.id)
                                }
                            }
                        }
                    }
                }
            }

            Column {
                id: sidebarFooter
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.margins: Theme.spacingLg
                width: parent.width - Theme.spacingLg * 2 - Theme.c.extrasStore.borderWidth
                spacing: Theme.spacingXs

                ThemedButton {
                    width: parent.width
                    variant: "ghost"
                    glyph: Theme.icons.upload
                    text: qsTr("Install from file…")
                    onClicked: root.importUserPackage("")
                }

                ThemedButton {
                    width: parent.width
                    visible: Qt.platform.os !== "android"
                    variant: "ghost"
                    glyph: Theme.icons.folder
                    text: qsTr("Open addons folder")
                    tooltip: qsTr("Put addon folders here, then reopen Extras")
                    onClicked: Qt.openUrlExternally(Addons.customAddonsFolderUrl())
                }
            }
        }

        Item {
            id: mainArea
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.left: sidebar.visible ? sidebar.right : parent.left
            anchors.right: parent.right

            Column {
                id: top
                width: parent.width
                spacing: Theme.spacingMd
                topPadding: Theme.spacingLg

                Row {
                    visible: root.compact
                    x: root.sidePadding
                    width: parent.width - root.sidePadding * 2
                    spacing: Theme.spacingMd

                    SearchBox {
                        width: parent.width - uploadButton.width - parent.spacing
                        text: root.searchText
                        onTextEdited: root.searchText = text
                    }
                    IconButton {
                        id: uploadButton
                        anchors.verticalCenter: parent.verticalCenter
                        glyph: Theme.icons.upload
                        tooltip: qsTr("Install from file…")
                        onClicked: root.importUserPackage("")
                    }
                }

                ScrollStrip {
                    visible: root.compact
                    width: parent.width
                    height: chipRow.height
                    padding: root.sidePadding

                    Row {
                        id: chipRow
                        spacing: Theme.spacingMd

                        Repeater {
                            model: root.sections.filter(s => !s.divider)
                            delegate: ThemedChip {
                                required property var modelData
                                text: (modelData.count || 0) > 0 ? qsTr("%1 (%2)").arg(modelData.title).arg(modelData.count)
                                                                 : modelData.title
                                selected: !root.searching && root.section === modelData.id
                                onClicked: root.selectSection(modelData.id)
                            }
                        }
                    }
                }

                Item {
                    x: root.sidePadding
                    width: parent.width - root.sidePadding * 2
                    height: headerColumn.height

                    Column {
                        id: headerColumn
                        anchors.left: parent.left
                        anchors.right: selectAllButton.visible ? selectAllButton.left : parent.right
                        anchors.rightMargin: selectAllButton.visible ? Theme.spacingLg : 0
                        spacing: Theme.spacingXs

                        ThemedLabel {
                            width: parent.width
                            text: root.headerInfo.title
                            tone: "default"
                            size: "base"
                            font.pixelSize: Theme.fontSizeBase + 2
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        ThemedLabel {
                            width: parent.width
                            visible: text.length > 0
                            text: root.headerInfo.subtitle
                            size: "sm"
                            elide: Text.ElideRight
                        }
                    }

                    ThemedButton {
                        id: selectAllButton
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        visible: root.selecting && root.selectedInView < root.selectableInView
                        variant: "ghost"
                        text: qsTr("Select all")
                        onClicked: root.selectAll()
                    }
                }

                ThemedLabel {
                    x: root.sidePadding
                    width: parent.width - root.sidePadding * 2
                    visible: (Addons.status.length > 0 || Addons.refreshing) && root.rows.length > 0
                    text: Addons.refreshing ? qsTr("Checking for extras…") : Addons.status
                    size: "sm"
                    color: Addons.refreshing ? Theme.c.extrasStore.foregroundMuted : Theme.c.extrasStore.stateFailed
                    wrapMode: Text.WordWrap
                }
            }

            Flickable {
                id: scroller
                anchors.top: top.bottom
                anchors.topMargin: Theme.spacingLg
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                contentHeight: content.height + Theme.spacing3xl
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                visible: root.rows.length > 0 && (root.shelves.length > 0 || root.viewRows.length > 0)
                ScrollBar.vertical: AppScrollBar {}

                Column {
                    id: content
                    x: root.sidePadding
                    width: scroller.width - root.sidePadding * 2
                    spacing: Theme.spacing2xl

                    readonly property int columns: root.compact ? 2
                        : Math.max(1, Math.floor((width + root.gridGap) / (240 + root.gridGap)))
                    readonly property real cardWidth: Math.floor((width - (columns - 1) * root.gridGap) / columns)
                    readonly property real cardHeight: root.compact ? 168 : 120

                    Repeater {
                        model: root.shelves

                        delegate: Column {
                            id: shelf
                            required property var modelData
                            width: content.width
                            spacing: Theme.spacingMd

                            ThemedLabel {
                                text: shelf.modelData.title
                                tone: "default"
                                size: "base"
                                font.weight: Font.Medium
                            }

                            ListView {
                                width: parent.width
                                height: content.cardHeight
                                orientation: ListView.Horizontal
                                spacing: root.gridGap
                                clip: true
                                boundsBehavior: Flickable.StopAtBounds
                                model: shelf.modelData.rows

                                delegate: ExtrasCard {
                                    required property var modelData
                                    store: root
                                    row: modelData
                                    width: content.cardWidth
                                    height: content.cardHeight
                                    onOpenRequested: root.openDetail(modelData.id)
                                    onSelectionToggled: root.toggleSelected(modelData.id)
                                }
                            }
                        }
                    }

                    ThemedLabel {
                        visible: root.shelves.length > 0
                        text: qsTr("All extras")
                        tone: "default"
                        size: "base"
                        font.weight: Font.Medium
                    }

                    Flow {
                        width: parent.width
                        spacing: root.gridGap

                        Repeater {
                            model: root.viewRows

                            delegate: ExtrasCard {
                                required property var modelData
                                store: root
                                row: modelData
                                width: content.cardWidth
                                height: content.cardHeight
                                onOpenRequested: root.openDetail(modelData.id)
                                onSelectionToggled: root.toggleSelected(modelData.id)
                            }
                        }
                    }
                }
            }

            Flow {
                x: root.sidePadding
                anchors.top: top.bottom
                anchors.topMargin: Theme.spacingLg
                width: parent.width - root.sidePadding * 2
                spacing: root.gridGap
                visible: root.rows.length === 0 && Addons.refreshing

                Repeater {
                    model: 6
                    SkeletonBox {
                        width: content.cardWidth
                        height: content.cardHeight
                        radius: Theme.c.extrasStore.cardRadius
                    }
                }
            }

            EmptyState {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: top.height / 2
                width: Math.min(parent.width - Theme.spacing3xl, 300)
                visible: !scroller.visible && !(root.rows.length === 0 && Addons.refreshing)
                readonly property bool offline: root.rows.length === 0
                glyph: offline ? (Addons.status.length > 0 ? Theme.icons.warning : Theme.icons.puzzle)
                               : Theme.icons.search
                title: offline ? (Addons.status.length > 0 ? qsTr("Can't reach the download store")
                                                           : qsTr("No extras yet"))
                               : (root.searching ? qsTr("Nothing matches your search")
                                                 : qsTr("Nothing here yet"))
                hint: offline ? (Addons.status.length > 0 ? qsTr("Check your connection and try again.") : "")
                              : (root.searching ? qsTr("Try a different word.")
                                                : qsTr("Pick another section."))
                actionText: offline ? qsTr("Retry") : ""
                onActionTriggered: Addons.refresh(true)
            }
        }

        Item {
            id: detailLayer
            property real slide: root.detailOpen ? 0 : 1
            Behavior on slide {
                NumberAnimation { duration: Theme.durationBase; easing.type: Theme.easing }
            }
            visible: slide < 1
            x: mainArea.x + slide * width
            width: mainArea.width
            height: parent.height

            ExtrasDetail {
                anchors.fill: parent
                store: root
                addonId: root.detailId
            }
        }
    }

    ExtrasInstallBar {
        id: installBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        store: root
    }
}
