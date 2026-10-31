import QtQuick
import Drift

Item {
    implicitWidth: 960
    implicitHeight: 640

    readonly property var toolWindow: layoutChooserDialog

    LayoutChooserDialog {
        id: layoutChooserDialog
    }
}
