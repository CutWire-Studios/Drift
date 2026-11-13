pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Drift

// Every category in one scrolling grid, a header per category that sticks to the top while its
// cards scroll past. Favorites, Recent and Mine are sections of their own above the categories,
// shown only when they have something in them. Pair with AssetCategoryChips: feed it
// visibleSections and currentSectionId, and send its clicks to scrollToSection().
//
// A Flickable that lays the rows out itself rather than a ListView with sections: a ListView only
// estimates its content height and origin when rows differ in height, and the bounds moving
// under a scroll is what broke touchpad momentum and made jumping to a section a guess. Here
// every position is arithmetic (headers and card rows each have one height), and a small pool
// of row items is recycled over whatever is in view.
Flickable {
    id: root

    readonly property string favoritesId: "__favorites__"
    readonly property string recentId: "__recent__"
    readonly property string mineId: "__mine__"

    property var items: []
    // [{ id, label }] in display order.
    property var categories: []
    property var categoryOf: function(item) { return item.category }
    // Mine items leave their category so they show once, in Mine.
    property var isMine: null
    property string mineLabel: qsTr("Mine")
    property var recentItems: []
    // EditorState.isAssetFavorite tab id; empty turns the Favorites section off.
    property string favoritesKind: ""
    property string idKey: "id"
    property string query: ""
    property var searchFields: ["label", "id"]

    // The card. It gets modelData from the row; bind its size to root.cardSize.
    property Component cardDelegate
    property real gap: Theme.assetCardGap
    property real nominalCardWidth: Theme.assetCardWidth
    property int minColumns: 1
    property real sidePadding: Theme.pagePadding
    readonly property real _available: width - sidePadding * 2 + gap
    readonly property int columnCount: Math.max(minColumns, Math.round(_available / (nominalCardWidth + gap)))
    readonly property real cellWidth: Math.floor(_available / columnCount)
    readonly property real cardSize: cellWidth - gap
    // Override when the card is taller than it is wide (labels under the square).
    property real cellHeight: cardSize + gap
    readonly property real headerHeight: Theme.controlHeightSm + Theme.spacingSm

    readonly property int itemCount: {
        let n = 0
        for (const s of visibleSections)
            n += s.items.length
        return n
    }
    readonly property string currentSectionId: _current ? _current.id : ""
    // How far past the view rows are kept alive.
    property real cacheBuffer: 320

    property int _favoritesTick: 0
    // [{ id, label, index, first, rows, before }]: first is the header's index in _rows, rows the
    // number of card rows, before the card rows in the sections above.
    property var _sections: []
    // Headers and card rows in order: { sec, n, items }, n 0 for the header and 1.. for cards.
    property var _rows: []
    property int _cardRows: 0
    // A jump asked for before the first rows were built, e.g. a tab restoring its last category.
    property string _pendingSection: ""
    // The row at the top of the view and how far into it the view is, so a rebuild (a new
    // favorite, a wider panel) keeps what you were looking at in place.
    property var _anchor: null
    property bool _restoring: false

    readonly property int _first: _rowAt(contentY - cacheBuffer)
    readonly property int _last: _rows.length > 0
                                 ? Math.min(_rowAt(contentY + height + cacheBuffer), _first + slotModel.count - 1)
                                 : -1
    readonly property int _currentIndex: {
        let i = 0
        for (const s of _sections) {
            if (_sectionY(s) <= contentY + 0.5)
                i = s.index
        }
        return i
    }
    readonly property var _current: _sections[_currentIndex] || null
    readonly property real _maxContentY: Math.max(0, contentHeight + bottomMargin - height)

    readonly property var visibleSections: {
        void root._favoritesTick
        const q = root.query.trim().toLowerCase()
        const fields = root.searchFields
        const matches = function(item) {
            if (q.length === 0)
                return true
            for (const f of fields) {
                if (String(item[f] || "").toLowerCase().indexOf(q) >= 0)
                    return true
            }
            return false
        }
        const out = []
        const push = function(id, label, list) {
            if (list.length > 0)
                out.push({ id: id, label: label, items: list })
        }
        const pool = root.items.filter(matches)
        // While searching these would only repeat matches already listed under their category.
        if (q.length === 0) {
            if (root.favoritesKind !== "")
                push(root.favoritesId, qsTr("Favorites"), pool.filter(function(item) {
                    return EditorState.isAssetFavorite(root.favoritesKind, item[root.idKey])
                }))
            push(root.recentId, qsTr("Recent"), root.recentItems)
        }
        const mine = root.isMine
        if (mine)
            push(root.mineId, root.mineLabel, pool.filter(function(item) { return mine(item) }))
        const byCategory = {}
        for (const item of pool) {
            if (mine && mine(item))
                continue
            const c = root.categoryOf(item)
            if (!byCategory[c])
                byCategory[c] = []
            byCategory[c].push(item)
        }
        for (const c of root.categories)
            push(c.id, c.label, byCategory[c.id] || [])
        return out
    }

    function _sectionY(s) {
        return s.index * root.headerHeight + s.before * root.cellHeight
    }

    function _rowY(row) {
        const y = _sectionY(root._sections[row.sec])
        return row.n === 0 ? y : y + root.headerHeight + (row.n - 1) * root.cellHeight
    }

    function _rowAt(y) {
        let sec = null
        for (const s of root._sections) {
            if (sec === null || _sectionY(s) <= y)
                sec = s
        }
        if (sec === null || root.cellHeight <= 0)
            return 0
        const into = y - _sectionY(sec) - root.headerHeight
        return sec.first + Math.max(0, Math.min(sec.rows, 1 + Math.floor(into / root.cellHeight)))
    }

    function scrollToSection(id) {
        if (root._rows.length === 0) {
            root._pendingSection = id
            return
        }
        const s = root._sections.find(function(s) { return s.id === id })
        if (!s)
            return
        scrollAnimation.to = Math.min(_sectionY(s), root._maxContentY)
        scrollAnimation.restart()
    }

    function _restore(anchor) {
        let y = 0
        const s = anchor ? root._sections.find(function(s) { return s.id === anchor.id }) : null
        if (s)
            y = _rowY({ sec: s.index, n: Math.min(anchor.n, s.rows) }) + anchor.offset
        else
            root._anchor = null
        root._restoring = true
        contentY = Math.max(0, Math.min(y, root._maxContentY))
        root._restoring = false
    }

    function _rebuild() {
        const anchor = root._anchor
        const cols = root.columnCount
        const sections = []
        const rows = []
        let cardRows = 0
        // Locals rather than reading fields back off the section object: qmlcachegen 6.12
        // segfaults compiling a property lookup on an object literal here.
        for (const s of root.visibleSections) {
            const index = sections.length
            const count = Math.ceil(s.items.length / cols)
            sections.push({ id: s.id, label: s.label, index: index, first: rows.length,
                            rows: count, before: cardRows })
            rows.push({ sec: index, n: 0, items: [] })
            for (let i = 0; i < count; ++i)
                rows.push({ sec: index, n: i + 1, items: s.items.slice(i * cols, (i + 1) * cols) })
            cardRows += count
        }
        scrollAnimation.stop()
        root._restoring = true
        // Emptied first so no row is bound against the other array's old contents.
        root._rows = []
        root._sections = sections
        root._cardRows = cardRows
        root._rows = rows
        _restore(anchor)
        if (root._pendingSection !== "" && rows.length > 0) {
            const id = root._pendingSection
            root._pendingSection = ""
            root.scrollToSection(id)
        }
    }

    // Enough row items for the tallest the view has been; a header is at most every other row.
    function _growPool() {
        if (root.cellHeight <= 0 || root.height <= 0)
            return
        const need = Math.min(400, Math.ceil((root.height + root.cacheBuffer * 2) / root.cellHeight) * 2 + 2)
        while (slotModel.count < need)
            slotModel.append({})
    }

    onVisibleSectionsChanged: Qt.callLater(root._rebuild)
    onColumnCountChanged: Qt.callLater(root._rebuild)
    onHeightChanged: _growPool()
    onCellHeightChanged: {
        _growPool()
        if (root._anchor)
            _restore(root._anchor)
    }
    onContentYChanged: {
        if (root._restoring || root._rows.length === 0)
            return
        if (contentY <= 0) {
            root._anchor = null
            return
        }
        const row = root._rows[_rowAt(contentY)]
        root._anchor = { id: root._sections[row.sec].id, n: row.n, offset: contentY - _rowY(row) }
    }
    Component.onCompleted: {
        _growPool()
        _rebuild()
    }

    Connections {
        target: EditorState
        function onAssetFavoritesChanged() {
            root._favoritesTick++
        }
    }

    NumberAnimation {
        id: scrollAnimation
        target: root
        property: "contentY"
        duration: Theme.durationBase
        easing.type: Easing.OutCubic
    }

    onMovementStarted: scrollAnimation.stop()

    clip: true
    contentWidth: width
    contentHeight: _sections.length * headerHeight + _cardRows * cellHeight
    boundsBehavior: Flickable.StopAtBounds
    // A mouse drag on a card drags the asset; the wheel and touchpad scroll.
    acceptedButtons: Theme.touchUi ? Qt.LeftButton : Qt.NoButton
    bottomMargin: Theme.pagePadding
    ScrollBar.vertical: AppScrollBar { }

    // Inset and rounded so the pinned header reads as a floating label over the cards rather
    // than a band cutting through them.
    component SectionHeader: Item {
        id: header
        property string label
        property real sidePadding

        Rectangle {
            x: header.sidePadding
            width: parent.width - header.sidePadding * 2
            height: parent.height - Theme.spacingSm
            anchors.verticalCenter: parent.verticalCenter
            radius: Theme.c.sectionedAssetGrid.headerRadius
            color: Theme.c.sectionedAssetGrid.headerBackground
            border.width: Theme.c.sectionedAssetGrid.headerBorderWidth
            border.color: Theme.c.sectionedAssetGrid.headerBorder

            Text {
                x: Theme.spacingMd
                width: parent.width - Theme.spacingMd * 2
                anchors.verticalCenter: parent.verticalCenter
                text: header.label
                color: Theme.c.sectionedAssetGrid.headerForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                font.weight: Font.Medium
                elide: Text.ElideRight
            }
        }
    }

    Repeater {
        model: ListModel { id: slotModel }

        delegate: Item {
            id: slot
            required property int index
            // Each row in view maps to one slot and keeps it for as long as it stays in view.
            readonly property int rowIndex: {
                const n = slotModel.count
                const r = root._first + (((index - root._first) % n) + n) % n
                return r <= root._last ? r : -1
            }
            readonly property var row: rowIndex >= 0 ? (root._rows[rowIndex] || null) : null
            readonly property bool isHeader: row !== null && row.n === 0

            visible: row !== null
            y: row !== null ? root._rowY(row) : 0
            width: root.width
            height: isHeader ? root.headerHeight : root.cellHeight

            SectionHeader {
                anchors.fill: parent
                visible: slot.isHeader
                label: slot.isHeader ? root._sections[slot.row.sec].label : ""
                sidePadding: root.sidePadding
            }

            Row {
                x: root.sidePadding
                spacing: root.gap

                Repeater {
                    model: slot.row !== null ? slot.row.items : []
                    delegate: root.cardDelegate
                }
            }
        }
    }

    // The current section's header, pinned to the top until the next one pushes it out.
    SectionHeader {
        readonly property var next: root._sections[root._currentIndex + 1]
        visible: root._current !== null
        z: 1
        y: next ? Math.min(root.contentY, root._sectionY(next) - root.headerHeight) : root.contentY
        width: root.width
        height: root.headerHeight
        label: root._current ? root._current.label : ""
        sidePadding: root.sidePadding
    }
}
