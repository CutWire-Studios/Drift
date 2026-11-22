import QtQuick
import QtQuick.Controls.Basic
import Drift

// The app-level Help menu. None of it needs a project, so the editor header and the start
// screen both offer it; each wires the signals to its own window.
ThemedContextMenu {
    id: root
    implicitWidth: 248

    signal helpRequested()
    signal shortcutsRequested()
    signal updatesRequested()
    signal debugInfoRequested()
    signal aboutRequested()

    ThemedMenuItem {
        text: qsTr("Help")
        icon.name: Theme.icons.help
        onTriggered: root.helpRequested()
    }

    ThemedMenuItem {
        text: qsTr("Keyboard shortcuts")
        icon.name: Theme.icons.keyboard
        shortcutId: "openKeymap"
        onTriggered: root.shortcutsRequested()
    }

    ThemedMenuItem {
        text: qsTr("Check for updates…")
        icon.name: Theme.icons.download
        onTriggered: root.updatesRequested()
    }

    ThemedMenuItem {
        text: qsTr("Save diagnostics…")
        icon.name: Theme.icons.fileText
        onTriggered: {
            CrashReports.saveDiagnostics()
            if (CrashReports.saveMessage.length > 0)
                Toasts.info(CrashReports.saveMessage)
        }
    }

    ThemedMenuItem {
        text: qsTr("Debug info…")
        icon.name: Theme.icons.bug
        onTriggered: root.debugInfoRequested()
    }

    ThemedMenuSeparator { }

    ThemedMenuItem {
        text: qsTr("About Drift")
        icon.name: Theme.icons.info
        onTriggered: root.aboutRequested()
    }
}
