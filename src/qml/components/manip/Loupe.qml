import QtQuick
import Drift

// A magnified patch of the preview around a touch point, drawn above the finger so the point
// being placed stays visible. Parent it to the overlay: `at` is where the finger is in the
// parent's coordinates and `sourcePoint` is the same spot in `source`'s.
Item {
    id: loupe

    // The item to magnify, normally the preview canvas.
    property Item source
    property point sourcePoint
    property point at
    property bool active: false
    property real zoom: 2.5
    property real diameter: 96

    visible: active && source !== null
    width: diameter
    height: diameter
    z: 110

    readonly property real lift: Theme.androidMinTouchTarget * 1.25
    x: Math.max(0, Math.min(parent ? parent.width - width : 0, at.x - width / 2))
    y: {
        const above = at.y - lift - height
        return above >= 0 ? above : at.y + lift * 0.6
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.c.manipLoupe.background
        border.width: Theme.c.manipLoupe.borderWidth
        border.color: Theme.c.manipLoupe.border
    }

    ShaderEffectSource {
        anchors.fill: parent
        anchors.margins: Theme.c.manipLoupe.borderWidth
        visible: loupe.visible
        sourceItem: loupe.source
        live: true
        hideSource: false
        sourceRect: Qt.rect(loupe.sourcePoint.x - width / loupe.zoom / 2,
                            loupe.sourcePoint.y - height / loupe.zoom / 2,
                            width / loupe.zoom, height / loupe.zoom)
    }

    Rectangle {
        anchors.centerIn: parent
        width: 1
        height: parent.height * 0.4
        color: Theme.c.manipLoupe.crosshair
    }
    Rectangle {
        anchors.centerIn: parent
        width: parent.width * 0.4
        height: 1
        color: Theme.c.manipLoupe.crosshair
    }
}
