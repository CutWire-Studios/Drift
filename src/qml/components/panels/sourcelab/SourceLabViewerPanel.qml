import QtQuick
import Drift

PanelFrame {
    id: root
    SearchAnchor {
        searchId: "sourcelab-viewer"
        title: qsTr("Source Lab")
        keywords: "source lab workspace cut trim crop reframe ranges subclip favorite reject marker speed denoise stabilize upscale enhance export clip normalize loudness"
    }

    // Set by PanelPool. The panel owns EditorState.assetPreview only while it is on screen.
    property bool shown: false
    // The bin asset the preview session was begun for.
    property string sessionId: ""
    property string compareMode: "off"
    property bool showB: false
    property string segmentPath: ""
    property real segmentStart: 0
    property string segmentKey: ""
    property string pendingSegmentKey: ""

    readonly property var lab: EditorState.sourceLab
    readonly property var preview: EditorState.assetPreview
    readonly property var recipe: root.lab.recipe
    readonly property string assetId: root.lab.currentAssetId
    readonly property bool hasAsset: root.assetId.length > 0
    readonly property bool isAudio: root.lab.kind === "audio"
    readonly property bool isImage: root.lab.kind === "image"
    readonly property bool keysActive: root.shown && root.hasAsset && !root.isImage

    readonly property string tool: {
        const r = root.recipe
        if ((r.upscaleModel ?? "") !== "" || (r.decompressModel ?? "") !== "")
            return "enhance"
        if ((r.speed ?? 1) !== 1 || r.denoise || r.stabilize || (r.gainDb ?? 0) !== 0
                || !isNaN(r.normalizeLufs ?? NaN))
            return "segment"
        return "crop"
    }
    // What the rendered segment depends on; moving In/Out does not invalidate it.
    readonly property string segmentRecipeKey: {
        const r = root.recipe
        return JSON.stringify([r.speed, r.keepPitch, r.gainDb, r.normalizeLufs, r.denoise,
                               r.stabilize, r.crop?.x, r.crop?.y, r.crop?.width, r.crop?.height])
    }

    function applyRecipe() {
        if (root.sessionId.length === 0)
            return
        root.preview.setPreviewRate(root.recipe.speed ?? 1)
        root.preview.setPreviewGain(root.recipe.gainDb ?? 0)
    }

    function endSession() {
        if (root.sessionId.length === 0)
            return
        root.preview.pause()
        root.preview.end()
        root.sessionId = ""
    }

    function syncSession() {
        const wanted = root.shown && root.hasAsset && !root.isImage ? root.assetId : ""
        if (wanted === root.sessionId)
            return
        root.endSession()
        root.segmentPath = ""
        if (wanted.length === 0)
            return
        const index = AssetLibrary.indexOfId(wanted)
        if (index < 0)
            return
        root.preview.pause()
        root.preview.begin(index)
        root.sessionId = wanted
        root.applyRecipe()
        root.preview.seek(root.recipe.trimIn ?? 0)
    }

    function togglePlay() {
        if (root.preview.playing || root.preview.shuttleRate !== 0)
            root.preview.pause()
        else
            root.preview.play()
    }

    function toggleCompareKey() {
        if (root.compareMode !== "toggle") {
            root.compareMode = "toggle"
            root.showB = true
        } else {
            root.showB = !root.showB
        }
    }

    function setIn() {
        const at = root.preview.snapToFrame(root.preview.position)
        const out = root.recipe.trimOut ?? -1
        if (out >= 0 && at >= out)
            return
        root.lab.setRecipeValue("trimIn", at)
    }

    function setOut() {
        const at = root.preview.snapToFrame(root.preview.position)
        if (at <= (root.recipe.trimIn ?? 0))
            return
        root.lab.setRecipeValue("trimOut", at >= root.lab.durationSeconds - 0.0005 ? -1 : at)
    }

    function goToOut() {
        const out = root.recipe.trimOut ?? -1
        root.preview.seek(root.preview.snapToFrame(out < 0 ? root.lab.durationSeconds : out))
    }

    // The editor's global shortcuts land here while Source Lab is on screen, so the keys keep
    // their usual bindings but act on the source instead of the timeline.
    function handleAction(id) {
        if (!root.keysActive || root.sessionId.length === 0)
            return false
        switch (id) {
        case "playPause": root.togglePlay(); return true
        case "stepBack": root.preview.stepFrames(-1); return true
        case "stepForward": root.preview.stepFrames(1); return true
        case "jumpBack": root.preview.stepFrames(-10); return true
        case "jumpForward": root.preview.stepFrames(10); return true
        case "speedDown": root.preview.shuttleReverse(); return true
        case "speedUp": root.preview.shuttleForward(); return true
        case "markIn": root.setIn(); return true
        case "markOut": root.setOut(); return true
        case "goToIn": root.preview.seek(root.preview.snapToFrame(root.recipe.trimIn ?? 0)); return true
        case "goToOut": root.goToOut(); return true
        case "toggleBookmark": root.lab.addMarker(root.preview.snapToFrame(root.preview.position)); return true
        }
        return false
    }

    onShownChanged: {
        root.syncSession()
    }
    onAssetIdChanged: root.syncSession()
    onRecipeChanged: root.applyRecipe()
    onKeysActiveChanged: {
        if (root.keysActive)
            ShellBridge.sourceLabActionHandler = root
        else if (ShellBridge.sourceLabActionHandler === root)
            ShellBridge.sourceLabActionHandler = null
    }
    Component.onDestruction: {
        root.endSession()
        if (ShellBridge.sourceLabActionHandler === root)
            ShellBridge.sourceLabActionHandler = null
    }

    Connections {
        target: EditorState
        function onPlayingChanged() {
            if (EditorState.playing && root.preview.playing)
                root.preview.pause()
        }
    }

    Connections {
        target: root.lab
        function onPreviewSegmentReady(path, startSeconds) {
            root.segmentPath = path
            root.segmentStart = startSeconds
            root.segmentKey = root.pendingSegmentKey
        }
    }

    Shortcut {
        sequence: "K"
        enabled: root.keysActive
        onActivated: root.preview.shuttleStop()
    }
    Shortcut {
        sequence: "\\"
        enabled: root.keysActive && !root.isAudio
        onActivated: root.toggleCompareKey()
    }

    AssetsPanelHeader {
        id: header
        width: parent.width
        height: Theme.panelHeaderHeight
        title: root.hasAsset ? root.lab.name : qsTr("Source Lab Viewer")

        Row {
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingMd
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingSm
            visible: root.hasAsset

            ThemedLabel {
                anchors.verticalCenter: parent.verticalCenter
                visible: !root.isAudio && root.lab.width > 0
                font.family: Theme.monoFontFamily
                text: qsTr("%1 x %2  %3 fps").arg(root.lab.width).arg(root.lab.height)
                      .arg(Math.round(root.lab.fps * 100) / 100)
            }
            IconButton {
                glyph: Theme.icons.star
                tooltip: qsTr("Favorite")
                active: root.lab.rating === 1
                onClicked: root.lab.setAssetRating(root.lab.rating === 1 ? 0 : 1)
            }
            IconButton {
                glyph: Theme.icons.x
                tooltip: qsTr("Reject")
                active: root.lab.rating === -1
                onClicked: root.lab.setAssetRating(root.lab.rating === -1 ? 0 : -1)
            }
        }
    }

    Row {
        id: compareBar
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingMd
        spacing: Theme.spacingSm
        visible: root.hasAsset && !root.isAudio && !root.isImage

        ThemedLabel {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Compare")
        }
        Repeater {
            model: [
                { id: "off", label: qsTr("Off") },
                { id: "split", label: qsTr("Split") },
                { id: "side", label: qsTr("Side by side") },
                { id: "toggle", label: qsTr("Toggle") }
            ]
            ThemedChip {
                required property var modelData
                anchors.verticalCenter: parent.verticalCenter
                text: modelData.label
                selected: root.compareMode === modelData.id
                tooltip: modelData.id === "toggle" ? qsTr("Switch between A and B with \\") : ""
                onClicked: {
                    root.compareMode = modelData.id
                    if (modelData.id === "toggle")
                        root.showB = true
                }
            }
        }
        ThemedButton {
            anchors.verticalCenter: parent.verticalCenter
            visible: root.compareMode !== "off" && root.tool === "segment"
            variant: "secondary"
            text: root.lab.previewBusy ? qsTr("Rendering...") : qsTr("Render preview")
            tooltip: qsTr("Render a few seconds around the playhead with these settings")
            enabled: !root.lab.previewBusy && !root.lab.busy
            onClicked: {
                root.pendingSegmentKey = root.segmentRecipeKey
                root.lab.renderPreviewSegment(root.preview.position, 5)
            }
        }
        ThemedButton {
            anchors.verticalCenter: parent.verticalCenter
            visible: root.compareMode !== "off" && root.tool === "enhance"
            variant: "secondary"
            text: compare.enhanceBusy ? qsTr("Enhancing...") : qsTr("Preview frame")
            tooltip: qsTr("Run the enhance models on the frame under the playhead")
            enabled: !compare.enhanceBusy && !root.lab.busy
            onClicked: compare.previewEnhance()
        }
    }

    SourceLabCompareView {
        id: compare
        anchors.top: compareBar.visible ? compareBar.bottom : header.bottom
        anchors.topMargin: Theme.spacingMd
        anchors.bottom: transport.top
        anchors.bottomMargin: Theme.spacingMd
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.spacingMd
        anchors.rightMargin: Theme.spacingMd
        visible: root.hasAsset
        mode: root.compareMode
        showB: root.showB
        tool: root.tool
        isImage: root.isImage
        isAudio: root.isAudio
        assetId: root.assetId
        imagePath: root.isImage ? (AssetLibrary.assetAt(AssetLibrary.indexOfId(root.assetId)).path ?? "") : ""
        segmentPath: root.segmentPath
        segmentStart: root.segmentStart
        segmentStale: root.segmentPath.length > 0 && root.segmentKey !== root.segmentRecipeKey
    }

    SourceLabTransport {
        id: transport
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spacingMd
        visible: root.hasAsset && !root.isImage
        onTogglePlayRequested: root.togglePlay()
    }

    EmptyState {
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingLg
        visible: !root.hasAsset
        glyph: Theme.icons.film
        title: qsTr("Select a clip in the media bin")
    }
}
