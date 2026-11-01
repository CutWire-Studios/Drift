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
            root.tid = EditorState.tracking.addPointTracker(0, 0, 0.455, 0.17)
            EditorState.tracking.setTrackerProperty(0, 0, root.tid, "minConfidence", 0.2)
            Workspaces.activeId = "masking-tracking"
    }

    function post() {
            EditorState.selectClip(0, 0)
            EditorState.playheadSeconds = 3.0
            EditorState.addMaskToClip(0, 0, "ellipse")
            EditorState.masking.setMaskProperty(EditorState.selectedTrack, EditorState.selectedClip, "feather", 28)
            EditorState.preview.maskViewMode = "overlay"
            EditorState.tracking.track(0, 0, root.tid, true, false)
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
        interval: 3000
        onTriggered: {
            root.post()
            settleTimer.start()
        }
    }

    Timer {
        id: settleTimer
        interval: 12000
        onTriggered: root.settled = true
    }

    EditorWorkspace {
        id: ws
        anchors.fill: parent
    }
}
