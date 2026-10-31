import QtQuick
import Drift

Item {
    implicitWidth: 960
    implicitHeight: 640

    readonly property var toolWindow: settingsDialog

    SettingsDialog {
        id: settingsDialog
    }
}
