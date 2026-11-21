import QtQuick
import Drift

PanelFrame {
    id: root
    SearchAnchor {
        searchId: "denoise-panel"
        title: qsTr("Remove noise")
        keywords: "denoise noise reduction clean audio hiss noise voice"
    }

    // Set by PanelPool. The panel only runs the model while it is on screen.
    property bool shown: false
    property bool denoiseReady: EditorState.denoise.available()
    property bool runtimeReady: Addons.runtimeAvailable()
    property string loadedKey: ""

    readonly property var clipData: EditorState.selectedClipData
    readonly property bool eligible: EditorState.selectedTrack >= 0 && EditorState.selectedClip >= 0
                                     && (clipData.kind === "audio" || clipData.kind === "video")
                                     && !clipData.sequenceId
    readonly property bool installed: denoiseReady && runtimeReady

    function keyFor(track, clip) {
        return track + ":" + clip
    }

    function openClip(track, clip, durationSeconds) {
        if (!root.installed)
            return
        root.loadedKey = root.keyFor(track, clip)
        view.openFor(track, clip, durationSeconds)
    }

    function openFor(track, clip, durationSeconds) {
        if (root.loadedKey === root.keyFor(track, clip) && view.busy)
            return
        root.openClip(track, clip, durationSeconds)
    }

    function release() {
        view.release()
        root.loadedKey = ""
    }

    function followSelection() {
        if (!root.shown || view.busy)
            return
        if (!root.installed || !root.eligible) {
            if (root.loadedKey.length > 0)
                root.release()
            return
        }
        const key = root.keyFor(EditorState.selectedTrack, EditorState.selectedClip)
        if (key !== root.loadedKey)
            root.openClip(EditorState.selectedTrack, EditorState.selectedClip,
                          clipData.duration !== undefined ? clipData.duration : 0)
    }

    onShownChanged: {
        if (root.shown)
            root.followSelection()
        else
            root.release()
    }
    onInstalledChanged: root.followSelection()
    onClipDataChanged: root.followSelection()

    Connections {
        target: EditorState
        function onSelectionChanged() { root.followSelection() }
    }

    Connections {
        target: view
        function onBusyChanged() { root.followSelection() }
    }

    Connections {
        target: Addons
        function onKindChanged(kind) {
            if (kind === "denoise-model")
                root.denoiseReady = EditorState.denoise.available()
            else if (kind === "onnxruntime")
                root.runtimeReady = Addons.runtimeAvailable()
        }
    }

    AssetsPanelHeader {
        id: header
        width: parent.width
        height: Theme.panelHeaderHeight
        title: qsTr("Noise Removal")
    }

    DenoiseView {
        id: view
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        visible: root.loadedKey.length > 0
        showCancel: false
        onApplied: {
            root.loadedKey = ""
            root.followSelection()
        }
    }

    EmptyState {
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingLg
        visible: root.loadedKey.length === 0
        glyph: Theme.icons.info
        title: !root.installed
               ? (root.runtimeReady ? qsTr("Download noise removal (about 9 MB)")
                                    : qsTr("Install AI engine first"))
               : qsTr("Select an audio or video clip to remove noise")
        actionText: root.installed ? "" : qsTr("Open Add-ons")
        onActionTriggered: ShellBridge.main.openAddonManager(
            root.runtimeReady ? "denoise-model" : "onnxruntime")
    }
}
