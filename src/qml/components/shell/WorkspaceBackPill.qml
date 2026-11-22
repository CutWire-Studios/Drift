import QtQuick
import QtQuick.Controls.Basic
import Drift

// "← Back to <workspace>" after a ShellBridge.goDeeper() jump; hidden otherwise.
AbstractButton {
    id: root

    readonly property string _name: {
        const list = Workspaces.workspaces
        for (let i = 0; i < list.length; ++i) {
            if (list[i].id === ShellBridge.returnWorkspaceId)
                return list[i].name
        }
        return ""
    }

    visible: ShellBridge.returnWorkspaceId !== "" && _name.length > 0
    hoverEnabled: true
    implicitHeight: Theme.controlHeightSm
    implicitWidth: label.implicitWidth + Theme.spacingLg * 2
    text: qsTr("← Back to %1").arg(_name)

    Accessible.role: Accessible.Button
    Accessible.name: text

    onClicked: ShellBridge.goBack()

    background: Rectangle {
        radius: Theme.radiusPill
        color: Theme.c.workspaceBackPill.background
        border.width: Theme.c.workspaceBackPill.borderWidth
        border.color: Theme.c.workspaceBackPill.border
        opacity: root.hovered ? 1 : 0.85
    }

    contentItem: Text {
        id: label
        text: root.text
        color: Theme.c.workspaceBackPill.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
