import QtQuick
import QtQuick.Window
import Drift

SettingsPage {
    id: page

    pageId: "about"
    title: qsTr("About")

    SettingsRow {
        title: qsTr("Drift")
        searchId: "about-version"
        description: qsTr("Version %1").arg(Updates.currentVersion)
        query: page.query
    }

    SettingsGroupHeader {
        text: qsTr("Updates")
        available: Updates.supported
        query: page.query
    }

    SettingsRow {
        title: qsTr("Check for updates on startup")
        searchId: "about-check-updates-on-startup"
        description: qsTr("Once a day, check whether a newer Drift has been released.")
        keywords: "update version release newer"
        available: Updates.supported
        query: page.query

        ThemedSwitch {
            checked: Updates.enabled
            onToggled: Updates.enabled = checked
        }
    }

    SettingsRow {
        title: qsTr("Check for updates")
        searchId: "about-check-for-updates"
        description: Updates.status
        keywords: "update version release newer download"
        available: Updates.supported
        query: page.query

        ThemedButton {
            variant: "secondary"
            glyph: Theme.icons.refresh
            text: Updates.checking ? qsTr("Checking…")
                  : Updates.downloading ? qsTr("Downloading…")
                  : qsTr("Check now")
            enabled: !Updates.checking && !Updates.downloading && !Updates.preparing
            onClicked: Updates.checkNow()
        }
    }

    SettingsGroupHeader {
        text: qsTr("Help")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Quick Setup")
        searchId: "about-quick-setup"
        description: qsTr("Walk through language, theme, interface size and add-ons again.")
        keywords: "setup first run welcome wizard onboarding language theme scale addons"
        available: EditorState.preferences.canRestart
        query: page.query

        ThemedButton {
            variant: "secondary"
            text: qsTr("Run again")
            onClicked: ShellBridge.main.rerunQuickSetup()
        }
    }

    SettingsRow {
        title: qsTr("About Drift")
        searchId: "about-drift"
        description: qsTr("Open-source video editor by CutWire Studios.")
        keywords: "license credits version"
        query: page.query

        ThemedButton {
            variant: "secondary"
            text: qsTr("Show about dialog")
            onClicked: ShellBridge.main.openAboutDialog()
        }
    }

    SettingsRow {
        title: qsTr("Debug info")
        searchId: "about-debug-info"
        description: qsTr("System, playback and codec details for bug reports.")
        keywords: "diagnostics bug report system codec gpu"
        query: page.query

        ThemedButton {
            variant: "secondary"
            text: qsTr("Open debug info…")
            onClicked: ShellBridge.main.openDebugInfo()
        }
    }
}
