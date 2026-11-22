import QtQuick
import Drift
// Grid, list, tree and sort toggles that share the media search row.
Row {
    id: root

    property bool gridMode: false
    property string viewMode: "grid"
    property bool treeMode: false
    property bool sortByKind: false

    signal sortToggled()

    spacing: Theme.spacingSm

    // One button that steps grid -> list -> tree on a phone; it shows the mode you are in and
    // names the one the next tap gives.
    readonly property bool cycling: Theme.touchInput && Theme.compact
    readonly property string currentMode: root.treeMode ? "tree" : root.viewMode === "list" ? "list" : "grid"
    readonly property string nextMode: currentMode === "grid" ? "list" : currentMode === "list" ? "tree" : "grid"

    IconButton {
        visible: root.cycling
        glyph: root.currentMode === "tree" ? Theme.icons.listTree
               : root.currentMode === "list" ? Theme.icons.list : Theme.icons.layoutGrid
        variant: "ghost"
        tooltip: root.nextMode === "list" ? qsTr("Switch to list view")
                 : root.nextMode === "tree" ? qsTr("Switch to tree view") : qsTr("Switch to grid view")
        onClicked: EditorState.mediaViewMode = root.nextMode
    }

    IconButton {
        visible: !root.cycling
        glyph: Theme.icons.layoutGrid
        variant: "ghost"
        tooltip: qsTr("Grid view")
        active: root.gridMode
        onClicked: EditorState.mediaViewMode = "grid"
    }

    IconButton {
        visible: !root.cycling
        glyph: Theme.icons.list
        variant: "ghost"
        tooltip: qsTr("List view")
        active: root.viewMode === "list"
        onClicked: EditorState.mediaViewMode = "list"
    }

    IconButton {
        visible: !root.cycling
        glyph: Theme.icons.listTree
        variant: "ghost"
        tooltip: qsTr("Tree view")
        active: root.treeMode
        onClicked: EditorState.mediaViewMode = "tree"
    }

    IconButton {
        glyph: root.sortByKind ? Theme.icons.sortByKind : Theme.icons.sortByName
        variant: "ghost"
        tooltip: root.sortByKind ? qsTr("Sort by name") : qsTr("Sort by type")
        onClicked: root.sortToggled()
    }
}
