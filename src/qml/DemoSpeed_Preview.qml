import QtQuick
import Drift

Item {
    id: root
    implicitWidth: 900
    implicitHeight: 720

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
    }

    function post() {
            speedCurveWindow.openFor(0, 0)
            EditorState.speedCurve.setPoints([
                {pos: 0, speed: 1, inDx: 0, inDy: 0, outDx: 0, outDy: 0, corner: false},
                {pos: 0.3, speed: 3.2, inDx: 0, inDy: 0, outDx: 0, outDy: 0, corner: false},
                {pos: 0.65, speed: 0.35, inDx: 0, inDy: 0, outDx: 0, outDy: 0, corner: false},
                {pos: 1, speed: 1, inDx: 0, inDy: 0, outDx: 0, outDy: 0, corner: false}])
            speedCurveWindow.points = EditorState.speedCurve.points
            console.error("speed pts " + JSON.stringify(EditorState.speedCurve.points))
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
        interval: 9000
        onTriggered: root.settled = true
    }

    readonly property var toolWindow: speedCurveWindow

    SpeedCurveWindow {
        id: speedCurveWindow
    }
}
