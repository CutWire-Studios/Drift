import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

// Drags the guides of the set being edited. Lines and margin edges move; a line
// dragged off the canvas is removed; dragging from the top or left strip adds a
// horizontal or vertical line. Aspect frames are always centred, so they are
// edited in the popover only. Covers the whole viewport so a drag can leave the
// canvas; only the left button is taken, so middle-drag pan and wheel zoom still
// reach the viewport underneath.
Item {
    id: root

    property var previewCanvas

    readonly property string setId: EditorState.guideEditSetId
    readonly property var editedSet: {
        const sets = EditorState.guideSets
        for (let i = 0; i < sets.length; ++i) {
            if (sets[i].id === setId)
                return sets[i]
        }
        return null
    }
    readonly property var items: editedSet ? editedSet.items : []

    readonly property real cx: previewCanvas ? previewCanvas.x : 0
    readonly property real cy: previewCanvas ? previewCanvas.y : 0
    readonly property real cw: previewCanvas ? Math.max(1, previewCanvas.width) : 1
    readonly property real ch: previewCanvas ? Math.max(1, previewCanvas.height) : 1

    readonly property real hitTolPx: 6
    readonly property real stripPx: 14
    // How far past the canvas edge a line has to go before releasing removes it.
    readonly property real removeMarginPx: 8

    // {id, key, axis, at, from, to}: the edge under the pointer or being dragged.
    property var hover: null
    property var drag: null
    property bool dragIsNew: false
    property bool dragRemoves: false
    property real dragValue: 0

    function hitTest(px, py) {
        let best = null
        let bestDist = hitTolPx
        const consider = function (t, p) {
            const d = Math.abs(p - t.at)
            const along = t.axis === "x" ? py : px
            if (d < bestDist && along >= t.from - hitTolPx && along <= t.to + hitTolPx) {
                bestDist = d
                best = t
            }
        }
        for (const item of items) {
            if (item.locked)
                continue
            if (item.kind === "v") {
                consider({ id: item.id, key: "pos", axis: "x", at: cx + item.pos * cw, from: cy, to: cy + ch }, px)
            } else if (item.kind === "h") {
                consider({ id: item.id, key: "pos", axis: "y", at: cy + item.pos * ch, from: cx, to: cx + cw }, py)
            } else if (item.kind === "rect") {
                const x0 = cx + item.left * cw
                const x1 = cx + (1 - item.right) * cw
                const y0 = cy + item.top * ch
                const y1 = cy + (1 - item.bottom) * ch
                consider({ id: item.id, key: "left", axis: "x", at: x0, from: y0, to: y1 }, px)
                consider({ id: item.id, key: "right", axis: "x", at: x1, from: y0, to: y1 }, px)
                consider({ id: item.id, key: "top", axis: "y", at: y0, from: x0, to: x1 }, py)
                consider({ id: item.id, key: "bottom", axis: "y", at: y1, from: x0, to: x1 }, py)
            }
        }
        return best
    }

    function inTopStrip(px, py) {
        return px >= cx && px <= cx + cw && py >= cy && py <= cy + stripPx
    }

    function inLeftStrip(px, py) {
        return px >= cx && px <= cx + stripPx && py >= cy && py <= cy + ch
    }

    // Pointer position to the dragged property's value, clamped as the controller clamps it.
    function valueAt(px, py, shift) {
        let f = drag.axis === "x" ? (px - cx) / cw : (py - cy) / ch
        if (shift)
            f = Math.round(f * 100) / 100
        if (drag.key === "pos")
            return Math.max(0, Math.min(1, f))
        if (drag.key === "left" || drag.key === "top")
            return Math.max(0, Math.min(0.5, f))
        return Math.max(0, Math.min(0.5, 1 - f))
    }

    function beginDrag(target, isNew) {
        drag = target
        dragIsNew = isNew
        dragRemoves = false
        hover = null
    }

    function moveDrag(px, py, shift) {
        dragValue = valueAt(px, py, shift)
        const at = drag.axis === "x" ? px : py
        const lo = drag.axis === "x" ? cx : cy
        const hi = drag.axis === "x" ? cx + cw : cy + ch
        dragRemoves = drag.key === "pos" && (at < lo - removeMarginPx || at > hi + removeMarginPx)
        EditorState.setGuideItemProperty(setId, drag.id, drag.key, dragValue)
    }

    function endDrag(px, py) {
        // A click on a strip adds nothing: the new line only stays once dragged out of it.
        const clickedStrip = dragIsNew && (drag.axis === "y" ? inTopStrip(px, py) : inLeftStrip(px, py))
        if (dragRemoves || clickedStrip)
            EditorState.removeGuideItem(setId, drag.id)
        drag = null
        dragIsNew = false
        dragRemoves = false
    }

    // Where the highlighted edge sits: the live value while dragging, the hit position on hover.
    function edgeAt(t) {
        if (t !== drag)
            return t.at
        const size = t.axis === "x" ? cw : ch
        const origin = t.axis === "x" ? cx : cy
        const f = (t.key === "right" || t.key === "bottom") ? 1 - dragValue : dragValue
        return origin + f * size
    }

    readonly property var shown: drag || hover

    Rectangle {
        x: root.cx
        y: root.cy
        width: root.cw
        height: root.stripPx
        color: Theme.panelBackground
        opacity: 0.6
    }

    Rectangle {
        x: root.cx
        y: root.cy + root.stripPx
        width: root.stripPx
        height: root.ch - root.stripPx
        color: Theme.panelBackground
        opacity: 0.6
    }

    Rectangle {
        visible: root.shown !== null
        readonly property bool vertical: root.shown ? root.shown.axis === "x" : false
        x: !root.shown ? 0 : vertical ? root.edgeAt(root.shown) - 1 : root.shown.from
        y: !root.shown ? 0 : vertical ? root.shown.from : root.edgeAt(root.shown) - 1
        width: !root.shown ? 0 : vertical ? 3 : root.shown.to - root.shown.from
        height: !root.shown ? 0 : vertical ? root.shown.to - root.shown.from : 3
        color: root.dragRemoves ? Theme.destructive : Theme.primary
    }

    Rectangle {
        id: readout
        visible: root.drag !== null
        x: Math.min(root.width - width, mouseArea.mouseX + Theme.spacingLg)
        y: Math.min(root.height - height, mouseArea.mouseY + Theme.spacingLg)
        width: readoutText.implicitWidth + Theme.spacingMd * 2
        height: readoutText.implicitHeight + Theme.spacingSm * 2
        radius: Theme.radiusSm
        color: Theme.panelBackground
        border.width: Theme.borderWidth
        border.color: Theme.panelBorder

        Text {
            id: readoutText
            anchors.centerIn: parent
            text: root.dragRemoves ? qsTr("Release to remove")
                                   : (root.dragValue * 100).toFixed(1) + "%"
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        hoverEnabled: true
        cursorShape: {
            const t = root.drag || root.hover
            if (t)
                return t.axis === "x" ? Qt.SplitHCursor : Qt.SplitVCursor
            if (root.inTopStrip(mouseX, mouseY))
                return Qt.SplitVCursor
            if (root.inLeftStrip(mouseX, mouseY))
                return Qt.SplitHCursor
            return Qt.ArrowCursor
        }

        onPressed: (mouse) => {
            const hit = root.hitTest(mouse.x, mouse.y)
            if (hit) {
                root.beginDrag(hit, false)
                root.dragValue = root.valueAt(mouse.x, mouse.y, false)
                return
            }
            const top = root.inTopStrip(mouse.x, mouse.y)
            if (!top && !root.inLeftStrip(mouse.x, mouse.y))
                return
            const id = EditorState.addGuideItem(root.setId, top ? "h" : "v")
            root.beginDrag({ id: id, key: "pos", axis: top ? "y" : "x",
                             at: 0, from: top ? root.cx : root.cy,
                             to: top ? root.cx + root.cw : root.cy + root.ch }, true)
            root.moveDrag(mouse.x, mouse.y, mouse.modifiers & Qt.ShiftModifier)
        }
        onPositionChanged: (mouse) => {
            if (root.drag)
                root.moveDrag(mouse.x, mouse.y, mouse.modifiers & Qt.ShiftModifier)
            else
                root.hover = root.hitTest(mouse.x, mouse.y)
        }
        onReleased: (mouse) => {
            if (root.drag)
                root.endDrag(mouse.x, mouse.y)
        }
        onExited: if (!root.drag) root.hover = null
    }

    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spacingLg
        width: barRow.implicitWidth + Theme.spacingLg * 2
        height: barRow.implicitHeight + Theme.spacingMd * 2
        radius: Theme.radiusMd
        color: Theme.panelBackground
        border.width: Theme.borderWidth
        border.color: Theme.panelBorder

        Row {
            id: barRow
            anchors.centerIn: parent
            spacing: Theme.spacingLg

            Column {
                anchors.verticalCenter: parent.verticalCenter

                Text {
                    text: qsTr("Editing %1").arg(root.editedSet ? root.editedSet.name : "")
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm
                    font.weight: Font.Medium
                }
                Text {
                    width: Math.min(implicitWidth, root.width * 0.6)
                    wrapMode: Text.WordWrap
                    text: qsTr("Drag from the top or left edge to add a guide, off the canvas to remove one. Shift steps by 1%.")
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
            }

            ThemedButton {
                anchors.verticalCenter: parent.verticalCenter
                variant: "primary"
                text: qsTr("Done")
                onClicked: EditorState.guideEditSetId = ""
            }
        }
    }
}
