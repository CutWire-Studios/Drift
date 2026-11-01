import QtQuick
import Drift

Item {
    id: root
    implicitWidth: 1600
    implicitHeight: 800

    readonly property string demo: "/tmp/Drift Demo"
    property string tid: ""
    property bool started: false
    property bool settled: false
    readonly property bool previewReady: root.settled

    function idx(n) {
        return AssetLibrary.indexOfId(AssetLibrary.assetIdForPath(root.demo + "/" + n))
    }

    function setup() {
            const r = EditorState.addModel3dClip(root.demo + "/BoxAnimated.glb", 0, 0, {"rotX": 25, "rotY": -35, "scale": 0.55})
            console.error("model3d " + r.ok + " " + (r.error || ""))
            EditorState.selectClip(0, 0)
    }

    function post() {
        
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

    Row {
        anchors.fill: parent
        PreviewPanel {
            width: parent.width * 0.62
            height: parent.height
        }
        Model3DInspector {
            width: parent.width * 0.38
            height: parent.height
        }
    }
}
