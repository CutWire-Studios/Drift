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
            EditorState.addTrack("audio")
            EditorState.addClipFromAssetAt(root.idx("music.mp3"), 0, 0)
            EditorState.selectClip(0, 0)
            const cat = EditorState.audioEffectCatalog()
            console.error("afx " + JSON.stringify(cat.map(e => e.id)))
            for (const e of cat.filter(e => /\.(eq|compressor)$/i.test(e.id)).slice(0, 2)) EditorState.addAudioEffect(0, 0, e.id)
            EditorState.startFocusedClipLoudness(false, -14)
            Workspaces.activeId = "audio"
    }

    function post() {
            EditorState.selectClip(0, 1)
            console.error("audio sel " + EditorState.selectedTrack + "," + EditorState.selectedClip + " " + JSON.stringify(EditorState.selectedClipData.name))
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
        interval: 8000
        onTriggered: root.settled = true
    }

    EditorWorkspace {
        id: ws
        anchors.fill: parent
    }
}
