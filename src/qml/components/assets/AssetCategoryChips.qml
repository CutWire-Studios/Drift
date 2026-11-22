import QtQuick
import QtQuick.Controls.Basic
import Drift
// Jump bar over a SectionedAssetGrid: favorites star, Recent and Mine, then one chip per
// category. A chip scrolls the grid to its section; the chip for the section in view lights up.
// ⭐ [Recent] [Mine] | [Color] [Glitch] …
Item {
    id: root

    readonly property string favoritesId: "__favorites__"
    readonly property string mineId: "__mine__"
    readonly property string recentId: "__recent__"

    // SectionedAssetGrid.visibleSections: [{ id, label }].
    property var sections: []
    property string activeCategory: ""
    property bool searching: false

    readonly property var _fixed: {
        const out = {}
        for (const s of sections) {
            if (s.id === favoritesId || s.id === recentId || s.id === mineId)
                out[s.id] = s
        }
        return out
    }
    readonly property var categories: sections.filter(function(s) {
        return s.id !== root.favoritesId && s.id !== root.recentId && s.id !== root.mineId
    })

    // Phone: the row folds away while the grid scrolls down and returns on the way back up, so the
    // cards get the height. Set to the grid's Flickable.
    property Flickable flickable: null
    property bool folded: false
    property real _lastY: 0

    signal categoryActivated(string categoryId)

    Connections {
        target: root.flickable
        enabled: Theme.phone
        function onContentYChanged() {
            const f = root.flickable
            const y = f.contentY
            const d = y - root._lastY
            root._lastY = y
            if (foldAnim.running || !(f.dragging || f.flicking))
                return
            if (y <= 0)
                root.folded = false
            else if (d > 6 && y > root.implicitHeight)
                root.folded = true
            else if (d < -6)
                root.folded = false
        }
    }

    function _revealActive() {
        for (let i = 0; i < categoryRepeater.count; ++i) {
            const chip = categoryRepeater.itemAt(i)
            if (chip && chip.modelData.id === root.activeCategory) {
                strip.ensureVisible(chip.x, chip.width)
                return
            }
        }
    }

    onActiveCategoryChanged: _revealActive()

    width: parent ? parent.width : 0
    readonly property real fullHeight: Theme.phone ? Theme.controlHeightSm + Theme.spacingLg : Theme.controlHeightSm
    implicitHeight: fullHeight
    height: visible ? (folded ? 0 : fullHeight) : 0
    clip: Theme.phone

    Behavior on height {
        enabled: Theme.phone
        NumberAnimation { id: foldAnim; duration: Theme.durationFast; easing.type: Theme.easing }
    }
    visible: !searching && sections.length > 1

    Row {
        id: row
        anchors.fill: parent
        anchors.leftMargin: Theme.pagePadding
        anchors.rightMargin: Theme.pagePadding
        spacing: Theme.spacingSm

        Row {
            id: fixedRow
            height: parent.height
            spacing: Theme.spacingSm

            IconButton {
                visible: !!root._fixed[root.favoritesId]
                anchors.verticalCenter: parent.verticalCenter
                glyph: Theme.icons.star
                variant: "ghost"
                buttonSize: root.height
                iconSize: 14
                active: root.activeCategory === root.favoritesId
                tooltip: qsTr("Favorites")
                onClicked: root.categoryActivated(root.favoritesId)
            }

            ThemedChip {
                visible: !!root._fixed[root.recentId]
                anchors.verticalCenter: parent.verticalCenter
                text: visible ? root._fixed[root.recentId].label : ""
                variant: "outline"
                selected: root.activeCategory === root.recentId
                onClicked: root.categoryActivated(root.recentId)
            }

            ThemedChip {
                visible: !!root._fixed[root.mineId]
                anchors.verticalCenter: parent.verticalCenter
                text: visible ? root._fixed[root.mineId].label : ""
                variant: "outline"
                selected: root.activeCategory === root.mineId
                onClicked: root.categoryActivated(root.mineId)
            }

            Rectangle {
                visible: Object.keys(root._fixed).length > 0 && root.categories.length > 0
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.c.assetCategoryChips.dividerWidth
                height: Theme.controlHeightSm - 6
                color: Theme.c.assetCategoryChips.divider
            }
        }

        ScrollStrip {
            id: strip
            width: Math.max(0, row.width - (fixedRow.width > 0 ? fixedRow.width + row.spacing : 0))
            height: parent.height

            Row {
                id: categoryRow
                height: parent.height
                spacing: Theme.spacingSm

                Repeater {
                    id: categoryRepeater
                    model: root.categories
                    delegate: ThemedChip {
                        required property var modelData
                        required property int index
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.label
                        variant: "secondary"
                        accentColor: Theme.categoryColor(index)
                        selected: root.activeCategory === modelData.id
                        onClicked: root.categoryActivated(modelData.id)
                    }
                }
            }
        }
    }
}
