import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift

// A secondary window of a workspace. Independent of the main window (no transient parent,
// not modal) so it can sit on another monitor; its panels come from the shared pool.
ApplicationWindow {
    id: root

    required property var pool
    property string workspaceId: ""
    property string windowId: ""
    property var entry: ({})

    // Closed by the user: the manager parks this window's panels and drops it.
    signal closedByUser(string windowId)

    readonly property alias host: host

    property bool _placed: false

    // Created with an item of the main window as parent, which would otherwise make the main
    // window its transient parent and keep this window stacked above it.
    transientParent: null

    palette.window: Theme.c.appWindow.paletteWindow
    palette.windowText: Theme.c.appWindow.paletteWindowText
    palette.base: Theme.c.appWindow.paletteBase
    palette.text: Theme.c.appWindow.paletteText
    palette.button: Theme.c.appWindow.paletteButton
    palette.buttonText: Theme.c.appWindow.paletteButtonText
    palette.placeholderText: Theme.c.appWindow.palettePlaceholder
    palette.dark: Theme.c.appWindow.paletteDark
    palette.mid: Theme.c.appWindow.paletteMid
    palette.midlight: Theme.c.appWindow.paletteMidlight
    palette.light: Theme.c.appWindow.paletteLight
    palette.shadow: Theme.c.appWindow.paletteShadow
    palette.highlight: Theme.c.appWindow.paletteHighlight
    palette.highlightedText: Theme.c.appWindow.paletteHighlightedText
    palette.toolTipBase: Theme.c.appWindow.paletteToolTipBase
    palette.toolTipText: Theme.c.appWindow.paletteToolTipText

    title: root.entry.title || Workspaces.workspace(root.workspaceId).name || qsTr("Drift")
    width: 960
    height: 600
    minimumWidth: 320
    minimumHeight: 240
    visibility: Window.Hidden
    color: {
        const opacity = ThemeManager.window.backgroundOpacity
        const multiplier = (opacity === undefined || opacity === null) ? 1 : opacity
        return WindowBackdrop.windowBackground(Theme.c.appWindow.background, multiplier)
    }

    function build() {
        root.host.build()
    }

    function _screenNamed(name) {
        const screens = Qt.application.screens
        for (let i = 0; i < screens.length; ++i) {
            if (screens[i].name === name)
                return screens[i]
        }
        return Qt.application.primaryScreen ?? screens[0]
    }

    function place() {
        const target = root._screenNamed(root.entry.screen ?? "")
        if (target)
            root.screen = target
        const saved = root.entry.geometry
        const area = target ? Qt.rect(target.virtualX, target.virtualY, target.width, target.height)
                            : Qt.rect(0, 0, 1920, 1080)
        let rect = null
        if (saved && saved.length === 4 && saved[2] > 0 && saved[3] > 0) {
            rect = Qt.rect(saved[0], saved[1], saved[2], saved[3])
            const overlaps = rect.x < area.x + area.width && rect.x + rect.width > area.x
                             && rect.y < area.y + area.height && rect.y + rect.height > area.y
            if (!overlaps)
                rect = null
        }
        if (!rect) {
            const w = Math.min(area.width, Math.max(root.minimumWidth, Math.round(area.width * 0.7)))
            const h = Math.min(area.height, Math.max(root.minimumHeight, Math.round(area.height * 0.7)))
            rect = Qt.rect(area.x + Math.round((area.width - w) / 2),
                           area.y + Math.round((area.height - h) / 2), w, h)
        }
        root.x = rect.x
        root.y = rect.y
        root.width = rect.width
        root.height = rect.height
        root._placed = true
        root.visibility = root.entry.fullscreen ? Window.FullScreen : Window.Windowed
    }

    function saveGeometry() {
        if (!root._placed || root.visibility === Window.FullScreen || root.visibility === Window.Hidden
                || root.visibility === Window.Minimized || root.visibility === Window.Maximized)
            return
        Workspaces.setWindowGeometry(root.workspaceId, root.windowId,
                                     Qt.rect(root.x, root.y, root.width, root.height),
                                     root.screen ? root.screen.name : "")
    }

    Timer {
        id: saveTimer
        interval: 500
        onTriggered: root.saveGeometry()
    }

    onXChanged: saveTimer.restart()
    onYChanged: saveTimer.restart()
    onWidthChanged: saveTimer.restart()
    onHeightChanged: saveTimer.restart()

    onClosing: {
        root.saveGeometry()
        root.closedByUser(root.windowId)
    }

    Component.onCompleted: {
        WindowBackdrop.attach(root)
        root.place()
    }

    // Arrow-key actions are matched on the focused item rather than as shortcuts (see Main.qml),
    // so this window needs the same hook the main editor scope has.
    FocusScope {
        anchors.fill: parent
        focus: true
        enabled: !(ShellBridge.main?.showStartScreen ?? false)
        Keys.onPressed: function(event) {
            if (!ShellBridge.main || !Theme.isArrowKey(event.key))
                return
            if (ShellBridge.main.dispatchChord(EditorState.keymap.chordFromKey(event.key, event.modifiers,
                                                                              event.nativeScanCode, event.text)))
                event.accepted = true
        }

        WorkspaceHost {
            id: host
            anchors.fill: parent
            pool: root.pool
            workspaceId: root.workspaceId
            windowId: root.windowId
            sideMargin: (ShellBridge.main?.previewFullscreen ?? false) && ShellBridge.main.fullscreenWindow === root
                        ? 0 : Theme.panelGap
        }
    }
}
