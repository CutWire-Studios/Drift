import QtQuick
import Drift

// Fallback when no workspace has room for a panel: the panel shows in its own window. Closing
// it parks the panel back in the pool. The panel is never destroyed here.
ToolWindow {
    id: root

    property string type: ""
    property var pool: null

    title: root.pool ? root.pool.label(root.type) : ""
    width: 720
    height: 540
    minimumWidth: 320
    minimumHeight: 240

    WorkspaceSlot {
        id: slot
        node: ({ panels: [{ type: root.type }] })
        pool: root.pool
    }

    function openPanel() {
        if (!root.visible)
            slot.place()
        root.show()
        root.raise()
        root.requestActivate()
    }

    onVisibleChanged: {
        if (root.visible)
            return
        const panel = root.pool.panel(root.type)
        if (panel.parent && panel.parent.parent === slot) {
            panel.anchors.fill = null
            panel.parent = root.pool
        }
    }
}
