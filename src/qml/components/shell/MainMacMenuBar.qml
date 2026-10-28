import Qt.labs.platform as Platform
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift

Platform.MenuBar {
    id: menuBar
    property ApplicationWindow appWindow
    property var header
    property var assets
    property LazyLoader downloadsLoader
    window: menuBar.appWindow

    Platform.Menu {
        title: qsTr("&File")

        Platform.MenuItem {
            text: qsTr("&New Project")
            shortcut: StandardKey.New
            onTriggered: menuBar.appWindow.requestNewProject()
        }
        Platform.MenuItem {
            text: qsTr("&Open Project…")
            shortcut: StandardKey.Open
            onTriggered: menuBar.appWindow.requestOpenProjectDialog()
        }
        Platform.MenuSeparator {}
        Platform.MenuItem {
            text: qsTr("&Save Project")
            shortcut: StandardKey.Save
            enabled: !menuBar.appWindow.showStartScreen
            onTriggered: menuBar.header.saveProject()
        }
        Platform.MenuItem {
            text: qsTr("Save Project &As…")
            shortcut: StandardKey.SaveAs
            enabled: !menuBar.appWindow.showStartScreen
            onTriggered: menuBar.header.saveProjectAs()
        }
        Platform.MenuItem {
            text: qsTr("Save Project &JSON…")
            enabled: !menuBar.appWindow.showStartScreen
            onTriggered: menuBar.header.saveProjectJson()
        }
        Platform.MenuItem {
            text: qsTr("Open Project JSON…")
            onTriggered: menuBar.header.openProjectJson()
        }
        Platform.MenuSeparator {}
        Platform.MenuItem {
            text: qsTr("&Export Video…")
            enabled: !menuBar.appWindow.showStartScreen && !EditorState.exportInProgress
            onTriggered: menuBar.header.exportVideo()
        }
        Platform.MenuItem {
            text: qsTr("&Package Project…")
            enabled: !menuBar.appWindow.showStartScreen
            onTriggered: menuBar.header.packageProject()
        }
        Platform.MenuSeparator {}
        Platform.MenuItem {
            text: qsTr("&Close Project")
            shortcut: StandardKey.Close
            enabled: !menuBar.appWindow.showStartScreen
            onTriggered: menuBar.appWindow.requestCloseProject()
        }
    }

    Platform.Menu {
        title: qsTr("&Edit")

        Platform.MenuItem {
            text: qsTr("&Undo")
            shortcut: StandardKey.Undo
            enabled: !menuBar.appWindow.showStartScreen && EditorState.canUndo
            onTriggered: menuBar.appWindow.dispatchAction("undo")
        }
        Platform.MenuItem {
            text: qsTr("&Redo")
            shortcut: StandardKey.Redo
            enabled: !menuBar.appWindow.showStartScreen && EditorState.canRedo
            onTriggered: menuBar.appWindow.dispatchAction("redo")
        }
        Platform.MenuSeparator {}
        Platform.MenuItem {
            text: qsTr("Cu&t")
            shortcut: StandardKey.Cut
            enabled: !menuBar.appWindow.showStartScreen
            onTriggered: menuBar.appWindow.dispatchAction("cut")
        }
        Platform.MenuItem {
            text: qsTr("&Copy")
            shortcut: StandardKey.Copy
            enabled: !menuBar.appWindow.showStartScreen
            onTriggered: menuBar.appWindow.dispatchAction("copy")
        }
        Platform.MenuItem {
            text: qsTr("&Paste")
            shortcut: StandardKey.Paste
            enabled: !menuBar.appWindow.showStartScreen
            onTriggered: menuBar.appWindow.dispatchAction("paste")
        }
        Platform.MenuItem {
            text: qsTr("&Delete")
            shortcut: StandardKey.Delete
            enabled: !menuBar.appWindow.showStartScreen
            onTriggered: menuBar.appWindow.dispatchAction("delete")
        }
        Platform.MenuItem {
            text: qsTr("Select &All")
            shortcut: StandardKey.SelectAll
            enabled: !menuBar.appWindow.showStartScreen
            onTriggered: menuBar.appWindow.dispatchAction("selectAll")
        }
        Platform.MenuItem {
            text: qsTr("Clear Selection")
            enabled: !menuBar.appWindow.showStartScreen
            onTriggered: menuBar.appWindow.dispatchAction("clearSelection")
        }
        Platform.MenuSeparator {}
        Platform.MenuItem {
            text: qsTr("Split Clip")
            shortcut: "S"
            enabled: !menuBar.appWindow.showStartScreen
            onTriggered: menuBar.appWindow.dispatchAction("split")
        }
        Platform.MenuItem {
            text: qsTr("Duplicate Clip")
            shortcut: "Ctrl+D"
            enabled: !menuBar.appWindow.showStartScreen
            onTriggered: menuBar.appWindow.dispatchAction("duplicate")
        }
        Platform.MenuItem {
            text: qsTr("Copy Effects")
            enabled: !menuBar.appWindow.showStartScreen
            onTriggered: menuBar.appWindow.dispatchAction("copyEffects")
        }
        Platform.MenuItem {
            text: qsTr("Paste Effects")
            enabled: !menuBar.appWindow.showStartScreen
            onTriggered: menuBar.appWindow.dispatchAction("pasteEffects")
        }
        Platform.MenuItem {
            text: qsTr("Paste Attributes…")
            shortcut: "Ctrl+Alt+V"
            enabled: !menuBar.appWindow.showStartScreen
            onTriggered: menuBar.appWindow.openPasteAttributes()
        }
        Platform.MenuSeparator {}
        Platform.MenuItem {
            text: qsTr("About Drift")
            role: Platform.MenuItem.AboutRole
            onTriggered: menuBar.appWindow.openAboutDialog()
        }
        Platform.MenuItem {
            text: qsTr("Settings…")
            shortcut: StandardKey.Preferences
            role: Platform.MenuItem.ApplicationSpecificRole
            onTriggered: menuBar.appWindow.openSettings()
        }
        Platform.MenuItem {
            text: qsTr("Check for Updates…")
            role: Platform.MenuItem.ApplicationSpecificRole
            onTriggered: menuBar.appWindow.checkForUpdates()
        }
    }

    Platform.Menu {
        title: qsTr("&Playback")

        Platform.MenuItem {
            text: qsTr("Play / Pause")
            shortcut: "Space"
            enabled: !menuBar.appWindow.showStartScreen
            onTriggered: menuBar.appWindow.dispatchAction("playPause")
        }
        Platform.MenuSeparator {}
        Platform.MenuItem {
            text: qsTr("Step Back One Frame")
            shortcut: "Left"
            enabled: !menuBar.appWindow.showStartScreen
            onTriggered: menuBar.appWindow.dispatchAction("stepBack")
        }
        Platform.MenuItem {
            text: qsTr("Step Forward One Frame")
            shortcut: "Right"
            enabled: !menuBar.appWindow.showStartScreen
            onTriggered: menuBar.appWindow.dispatchAction("stepForward")
        }
        Platform.MenuItem {
            text: qsTr("Previous Cut Point")
            shortcut: "Up"
            enabled: !menuBar.appWindow.showStartScreen
            onTriggered: menuBar.appWindow.dispatchAction("previousEdit")
        }
        Platform.MenuItem {
            text: qsTr("Next Cut Point")
            shortcut: "Down"
            enabled: !menuBar.appWindow.showStartScreen
            onTriggered: menuBar.appWindow.dispatchAction("nextEdit")
        }
        Platform.MenuItem {
            text: qsTr("Go to Start of Timeline")
            shortcut: "Home"
            enabled: !menuBar.appWindow.showStartScreen
            onTriggered: menuBar.appWindow.dispatchAction("goToStart")
        }
        Platform.MenuSeparator {}
        Platform.MenuItem {
            text: qsTr("Toggle Bookmark")
            shortcut: "M"
            enabled: !menuBar.appWindow.showStartScreen
            onTriggered: menuBar.appWindow.dispatchAction("toggleBookmark")
        }
        Platform.MenuItem {
            text: qsTr("Next Bookmark")
            enabled: !menuBar.appWindow.showStartScreen
            onTriggered: menuBar.appWindow.dispatchAction("nextBookmark")
        }
        Platform.MenuItem {
            text: qsTr("Previous Bookmark")
            enabled: !menuBar.appWindow.showStartScreen
            onTriggered: menuBar.appWindow.dispatchAction("previousBookmark")
        }
    }

    Platform.Menu {
        title: qsTr("&View")

        Platform.MenuItem {
            text: qsTr("Zoom &In")
            shortcut: StandardKey.ZoomIn
            enabled: !menuBar.appWindow.showStartScreen
            onTriggered: menuBar.appWindow.dispatchAction("zoomIn")
        }
        Platform.MenuItem {
            text: qsTr("Zoom &Out")
            shortcut: StandardKey.ZoomOut
            enabled: !menuBar.appWindow.showStartScreen
            onTriggered: menuBar.appWindow.dispatchAction("zoomOut")
        }
        Platform.MenuSeparator {}
        Platform.MenuItem {
            text: qsTr("Toggle Fullscreen Preview")
            enabled: !menuBar.appWindow.showStartScreen
            onTriggered: menuBar.appWindow.togglePreviewFullscreen()
        }
        Platform.MenuItem {
            text: qsTr("Toggle Guides")
            enabled: !menuBar.appWindow.showStartScreen
            onTriggered: menuBar.appWindow.dispatchAction("toggleGuides")
        }
        Platform.MenuSeparator {}
        Platform.MenuItem {
            text: qsTr("Switch to Mobile Layout…")
            onTriggered: menuBar.appWindow.switchShell("mobile")
        }
    }

    Platform.Menu {
        id: windowMenu
        title: qsTr("&Window")

        Instantiator {
            model: Workspaces.workspaces

            Platform.MenuItem {
                required property var modelData
                text: modelData.name
                checkable: true
                checked: modelData.id === Workspaces.activeId
                onTriggered: Workspaces.activeId = modelData.id
            }

            onObjectAdded: (index, object) => windowMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => windowMenu.removeItem(object)
        }
        Platform.MenuItem {
            text: qsTr("Customize Layouts in Settings…")
            onTriggered: Qt.callLater(() => menuBar.appWindow.openSettings("workspaces"))
        }
        Platform.MenuSeparator {}
        Platform.MenuItem {
            text: qsTr("Landscape Workspace")
            checkable: true
            checked: EditorState.preferences.workspaceLayoutOverridden && EditorState.preferences.workspaceLayoutPreferred === "landscape"
            onTriggered: EditorState.preferences.setWorkspaceLayoutPreference("landscape")
        }
        Platform.MenuItem {
            text: qsTr("Portrait Workspace")
            checkable: true
            checked: EditorState.preferences.workspaceLayoutOverridden && EditorState.preferences.workspaceLayoutPreferred === "portrait"
            onTriggered: EditorState.preferences.setWorkspaceLayoutPreference("portrait")
        }
        Platform.MenuItem {
            text: qsTr("Auto Workspace (Follow Canvas)")
            checkable: true
            checked: !EditorState.preferences.workspaceLayoutOverridden
            onTriggered: EditorState.preferences.clearWorkspaceLayoutPreference()
        }
        Platform.MenuSeparator {}
        Platform.MenuItem {
            text: qsTr("Downloads")
            onTriggered: menuBar.downloadsLoader.ensure().show()
        }
        Platform.MenuItem {
            text: qsTr("Multicam")
            onTriggered: menuBar.appWindow.openMulticam()
        }
    }

    Platform.Menu {
        title: qsTr("&Help")

        Platform.MenuItem {
            text: qsTr("Keyboard Shortcuts")
            shortcut: "F1"
            onTriggered: menuBar.appWindow.openSettings("keymap")
        }
        Platform.MenuItem {
            text: qsTr("Extras…")
            onTriggered: menuBar.appWindow.openExtras()
        }
        Platform.MenuSeparator {}
        Platform.MenuItem {
            text: qsTr("Debug Info…")
            onTriggered: menuBar.appWindow.openDebugInfo()
        }
    }
}
