import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift

Column {
    id: root
    signal downloadsRequested()
    property alias editorHeader: editorHeader
    property alias pool: panelPool
    anchors.fill: parent
    spacing: 0
    // The start screen sits on top of this document rather than replacing it (see
    // the StartScreen instance below), so without this the editor underneath was
    // still fully live — clickable through any gap and, worse, still reachable by
    // keyboard even though nothing of it is visible. Disabling blocks mouse input
    // and keyboard focus for the whole subtree, not just the pointer.
    enabled: !ShellBridge.main?.showStartScreen

    EditorHeader {
        id: editorHeader
        width: parent.width
        visible: !ShellBridge.main?.mainPreviewFullscreen
        onDownloadsRequested: root.downloadsRequested()
    }

    ProjectFolderBanner {
        visible: shown && !ShellBridge.main?.mainPreviewFullscreen
    }

    Item {
        width: parent.width
        // Clamped so the editor body never takes a negative height while the
        // window is being resized toward its minimum.
        height: Math.max(0, parent.height
                            - (ShellBridge.main?.mainPreviewFullscreen ? 0 : Theme.headerHeight))

        PanelPool {
            id: panelPool
        }

        WorkspaceHost {
            id: mainHost
            anchors.fill: parent
            pool: panelPool
            windowId: "main"
            // Edge-to-edge in fullscreen; the usual page padding otherwise.
            sideMargin: ShellBridge.main?.mainPreviewFullscreen ? 0 : Theme.panelGap
        }
    }

    readonly property string effectiveId: ShellBridge.main?.effectiveWorkspaceId ?? Workspaces.activeId

    property var _windows: []
    property var _closed: ({})
    property bool _ready: false
    property bool _forceReapply: false
    property string _appliedSignature: ""

    // Sizes and geometry are written back quietly while the user drags, so they must not
    // count as a change here or every drag would end in a rebuild.
    function _signature(id) {
        return JSON.stringify(Workspaces.workspace(id).windows ?? [],
                              (key, value) => key === "sizes" || key === "geometry" ? undefined : value)
    }

    function _reapply() {
        const force = root._forceReapply
        root._forceReapply = false
        if (force || root._signature(root.effectiveId) !== root._appliedSignature)
            root.restoreWindows()
    }

    Component { id: windowC; WorkspaceWindow {} }

    function _dropWindows() {
        for (const win of root._windows) {
            if (ShellBridge.main?.fullscreenWindow === win)
                ShellBridge.main.togglePreviewFullscreen()
            win.host.clear()
            win.destroy()
        }
        root._windows = []
    }

    // Every panel is parked before any host builds: a panel belongs to one window at a time,
    // so a host building first could take a panel a later host was still holding, or have
    // one it just placed parked from under it. Old secondary windows go once they are empty;
    // the main host builds next, then the secondary windows the workspace still wants.
    function applyWorkspace() {
        root._appliedSignature = root._signature(root.effectiveId)
        panelPool.parkAll()
        mainHost.clear()
        root._dropWindows()

        mainHost.workspaceId = root.effectiveId
        mainHost.build()

        const windows = Workspaces.workspace(root.effectiveId).windows ?? []
        const opened = []
        for (const entry of windows.slice(1)) {
            if (root._closed[entry.id])
                continue
            const win = windowC.createObject(root, {
                pool: panelPool,
                workspaceId: root.effectiveId,
                windowId: entry.id,
                entry: entry
            })
            win.closedByUser.connect(root._windowClosed)
            win.build()
            opened.push(win)
        }
        root._windows = opened
    }

    function _windowClosed(windowId) {
        const closed = Object.assign({}, root._closed)
        closed[windowId] = true
        root._closed = closed
        Qt.callLater(root.applyWorkspace)
    }

    function restoreWindows() {
        root._closed = ({})
        root.applyWorkspace()
    }

    function closeWindows() {
        root._dropWindows()
    }

    onEffectiveIdChanged: if (root._ready) root.restoreWindows()

    Component.onCompleted: {
        root._ready = true
        root.applyWorkspace()
    }

    Connections {
        target: Workspaces
        function onActiveWorkspaceEdited() {
            root._forceReapply = true
            Qt.callLater(root._reapply)
        }

        function onWorkspacesChanged() { Qt.callLater(root._reapply) }
    }
}
