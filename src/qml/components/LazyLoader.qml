import QtQuick

// Holds a dialog, popup or window that is only built when first opened and, unless
// keepLoaded is set, destroyed again once it hides. Call ensure() to get the item
// (creating it if needed), e.g. `settingsLoader.ensure().open()`.
// Synchronous on purpose: callers use the returned item in the same tick.
Loader {
    id: root

    active: false

    // Keep the item after its first close, for one that holds results worth coming back to.
    property bool keepLoaded: false

    readonly property bool shown: root.item !== null && root.item.visible

    function ensure() {
        root.active = true
        return root.item
    }

    function releaseIfHidden() {
        if (root.item && !root.item.visible)
            root.active = false
    }

    // Deferred so the item's own closed/closing handlers finish before it is destroyed,
    // and so a reopen in the same tick keeps it.
    Connections {
        target: root.keepLoaded ? null : root.item
        ignoreUnknownSignals: true
        function onVisibleChanged() {
            if (!root.item.visible)
                Qt.callLater(root.releaseIfHidden)
        }
    }
}
