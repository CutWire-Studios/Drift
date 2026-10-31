import QtQuick
import Drift

// Mock media for preview fixtures. Imports the files in tests/data/preview-media and places
// the two clips on the open project's timeline. `ready` stays false until the
// placement is done; PreviewHost holds the screenshot until then.
QtObject {
    id: root

    // Also imports captions.srt over the first clip, so the Subtitles panel has cues to show.
    property bool subtitles: false

    readonly property bool ready: root._placed
    property bool _placed: false

    function _indexOf(name) {
        return AssetLibrary.indexOfId(AssetLibrary.assetIdForPath(PreviewMediaDir + "/" + name))
    }

    function _tryPlace() {
        if (root._placed || AssetLibrary.importing)
            return
        const talking = root._indexOf("sunset-waves.mp4")
        const broll = root._indexOf("shanghai-skyline.mp4")
        if (talking < 0 || broll < 0)
            return
        // Placing before the probe finishes uses a placeholder length, which leaves a gap.
        const talkingId = AssetLibrary.assetIdAt(talking)
        const brollId = AssetLibrary.assetIdAt(broll)
        if (AssetLibrary.isImportPending(talkingId) || AssetLibrary.isImportPending(brollId))
            return
        const talkingSeconds = AssetLibrary.assetAt(talking).durationSeconds
        if (!(talkingSeconds > 0))
            return
        // The project's Video 1 track sits below the text track; the second clip follows the first.
        EditorState.addClipFromAssetAt(talking, 1, 0)
        EditorState.addClipFromAssetAt(broll, 1, talkingSeconds)
        // Fixtures that edit the selected clip show the first clip.
        EditorState.selectClip(1, 0)
        EditorState.sourceLab.load(AssetLibrary.assetIdAt(talking))
        if (root.subtitles)
            EditorState.importSubtitleFile("file://" + PreviewMediaDir + "/captions.srt", 0)
        root._placed = true
    }

    // Probe completion has no signal here, so keep checking until the clips are placed.
    readonly property Timer _poll: Timer {
        interval: 100
        repeat: true
        running: !root._placed
        onTriggered: root._tryPlace()
    }

    readonly property Connections _library: Connections {
        target: AssetLibrary
        function onImportingChanged() { root._tryPlace() }
        function onCountChanged() { root._tryPlace() }
    }

    Component.onCompleted: {
        const names = ["sunset-waves.mp4", "shanghai-skyline.mp4", "music.mp3"]
        AssetLibrary.importUrlsAsync(names.map(name => "file://" + PreviewMediaDir + "/" + name))
    }
}
