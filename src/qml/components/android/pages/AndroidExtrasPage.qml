import QtQuick
import Drift

// The Extras store as a full phone page. Pushed on AndroidMain's stack like Settings and the
// Marketplace; Back closes an open detail or clears the selection first, then the page.
AndroidSubPage {
    id: root

    property alias store: extrasStore
    // Same values as ExtrasStore.open(): "" | category id | "addon:<id>" | "kind:<kind>" | "installed" | "updates"
    property string target: ""

    title: qsTr("Extras")

    function handleBack() {
        return extrasStore.handleBack()
    }

    function importUserPackage(url) {
        extrasStore.importUserPackage(url)
    }

    Component.onCompleted: extrasStore.open(root.target)

    ExtrasStore {
        id: extrasStore
        anchors.fill: parent
        compact: true
    }
}
