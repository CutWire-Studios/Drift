import QtQuick
import Drift

Item {
    id: root
    implicitWidth: 1600
    implicitHeight: 900

    property bool settled: false
    readonly property bool previewReady: mock.ready && root.settled

    PreviewMockData {
        id: mock
        
    }

    Connections {
        target: mock
        function onReadyChanged() {
            if (!mock.ready)
                return
            const idx = n => AssetLibrary.indexOfId(AssetLibrary.assetIdForPath(PreviewMediaDir + "/" + n))
            const d = AssetLibrary.assetAt(idx("sunset-waves.mp4")).durationSeconds
            EditorState.addClipFromAssetAt(idx("sunset-waves.mp4"), 0, 0)
            EditorState.addClipFromAssetAt(idx("shanghai-skyline.mp4"), 0, d)
            EditorState.selectClip(0, 0)
            EditorState.playheadSeconds = 3.0
            Workspaces.activeId = "color"
            settleTimer.start()
        }
    }

    Timer {
        id: settleTimer
        interval: 2500
        onTriggered: root.settled = true
    }

    EditorWorkspace {
        anchors.fill: parent
    }
}
