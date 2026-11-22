import QtQuick
import Drift

// One saved range on the strip's range band. Drag the body to move it and either edge to resize
// it; the change is committed (one undo step) when the drag ends.
Item {
    id: root

    required property var modelData
    required property var strip

    property real liveIn: root.modelData.in
    property real liveOut: root.modelData.out
    readonly property color tint: root.modelData.rating > 0 ? Theme.c.sourceLabStrip.rangeFavorite
                                  : root.modelData.rating < 0 ? Theme.c.sourceLabStrip.rangeReject
                                                              : Theme.c.sourceLabStrip.rangeNeutral
    readonly property real minLength: root.strip.minSpan

    function commit() {
        if (Math.abs(root.liveIn - root.modelData.in) > 1e-6 || Math.abs(root.liveOut - root.modelData.out) > 1e-6)
            EditorState.sourceLab.updateRange(root.modelData.id, root.liveIn, root.liveOut)
        root.liveIn = Qt.binding(() => root.modelData.in)
        root.liveOut = Qt.binding(() => root.modelData.out)
    }

    x: root.strip.xOf(root.liveIn)
    width: Math.max(3, root.strip.xOf(root.liveOut) - x)
    height: parent.height

    Rectangle {
        anchors.fill: parent
        color: Qt.alpha(root.tint, Theme.c.sourceLabStrip.rangeOpacity)
        border.width: Theme.c.sourceLabStrip.rangeBorderWidth
        border.color: root.tint
    }

    Text {
        anchors.fill: parent
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        text: root.modelData.name
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeTick
        color: Theme.c.themedLabel.foreground
    }

    MouseArea {
        id: body
        anchors.fill: parent
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        property real grabX: 0
        property real startIn: 0
        property real startOut: 0
        property bool moved: false
        onPressed: (mouse) => {
            grabX = mapToItem(root.parent, mouse.x, mouse.y).x
            startIn = root.liveIn
            startOut = root.liveOut
            moved = false
        }
        onPositionChanged: (mouse) => {
            if (!pressed)
                return
            const dx = mapToItem(root.parent, mouse.x, mouse.y).x - grabX
            if (!moved && Math.abs(dx) < 3)
                return
            moved = true
            const length = startOut - startIn
            const snapped = root.strip.snap(startIn + dx / root.strip.pxPerSecond, "range:" + root.modelData.id)
            const start = Math.max(0, Math.min(root.strip.panel.duration - length, snapped))
            root.liveIn = start
            root.liveOut = start + length
        }
        onReleased: (mouse) => {
            if (moved)
                root.commit()
            else
                EditorState.assetPreview.seek(root.strip.snap(root.strip.tOf(mapToItem(root.parent, mouse.x, mouse.y).x), ""))
        }
    }

    SourceLabHandle {
        centerX: 0
        barWidth: 4
        color: root.tint
        onMoved: (parentX) => {
            const t = root.strip.snap(root.strip.tOf(root.x + parentX), "range:" + root.modelData.id)
            root.liveIn = Math.max(0, Math.min(root.liveOut - root.minLength, t))
        }
        onReleased: root.commit()
    }
    SourceLabHandle {
        centerX: root.width
        barWidth: 4
        color: root.tint
        onMoved: (parentX) => {
            const t = root.strip.snap(root.strip.tOf(root.x + parentX), "range:" + root.modelData.id)
            root.liveOut = Math.min(root.strip.panel.duration, Math.max(root.liveIn + root.minLength, t))
        }
        onReleased: root.commit()
    }
}
