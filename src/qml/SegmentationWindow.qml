import QtQuick
import QtQuick.Window
import Drift

// Cutout surface, for whichever backend is installed. Opened for one clip: pick a reference frame,
// mark the subject if the backend takes prompts, then run the pass over the whole clip.
//
// SAM2 is prompted and cuts out anything; RVM finds people on its own and has nothing to click, so
// the prompt overlay is bound to segmentBackendUsesPoints rather than shown unconditionally.
ToolWindow {
    id: root
    property alias frameSlider: frameColumn.frameSlider
    property alias backendBox: sidebar.backendBox
    property alias qualityBox: sidebar.qualityBox

    property int trackIndex: -1
    property int clipIndex: -1
    property real clipStartSeconds: 0
    property real clipDurationSeconds: 0

    width: 1100
    height: 720
    minimumWidth: 780
    minimumHeight: 520
    title: qsTr("Remove background")
    color: Theme.c.segmentationWindow.background

    function openFor(track, clip, startSeconds, durationSeconds, sessionAlreadyStarted) {
        root.trackIndex = track
        root.clipIndex = clip
        root.clipStartSeconds = startSeconds
        root.clipDurationSeconds = durationSeconds
        frameSlider.value = 0
        if (!sessionAlreadyStarted)
            EditorState.segmentation.beginSession(track, clip, startSeconds)
        root.show()
        root.raise()
        root.requestActivate()
    }

    onClosing: EditorState.segmentation.endSession()

    // Rebuilt rather than bound: segmentationBackends() and rvmQualities() are plain calls, so
    // nothing would re-evaluate them when an addon is installed while this window is open.
    property var backendModel: []
    property var qualityModel: []

    function refreshBackends() {
        const installed = EditorState.segmentation.backends()
        const backends = []
        if (installed.indexOf("sam2") >= 0)
            backends.push({ label: qsTr("Anything (click to pick)"), value: "sam2" })
        if (installed.indexOf("rvm") >= 0)
            backends.push({ label: qsTr("People (automatic)"), value: "rvm" })
        root.backendModel = backends

        const qualities = []
        const variants = EditorState.segmentation.rvmQualities()
        for (let i = 0; i < variants.length; ++i) {
            qualities.push({
                label: variants[i] === "resnet50" ? qsTr("Best quality (slower)") : qsTr("Fast"),
                value: variants[i]
            })
        }
        root.qualityModel = qualities
    }

    Component.onCompleted: root.refreshBackends()

    Connections {
        target: Addons
        function onKindChanged(kind) {
            if (kind === "sam2-model" || kind === "rvm-model")
                root.refreshBackends()
        }
    }

    Connections {
        target: EditorState.segmentation
        function onFinished(ok, message) {
            if (ok)
                root.close()
        }
        // The session corrects a remembered backend that is no longer installed, so the control
        // follows the session rather than the other way round.
        function onSessionChanged() {
            backendBox.currentIndex = backendBox.indexOfValue(EditorState.segmentation.backend)
        }
    }

    Row {
        anchors.fill: parent
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingLg

        SegmentationFrameColumn {
            id: frameColumn
            tool: root
            sidebarWidth: sidebar.width
        }

        // ----- Controls --------------------------------------------------------------------
        SegmentationControlColumn {
            id: sidebar
            tool: root
        }
    }
}
