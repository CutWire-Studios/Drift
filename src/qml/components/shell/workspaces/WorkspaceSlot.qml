pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import Drift

// A leaf of the layout: one panel filling the slot, or several behind a tab strip. Panels
// are only ever re-parented in and out of `body`, never owned by it.
Item {
    id: root

    property var node: ({})
    property var pool: null
    property int activeIndex: node.active ?? 0

    readonly property var entries: root.node.panels ?? []
    readonly property bool fullscreen: (ShellBridge.main?.previewFullscreen ?? false)
                                       && root.Window.window === ShellBridge.main?.fullscreenWindow
    readonly property bool tabbed: root.entries.length > 1 && !root.fullscreen

    anchors.fill: parent

    function place() {
        for (let i = 0; i < root.entries.length; ++i) {
            const entry = root.entries[i]
            const panel = root.pool.panel(entry.type)
            panel.parent = body
            panel.anchors.fill = body
            if (entry.tab && "tab" in panel)
                panel.tab = entry.tab
        }
        root.applyActive()
    }

    function applyActive() {
        for (let i = 0; i < root.entries.length; ++i) {
            const type = root.entries[i].type
            root.pool.panel(type).visible = root.fullscreen ? type === "preview"
                                                            : i === root.activeIndex
        }
    }

    function activate(type) {
        for (let i = 0; i < root.entries.length; ++i) {
            if (root.entries[i].type === type) {
                root.activeIndex = i
                return
            }
        }
    }

    onActiveIndexChanged: applyActive()
    onFullscreenChanged: applyActive()
    Component.onCompleted: place()

    Row {
        id: tabStrip
        visible: root.tabbed
        spacing: Theme.spacingSm
        height: root.tabbed ? Theme.controlHeightSm + Theme.spacingSm : 0

        Repeater {
            model: root.entries
            ThemedChip {
                required property var modelData
                required property int index
                text: root.pool.label(modelData.type)
                selected: index === root.activeIndex
                onClicked: root.activeIndex = index
            }
        }
    }

    Item {
        id: body
        clip: true
        anchors.top: tabStrip.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
    }
}
