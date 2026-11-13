import QtQuick
import Drift
// Transitions tab: the shared picker grid with draggable cards, dragged onto clip overlaps.
Item {
    id: root

    property alias catalogTick: grid.catalogTick
    readonly property alias categories: grid.categories
    readonly property alias activeCategory: grid.activeCategory
    function showCategory(id) { grid.showCategory(id) }
    property alias searchText: grid.searchText

    TransitionPickerGrid {
        id: grid
        anchors.fill: parent
        draggable: true

        IconButton {
            glyph: Theme.icons.download
            variant: "ghost"
            buttonSize: Theme.controlHeight
            tooltip: qsTr("Install a custom transition or effect from a .driftfx or zip made in Drift Forge")
            onClicked: ShellBridge.main.importUserPackage("")
        }
    }
}
