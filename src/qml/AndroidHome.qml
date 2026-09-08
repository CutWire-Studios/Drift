import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import Drift
import "components"

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

    readonly property string startDestination: "projects"
    property string current: "projects"

    readonly property bool needsAttention: {
        const win = root.Window.window
        return (win ? win.addonAttentionNeeded : false) || Updates.updateAvailable
    }

    function showDestination(destinationId) {
        root.current = destinationId
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
            onOpenProjectRequested: root.openProjectRequested()
            onOpenRecentRequested: (path) => root.openRecentRequested(path)
        }

        // Phase B fills this with AndroidMarket. Loaded lazily because the store is a
        // network surface nobody has asked for until they select the destination.
        Loader {
            id: marketLoader
            active: root.current === "market"
            asynchronous: true

            sourceComponent: Item {
                ThemedLabel {
                    anchors.centerIn: parent
                    text: qsTr("Coming soon")
                    tone: "muted"
                }
            }
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
