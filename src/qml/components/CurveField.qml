import QtQuick
import Drift

// A button showing the current curve; it opens the shared EasingPicker in curve mode, as a popup
// on the desktop shell and as the curve sheet on the phone shell. The site owns the value: it
// feeds `curve` / `handles` in and writes the choice back from curveChosen.
//
// "fade" curves are an id (linear / smooth / equalPower / custom / bezier) plus bezier handles
// [x1, y1, x2, y2]. "text" eases are an id or a CSS cubic-bezier(...) string and have no handles.
Item {
    id: root

    property string domain: "fade"
    property string curve: "linear"
    property var handles: []
    property bool allowFunctions: false
    property bool unitRange: domain === "fade"
    property bool useSheet: ShellBridge.phoneShell

    signal curveChosen(string curve, var handles)

    function r(v) { return Math.round(v * 1000) / 1000 }

    function parseCubic(text) {
        const m = /^cubic-bezier\(\s*([-\d.]+)\s*,\s*([-\d.]+)\s*,\s*([-\d.]+)\s*,\s*([-\d.]+)\s*\)$/.exec(text || "")
        if (!m)
            return null
        return { kind: "bezier", x1: Number(m[1]), y1: Number(m[2]), x2: Number(m[3]), y2: Number(m[4]) }
    }

    readonly property var currentSpec: {
        if (root.domain === "text")
            return root.parseCubic(root.curve)
        if (root.curve === "bezier" && root.handles && root.handles.length === 4)
            return { kind: "bezier", x1: root.handles[0], y1: root.handles[1],
                     x2: root.handles[2], y2: root.handles[3] }
        return null
    }
    readonly property string currentStyle: root.currentSpec ? "" : root.curve
    readonly property var styleList: EditorState.curveStyles(root.domain)
    readonly property var currentStyleEntry: {
        for (const st of root.styleList) {
            if (st.id === root.currentStyle)
                return st
        }
        return null
    }
    readonly property var thumbSamples: root.currentSpec ? EditorState.easingSamples(root.currentSpec, 24)
                                        : (root.currentStyleEntry ? root.currentStyleEntry.samples : [])
    readonly property string curveLabel: {
        if (root.currentStyleEntry)
            return root.currentStyleEntry.label
        if (!root.currentSpec)
            return qsTr("Custom")
        for (const p of EditorState.easingPresets()) {
            const s = p.spec
            if (s.kind === "bezier" && Math.abs(s.x1 - root.currentSpec.x1) < 1e-3
                    && Math.abs(s.y1 - root.currentSpec.y1) < 1e-3
                    && Math.abs(s.x2 - root.currentSpec.x2) < 1e-3
                    && Math.abs(s.y2 - root.currentSpec.y2) < 1e-3)
                return p.label
        }
        return qsTr("Bezier")
    }

    function applyCurve(spec) {
        if (spec.kind === "bezier") {
            if (root.domain === "text")
                root.curveChosen("cubic-bezier(" + r(spec.x1) + "," + r(spec.y1) + ","
                                 + r(spec.x2) + "," + r(spec.y2) + ")", [])
            else
                root.curveChosen("bezier", [r(spec.x1), r(spec.y1), r(spec.x2), r(spec.y2)])
        } else {
            root.curveChosen("linear", [])
        }
    }

    function applyStyle(id) {
        root.curveChosen(id, [])
        if (id === "custom" && popupLoader.item)
            popupLoader.item.close()
    }

    function openPicker() {
        const config = { domain: root.domain, allowFunctions: root.allowFunctions,
                         unitRange: root.unitRange }
        if (root.useSheet) {
            ShellBridge.curveSheetRequested(config, root)
            return
        }
        popupLoader.active = true
        popupLoader.item.openForCurve(config, root)
    }

    implicitWidth: 160
    implicitHeight: Theme.touchInput ? 40 : 30

    Rectangle {
        anchors.fill: parent
        radius: Theme.c.easingPicker.radius
        color: fieldArea.containsMouse ? Theme.c.easingPicker.tileBackgroundHover
                                       : Theme.c.easingPicker.tileBackground
        border.width: Theme.c.easingPicker.borderWidth
        border.color: Theme.c.easingPicker.border
    }

    EasingThumb {
        id: thumb
        x: Theme.spacingXs
        anchors.verticalCenter: parent.verticalCenter
        width: 32
        height: root.height - 8
        showBackground: false
        samples: root.thumbSamples
    }

    Text {
        anchors.left: thumb.right
        anchors.leftMargin: Theme.spacingSm
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingSm
        anchors.verticalCenter: parent.verticalCenter
        text: root.curveLabel
        elide: Text.ElideRight
        color: Theme.c.easingPicker.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeSm
    }

    MouseArea {
        id: fieldArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.openPicker()
    }

    Loader {
        id: popupLoader
        active: false
        sourceComponent: EasingPickerPopup {
            anchorItem: root
        }
    }
}
