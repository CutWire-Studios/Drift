pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Drift

// Type-to-add search over controller.quickAddEntries. Opened by Space, a double-click or a long
// press on the canvas, or by dropping a wire on empty space; in the wire case the list is ranked
// (and, before typing, filtered) by the dragged socket's type, and the pick is created at worldPos
// and wired to the dragged port.
Popup {
    id: root

    property var canvas: null
    property var controller: null
    // The wire being dragged, {node, port, isOutput, type}, or null for a plain add.
    property var pendingWire: null
    property point worldPos: Qt.point(0, 0)
    property bool wasPicked: false
    readonly property real nodeWidth: 220

    property var rows: []
    property int highlighted: -1

    function openAt(sx, sy, wp, wire) {
        wasPicked = false
        pendingWire = wire
        worldPos = wp
        search.text = ""
        rebuild()
        x = Math.max(8, Math.min(sx, (parent ? parent.width : sx) - width - 8))
        y = Math.max(8, Math.min(sy, (parent ? parent.height : sy) - height - 8))
        open()
    }

    function rebuild() {
        const q = search.text.trim()
        const entries = controller
            ? controller.quickAddEntries(q, pendingWire ? pendingWire.type : "", pendingWire ? pendingWire.isOutput : false)
            : []
        const out = []
        let lastCategory = ""
        for (const e of entries) {
            if (q.length === 0 && e.category !== lastCategory) {
                out.push({ header: true, label: e.category })
                lastCategory = e.category
            }
            out.push(e)
        }
        rows = out
        highlighted = -1
        step(1)
    }

    function step(delta) {
        let i = highlighted
        for (;;) {
            i += delta
            if (i < 0 || i >= rows.length)
                return
            if (!rows[i].header)
                break
        }
        highlighted = i
        list.positionViewAtIndex(i, ListView.Contain)
    }

    function pick(entry) {
        wasPicked = true
        close()
        const w = pendingWire
        const at = Qt.point(w && !w.isOutput ? worldPos.x - nodeWidth : worldPos.x, worldPos.y - 14)
        const id = w ? controller.addNodeConnected(entry.type, at, w.node, w.port, w.isOutput)
                     : controller.addNode(entry.type, at)
        if (canvas)
            canvas.cancelPending()
        if (id)
            controller.select(id, false)
    }

    function pickHighlighted() {
        if (highlighted >= 0)
            pick(rows[highlighted])
    }

    width: 340
    height: Math.min(420, spacer.height + list.contentHeight + padding * 2 + Theme.spacingSm)
    padding: Theme.spacingSm
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    onOpened: search.forceActiveFocus()
    onClosed: {
        if (!wasPicked && canvas)
            canvas.cancelPending()
        if (canvas)
            canvas.forceActiveFocus()
    }

    background: Rectangle {
        radius: Theme.c.themedContextMenu.radius
        color: Theme.c.themedContextMenu.background
        border.width: Theme.c.themedContextMenu.borderWidth
        border.color: Theme.c.themedContextMenu.border
    }

    contentItem: Item {
        ThemedTextField {
            id: search
            width: parent.width
            placeholderText: root.pendingWire ? qsTr("Add and connect…") : qsTr("Search nodes…")
            onTextChanged: root.rebuild()
            Keys.onDownPressed: root.step(1)
            Keys.onUpPressed: root.step(-1)
            Keys.onTabPressed: (event) => event.accepted = true
            Keys.onReturnPressed: root.pickHighlighted()
            Keys.onEnterPressed: root.pickHighlighted()
        }

        Item {
            id: spacer
            width: parent.width
            height: search.height + Theme.spacingSm
        }

        Text {
            x: Theme.spacingLg
            y: spacer.height + Theme.spacingSm
            visible: root.rows.length === 0
            text: qsTr("No matching nodes")
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            color: Theme.c.themedMenuItem.sectionForeground
        }

        ListView {
            id: list
            y: spacer.height
            width: parent.width
            height: parent.height - spacer.height
            clip: true
            model: root.rows
            boundsBehavior: Flickable.StopAtBounds

            delegate: Item {
                id: row

                required property var modelData
                required property int index

                width: ListView.view.width
                height: row.modelData.header ? Theme.controlHeightSm : Theme.controlHeightSm * 1.6

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.c.themedMenuItem.radius
                    color: !row.modelData.header && (area.containsMouse || root.highlighted === row.index)
                           ? Theme.c.themedMenuItem.backgroundHighlighted : "transparent"
                }

                Rectangle {
                    visible: !row.modelData.header
                    x: Theme.spacingMd
                    y: Theme.spacingMd + 2
                    width: 6
                    height: 6
                    radius: 3
                    color: Theme.c.nodeDelegate.accent[row.modelData.accent] ?? Theme.c.nodeDelegate.accent.neutral
                }

                Text {
                    id: title
                    x: Theme.spacingLg + 8
                    y: row.modelData.header ? (parent.height - height) / 2 : Theme.spacingXs
                    width: parent.width - 2 * Theme.spacingLg - 8 - categoryLabel.width - Theme.spacingSm
                    text: row.modelData.label
                    elide: Text.ElideRight
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    font.weight: row.modelData.header ? Font.DemiBold : Font.Normal
                    color: row.modelData.header ? Theme.c.themedMenuItem.sectionForeground
                                                : Theme.c.themedMenuItem.foreground
                }

                Text {
                    id: categoryLabel
                    anchors.top: title.top
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingLg
                    visible: !row.modelData.header
                    width: visible ? implicitWidth : 0
                    text: row.modelData.category || ""
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    color: Theme.c.themedMenuItem.sectionForeground
                }

                Text {
                    visible: !row.modelData.header
                    x: Theme.spacingLg + 8
                    anchors.top: title.bottom
                    width: parent.width - 2 * Theme.spacingLg - 8
                    text: row.modelData.description || ""
                    elide: Text.ElideRight
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    color: Theme.c.themedMenuItem.sectionForeground
                }

                MouseArea {
                    id: area
                    anchors.fill: parent
                    enabled: !row.modelData.header
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onEntered: root.highlighted = row.index
                    onClicked: root.pick(row.modelData)
                }
            }
        }
    }
}
