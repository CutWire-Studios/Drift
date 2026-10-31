import QtQuick
import QtQuick.Controls.Basic
import Drift

// The Extras store in its own window, so a download outlives the panel that started it. On
// Android a ToolWindow is a full-screen popup, which is what the desktop-shell tablet gets.
ToolWindow {
    id: root

    width: 1100
    height: 760
    minimumWidth: 720
    minimumHeight: 520
    title: qsTr("Extras")
    color: Theme.c.extrasStore.background

    // target: "" | category id | "addon:<id>" | "kind:<kind>" | "installed" | "updates"
    function open(target) {
        root.visible = true
        root.raise()
        root.requestActivate()
        store.open(target || "")
    }

    function show() {
        root.open("")
    }

    function importUserPackage(url) {
        store.importUserPackage(url)
    }

    Shortcut {
        sequence: "Escape"
        enabled: root.visible
        onActivated: {
            if (!store.handleBack())
                root.close()
        }
    }

    ExtrasStore {
        id: store
        anchors.fill: parent
        onShowRequested: {
            root.visible = true
            root.raise()
            root.requestActivate()
        }
    }
}
