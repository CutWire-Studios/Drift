import QtQuick
import Drift

Item {
    id: root
    implicitWidth: 1500
    implicitHeight: 760

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
            EditorState.color.setParam("exposure", 0.25)
            EditorState.color.setParam("contrast", 0.3)
            EditorState.color.setParam("saturation", 1.3)
            EditorState.color.setParam("temperature", -0.25)
            EditorState.color.setVec2("lift", -0.12, -0.18)
            EditorState.color.setVec2("gamma", 0.05, 0.08)
            EditorState.color.setVec2("gain", 0.25, 0.2)
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
            width: parent.width * 0.5
            height: parent.height
        }
        ColorPanel {
            width: parent.width * 0.5
            height: parent.height
            tab: 1
            shown: true
        }
    }
}
