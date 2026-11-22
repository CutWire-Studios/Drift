import QtQuick
import Drift

Item {
    id: helpButton
    implicitWidth: helpBtn.implicitWidth
    implicitHeight: helpBtn.implicitHeight
    width: implicitWidth
    height: implicitHeight
    anchors.verticalCenter: parent.verticalCenter

    IconButton {
        id: helpBtn
        anchors.fill: parent
        glyph: Theme.icons.help
        variant: "ghost"
        text: qsTr("Help")
        active: helpMenu.opened
        tooltip: qsTr("Help and support")
        tooltipBody: qsTr("Guides, keyboard shortcuts, updates and diagnostics")
        onClicked: helpMenu.popup(0, helpButton.height + Theme.spacingMd)
    }

    AppSettingsMenu {
        id: helpMenu
        onHelpRequested: ShellBridge.main.openHelpDialog()
        onShortcutsRequested: ShellBridge.main.openSettings("keymap")
        onUpdatesRequested: ShellBridge.main.checkForUpdates()
        onDebugInfoRequested: ShellBridge.main.openDebugInfo()
        onAboutRequested: ShellBridge.main.openAboutDialog()
    }
}
