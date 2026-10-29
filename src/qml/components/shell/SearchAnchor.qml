import QtQuick
import Drift

// Marks `target` (its parent by default) as a global search target; the navigator scrolls to it
// and flashes it. Invisible, so a Column, Row or Layout skips it and it can sit anywhere.
Item {
    id: root

    property string searchId: ""
    property string title: ""
    property string keywords: ""
    // Set by the search index generator; overrides where a result opens.
    property string searchTarget: ""
    property Item target: parent

    visible: false

    Component.onCompleted: {
        if (root.searchId !== "" && root.target)
            ShellBridge.registerAnchor(root.searchId, root.target)
    }
    Component.onDestruction: {
        if (root.searchId !== "" && root.target)
            ShellBridge.unregisterAnchor(root.searchId, root.target)
    }
}
