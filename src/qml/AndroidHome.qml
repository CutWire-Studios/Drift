import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import Drift

// The home shell: a bottom-nav host over three destinations.
//
// This used to be one scrolling page whose middle band was the layout picker, so the first
// thing on screen was a question about aspect ratios rather than the user's own work. Now
// Projects is what you land on, and the two things that had nowhere else to live — the store
// and the app's own settings — are peers rather than entries buried in the editor's overflow.
Item {
    id: root

    signal enterEditor()
    signal openProjectRequested()
    signal openRecentRequested(string path)
    signal newProjectRequested()
    signal quickEditRequested()

    readonly property string startDestination: "projects"
    property string current: "projects"

    readonly property bool needsAttention: {
        const win = root.Window.window
        return (win ? win.addonAttentionNeeded : false) || Updates.updateAvailable
    }

    function showDestination(destinationId) {
        if (destinationId === "market")
            marketLoader.active = true
        root.current = destinationId
    }

    // A shared link. Switches to Market and hands the url to it — parked first, because the
    // loader is asynchronous and on a cold start from a share the page does not exist yet.
    property string _pendingLinkUrl: ""

    function startLinkImport(url) {
        root._pendingLinkUrl = url
        root.showDestination("market")
        root._flushPendingLink()
    }

    function _flushPendingLink() {
        if (root._pendingLinkUrl === "" || !marketLoader.item)
            return
        const url = root._pendingLinkUrl
        root._pendingLinkUrl = ""
        marketLoader.item.startLinkImport(url)
    }

    // Android's convention: Back from a secondary destination returns to the start
    // destination rather than leaving the app. Market's own drill-down unwinds first, so a
    // Back inside the store does not skip past it straight to Projects.
    function handleBack() {
        const market = marketLoader.item
        if (market && market.handleBack !== undefined && market.handleBack())
            return true
        if (root.current !== root.startDestination) {
            root.current = root.startDestination
            return true
        }
        return false
    }

    readonly property var destinationIds: ["projects", "market", "me"]

    StackLayout {
        id: pages
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: nav.top
        // StackLayout keeps every destination alive, so each one holds its own scroll
        // position across a switch instead of snapping back to the top.
        currentIndex: Math.max(0, root.destinationIds.indexOf(root.current))

        AndroidProjectsPage {
            onNewProjectRequested: root.newProjectRequested()
            onQuickEditRequested: root.quickEditRequested()
            onOpenProjectRequested: root.openProjectRequested()
            onOpenRecentRequested: (path) => root.openRecentRequested(path)
        }

        // Lazy, because the store is a network surface nobody has asked for until they select
        // the destination — but latched, not unloaded on the way out. Binding `active` straight
        // to the current destination destroyed the page on every switch away, which threw out
        // the results and re-fetched the catalog on the way back; the sibling destinations are
        // kept alive by the StackLayout for exactly that reason, and this matches them.
        Loader {
            id: marketLoader
            active: false
            asynchronous: true

            sourceComponent: AndroidMarket { }

            onLoaded: root._flushPendingLink()
        }

        AndroidMePage { }
    }

    AndroidHomeNav {
        id: nav
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        current: root.current
        attention: root.needsAttention
        onSelected: (destinationId) => root.showDestination(destinationId)
    }
}
