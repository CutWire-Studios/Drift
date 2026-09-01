import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

// Path trail back to the bin root for the folder currently being viewed. Walks
// BinFolderModel.folderById up through parentId; only shown once folders exist.
Row {
    id: root

    property string currentFolderId: ""
    signal navigate(string folderId)

    spacing: 4
    visible: BinFolderModel.count > 0
    // Row sizes itself from its Repeater's content regardless of `visible`; collapse it
    // explicitly so an invisible breadcrumb doesn't leave a gap before the search field.
    height: visible ? implicitHeight : 0

    // folderById() is a plain invokable call, not a property read, so on its own it gives
    // `trail` nothing to depend on — the crumb would only ever refresh when currentFolderId
    // itself changes, never when a folder along the trail is renamed (live, or reverted via
    // undo/redo) while it's still showing. BinFolderModel.dataChanged is the model's own signal
    // for exactly that, now that it actually fires for renames.
    property int _refreshTick: 0
    Connections {
        target: BinFolderModel
        function onDataChanged() { root._refreshTick++ }
    }

    readonly property var trail: {
        void root._refreshTick
        const chain = []
        const visited = new Set()
        let id = root.currentFolderId
        // Project deserialization doesn't reject a self- or mutually-parented folder; an
        // undetected cycle here would spin forever and hang the UI, so bail on a repeat id.
        while (id !== "" && !visited.has(id)) {
            visited.add(id)
            const folder = BinFolderModel.folderById(id)
            if (!folder || Object.keys(folder).length === 0)
                break
            chain.unshift(folder)
            id = folder.parentId
        }
        return [{id: "", name: qsTr("Media")}].concat(chain)
    }

    Repeater {
        model: root.trail
        delegate: Row {
            required property var modelData
            required property int index
            spacing: 4

            IconGlyph {
                visible: index > 0
                anchors.verticalCenter: crumbLabel.verticalCenter
                glyph: Theme.icons.chevronRight
                iconSize: Theme.iconSizeSm
                iconColor: Theme.mutedForeground
            }

            Text {
                id: crumbLabel
                text: modelData.name
                color: modelData.id === root.currentFolderId
                       ? Theme.panelForeground : Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                font.weight: modelData.id === root.currentFolderId ? Font.Medium : Font.Normal

                MouseArea {
                    anchors.fill: parent
                    cursorShape: modelData.id === root.currentFolderId
                                 ? Qt.ArrowCursor : Qt.PointingHandCursor
                    enabled: modelData.id !== root.currentFolderId
                    onClicked: {
                        Haptics.select()
                        root.navigate(modelData.id)
                    }
                }
            }
        }
    }
}
