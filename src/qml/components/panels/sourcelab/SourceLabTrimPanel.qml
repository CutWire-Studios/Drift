import QtQuick
import Drift

PanelFrame {
    id: root
    SearchAnchor {
        searchId: "sourcelab-trim"
        title: qsTr("Trim source clip")
        keywords: "trim cut in out start end ranges subclip split scene detection favorite reject marker"
    }

    property bool shown: false

    readonly property var lab: EditorState.sourceLab
    readonly property bool hasAsset: root.lab.currentAssetId.length > 0 && root.lab.durationSeconds > 0
    readonly property bool isAudio: root.lab.kind === "audio"
    readonly property real duration: root.lab.durationSeconds
    readonly property real fps: root.lab.fps > 0 ? root.lab.fps : 30
    readonly property bool hasWaveform: root.isAudio || root.lab.hasAudio
    readonly property real minViewDur: Math.min(root.duration, Math.max(0.05, 10 / root.fps))

    // The strip's window onto the source, in seconds.
    property real viewStart: 0
    property real viewDur: 1
    property bool snapTargets: true
    property bool zeroCrossSnap: false
    property var asset: ({})

    function clampView() {
        root.viewDur = Math.max(root.minViewDur, Math.min(root.duration, root.viewDur))
        root.viewStart = Math.max(0, Math.min(root.duration - root.viewDur, root.viewStart))
    }

    function scrollTo(start) {
        root.viewStart = start
        root.clampView()
    }

    function zoomTo(dur, anchorTime, anchorFraction) {
        root.viewDur = dur
        root.viewStart = anchorTime - anchorFraction * root.viewDur
        root.clampView()
    }

    function zoomAt(factor, anchorTime) {
        const fraction = (anchorTime - root.viewStart) / root.viewDur
        root.zoomTo(root.viewDur / factor, anchorTime, fraction)
    }

    function fit() {
        root.viewStart = 0
        root.viewDur = Math.max(0.1, root.duration)
        root.clampView()
    }

    function reloadAsset() {
        const index = AssetLibrary.indexOfId(root.lab.currentAssetId)
        root.asset = index >= 0 ? AssetLibrary.assetAt(index) : ({})
    }

    // Keeps the playhead on screen while it plays.
    function follow() {
        const at = EditorState.assetPreview.position
        if (at < root.viewStart || at > root.viewStart + root.viewDur * 0.92)
            root.scrollTo(at - root.viewDur * 0.08)
    }

    Connections {
        target: root.lab
        function onCurrentAssetIdChanged() {
            root.reloadAsset()
            root.fit()
        }
        function onAssetChanged() {
            root.clampView()
        }
    }

    Connections {
        target: AssetLibrary
        function onAssetMetadataChanged(assetId) {
            if (assetId === root.lab.currentAssetId)
                root.reloadAsset()
        }
    }

    Connections {
        target: EditorState.assetPreview
        function onPositionChanged() {
            if (EditorState.assetPreview.playing)
                root.follow()
        }
    }

    onDurationChanged: root.fit()
    Component.onCompleted: {
        root.reloadAsset()
        root.fit()
    }

    AssetsPanelHeader {
        id: header
        width: parent.width
        height: Theme.panelHeaderHeight
        title: qsTr("Source Lab Trim")
    }

    Flow {
        id: toolbar
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingMd
        spacing: Theme.spacingSm
        visible: root.hasAsset

        ThemedButton {
            variant: "secondary"
            glyph: Theme.icons.plus
            text: qsTr("Range from In/Out")
            tooltip: qsTr("Save the trimmed part as a range")
            onClicked: {
                const r = root.lab.recipe
                root.lab.addRange(r.trimIn ?? 0, (r.trimOut ?? -1) < 0 ? root.duration : r.trimOut)
            }
        }
        ThemedButton {
            variant: "ghost"
            glyph: Theme.icons.bookmark
            text: qsTr("Marker")
            tooltip: qsTr("Add a marker at the playhead (M)")
            onClicked: root.lab.addMarker(EditorState.assetPreview.position)
        }
        IconButton {
            glyph: Theme.icons.zoomFit
            tooltip: qsTr("Show the whole source")
            onClicked: root.fit()
        }
        IconButton {
            glyph: Theme.icons.zoomIn
            tooltip: qsTr("Zoom in (Ctrl+scroll)")
            onClicked: root.zoomAt(1.5, root.viewStart + root.viewDur / 2)
        }
        IconButton {
            glyph: Theme.icons.zoomOut
            tooltip: qsTr("Zoom out (Ctrl+scroll)")
            onClicked: root.zoomAt(1 / 1.5, root.viewStart + root.viewDur / 2)
        }
        IconButton {
            glyph: Theme.icons.magnet
            tooltip: qsTr("Snap to the playhead, markers, ranges and trim")
            active: root.snapTargets
            onClicked: root.snapTargets = !root.snapTargets
        }
        ThemedChip {
            visible: root.isAudio
            text: qsTr("Zero-crossing")
            selected: root.zeroCrossSnap
            tooltip: qsTr("Snap cuts to where the waveform crosses zero")
            onClicked: root.zeroCrossSnap = !root.zeroCrossSnap
        }
    }

    SourceLabOverview {
        id: overview
        anchors.top: toolbar.bottom
        anchors.topMargin: Theme.spacingMd
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingMd
        height: 36
        visible: root.hasAsset
        panel: root
    }

    SourceLabStrip {
        anchors.top: overview.bottom
        anchors.topMargin: Theme.spacingSm
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingMd
        visible: root.hasAsset
        panel: root
        asset: root.asset
    }

    EmptyState {
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingLg
        visible: !root.hasAsset
        glyph: Theme.icons.scissors
        title: qsTr("Select a clip in the media bin")
    }
}
