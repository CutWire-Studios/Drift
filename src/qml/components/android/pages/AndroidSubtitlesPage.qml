import QtQuick
import Drift
// Subtitles tab of the properties sheet on the phone tool page. The editor scrolls its own cue
// list; tapping a cue raises the keyboard composer, which AndroidEditor keeps above this page.
Item {
    id: root

    property var scaffold: null
    property int trackIndex: -1
    property int clipIndex: -1

    readonly property var clipData: {
        void EditorState.selectionRevision
        void EditorState.tracksRevision
        return EditorState.selectedClipData
    }

    SubtitleEditor {
        anchors.fill: parent
        clip: root.clipData
        compactEdit: true
        onCueEditRequested: index => ShellBridge.main.editorPage.openCueComposer(index)
    }
}
