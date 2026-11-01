import QtQuick
import Drift

Item {
    id: root
    implicitWidth: 1600
    implicitHeight: 900

    readonly property string demo: "/tmp/Drift Demo"
    property string tid: ""
    property bool started: false
    property bool settled: false
    readonly property bool previewReady: root.settled

    function idx(n) {
        return AssetLibrary.indexOfId(AssetLibrary.assetIdForPath(root.demo + "/" + n))
    }

    function setup() {
                        const d = AssetLibrary.assetAt(idx("sunset-waves.mp4")).durationSeconds
                        EditorState.addClipFromAssetAt(idx("sunset-waves.mp4"), 0, 0)
                        EditorState.addClipFromAssetAt(idx("shanghai-skyline.mp4"), 0, d)
                        EditorState.selectClip(0, 0)
                        EditorState.playheadSeconds = 3.0
            const k = (p, pts) => pts.forEach(q => EditorState.setClipKeyframe(0, 0, p, q[0], q[1]))
            k("x", [[0, -250], [2, 0], [5, 220], [8, 0]])
            k("y", [[0, 100], [3, -60], [6, 80], [9, 0]])
            k("opacity", [[0, 0], [1.5, 1], [8, 1], [9.8, 0.4]])
            k("rotation", [[0, 0], [4, 10], [8, -6]])
            for (const t of [0, 2, 5, 8]) EditorState.setKeyframeEasingAt(0, 0, "x", t, "easeInOut")
            Workspaces.activeId = "keyframes"
    }

    function post() {
            ws.pool.keyframesPanel.mode = "curves"
            ws.pool.keyframesPanel.fitAll()
    }

    Component.onCompleted: AssetLibrary.importUrlsAsync(
        ["sunset-waves.mp4", "shanghai-skyline.mp4", "music.mp3"].map(n => "file://" + encodeURI(root.demo + "/" + n)))

    Timer {
        interval: 100
        repeat: true
        running: !root.started
        onTriggered: {
            if (AssetLibrary.importing)
                return
            const a = root.idx("sunset-waves.mp4"), b = root.idx("shanghai-skyline.mp4"), c = root.idx("music.mp3")
            if (a < 0 || b < 0 || c < 0)
                return
            if (AssetLibrary.isImportPending(AssetLibrary.assetIdAt(a)) || AssetLibrary.isImportPending(AssetLibrary.assetIdAt(b)))
                return
            if (!(AssetLibrary.assetAt(a).durationSeconds > 0))
                return
            root.started = true
            root.setup()
            postTimer.start()
        }
    }

    Timer {
        id: postTimer
        interval: 1800
        onTriggered: {
            root.post()
            settleTimer.start()
        }
    }

    Timer {
        id: settleTimer
        interval: 5000
        onTriggered: root.settled = true
    }

    EditorWorkspace {
        id: ws
        anchors.fill: parent
    }
}
