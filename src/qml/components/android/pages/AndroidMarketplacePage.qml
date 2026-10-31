import QtQuick
import Drift

// The Marketplace as a full phone page. Pushed on AndroidMain's stack like Settings and Extras;
// Back closes an open listing first, then the page.
AndroidSubPage {
    id: root

    property alias view: marketView

    title: qsTr("Marketplace")

    function handleBack() {
        return marketView.handleBack()
    }

    MarketplaceView {
        id: marketView
        anchors.fill: parent
        compact: true
    }
}
