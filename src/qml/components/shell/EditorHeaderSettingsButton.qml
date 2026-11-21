import QtQuick
import Drift
import QtQuick.Window

Item {
    id: settingsButton
    implicitWidth: settingsBtn.implicitWidth
    implicitHeight: settingsBtn.implicitHeight
    width: implicitWidth
    height: implicitHeight
    anchors.verticalCenter: parent.verticalCenter

    IconButton {
        id: settingsBtn
        anchors.fill: parent
        glyph: Theme.icons.settings
        variant: "ghost"
        text: qsTr("Settings")
        tooltip: qsTr("Settings")
        tooltipBody: qsTr("Preferences, appearance, shortcuts and workspaces")
        tooltipActionId: "openSettings"
        onClicked: ShellBridge.main.openSettings()
    }
}
