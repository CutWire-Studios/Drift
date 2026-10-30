import QtQuick
import Drift
// The Marketplace in its own window, so a download outlives the panel that started it. The
// pages come from the server layout; Downloads is the last tab. On Android a ToolWindow is a
// full-screen popup, which is what the desktop-shell tablet gets too.
ToolWindow {
    id: root

    width: 1100
    height: 760
    minimumWidth: 720
    minimumHeight: 520
    title: qsTr("Marketplace")
    color: Theme.c.marketplace.background

    // Whatever page was last shown when called with no arguments.
    function open(pageOrTab, source, itemId) {
        root.visible = true
        root.raise()
        root.requestActivate()
        view.open(pageOrTab || "", source || "", itemId || "")
    }

    function show() {
        root.open("")
    }

    MarketplaceView {
        id: view
        anchors.fill: parent
    }
}
