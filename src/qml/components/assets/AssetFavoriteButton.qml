import QtQuick
import Drift
import ".."

// Star toggle for marking an asset-browser item as a favorite.
IconButton {
    id: root

    property string tabId: ""
    property string itemId: ""

    property bool favorited: false

    glyph: Theme.icons.star
    variant: "ghost"
    buttonSize: 18
    iconSize: 12
    active: favorited
    tooltip: favorited ? qsTr("Remove from favorites") : qsTr("Add to favorites")

    Component.onCompleted: favorited = EditorState.isAssetFavorite(tabId, itemId)

    Connections {
        target: EditorState
        function onAssetFavoritesChanged() {
            root.favorited = EditorState.isAssetFavorite(root.tabId, root.itemId)
        }
    }

    onClicked: EditorState.toggleAssetFavorite(tabId, itemId)
}
