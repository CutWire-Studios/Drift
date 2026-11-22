import QtQuick
import Drift

// Quiet "Open in X ↗" link for a panel header. `target` is a ShellBridge.goDeeper() context.
// Hidden on the phone shell and when the destination panel is already on screen.
Item {
    id: root

    property var target: ({})
    property string label: ""
    property bool active: true
    // Lets a link sit vertically centred in a row of taller controls.
    property real rowHeight: 0

    signal triggered()

    readonly property string _workspaceId: {
        void Workspaces.activeId
        return target.workspace ?? (target.panel ? Workspaces.workspaceWithPanel(target.panel) : "")
    }
    readonly property string _workspaceName: {
        const list = Workspaces.workspaces
        for (let i = 0; i < list.length; ++i) {
            if (list[i].id === _workspaceId)
                return list[i].name
        }
        return ""
    }
    readonly property bool _alreadyShown: {
        void Workspaces.activeId
        const pool = ShellBridge.main?.panelPool
        if (target.panel && pool)
            return pool.inLayout(target.panel)
        return _workspaceId === Workspaces.activeId
    }

    visible: active && !ShellBridge.phoneShell && !Theme.phone && !_alreadyShown && (label.length > 0 || _workspaceName.length > 0)
    implicitWidth: linkText.implicitWidth
    implicitHeight: Math.max(linkText.implicitHeight, rowHeight)

    Accessible.role: Accessible.Link
    Accessible.name: linkText.text

    Text {
        id: linkText
        anchors.verticalCenter: parent.verticalCenter
        text: root.label.length > 0 ? root.label : qsTr("Open in %1 ↗").arg(root._workspaceName)
        color: area.containsMouse ? Theme.c.workspaceLink.foregroundHover : Theme.c.workspaceLink.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
        font.underline: area.containsMouse
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            if (ShellBridge.goDeeper(root.target))
                root.triggered()
        }
    }
}
