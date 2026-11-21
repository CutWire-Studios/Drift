import QtQuick
import QtQuick.Window
import Drift

// Compression removal and upscaling for one clip's used range, or a whole bin video. One frame can
// be previewed first, original and enhanced split by a draggable divider under a shared zoom; the
// full run adds the result to the media bin as a new item and leaves the source alone.
ToolWindow {
    id: root
    property alias frameSlider: compareView.frameSlider
    property alias stage: compareView.stage
    property alias decompressBox: sidebar.decompressBox

    // The main window, for its addon manager.
    property var host: null
    // A clip id or a bin video's asset id; see RestoreController::run.
    property string targetId: ""
    property bool forAsset: false
    property real rangeSeconds: 0
    property int sourceWidth: 0
    property int sourceHeight: 0
    property real sourceFps: 30
    // The bin asset behind the target, for the Source Lab link.
    property string labAssetId: ""

    // Rebuilt rather than bound: models() is a plain call, so nothing would re-evaluate it
    // when an addon is installed or a model is dropped into the folder while this window is open.
    property var decompressModel: []
    property var upscaleModel: []
    property bool runtimeReady: Addons.runtimeAvailable()

    property int jobRevision: 0
    readonly property var job: { jobRevision; return root.targetId ? EditorState.restore.job(root.targetId) : ({}) }
    readonly property bool running: job.active === true

    property int previewRevision: 0
    readonly property var preview: { previewRevision; return EditorState.restore.previewState() }
    readonly property bool hasEnhanced: preview.enhancedWidth > 0
    // Which models made the enhanced half, so a later change of model can say it is out of date.
    property string previewedWith: ""
    readonly property string chosenModels: (decompressBox.currentValue || "") + "|" + root.upscaleId

    // The picked upscaler: "" for none. The picker writes it; refreshModels() drops it if the model
    // has gone.
    property string upscaleId: ""
    readonly property var upscaleChoice: {
        for (let i = 0; i < upscaleModel.length; ++i) {
            if (upscaleModel[i].value === root.upscaleId)
                return upscaleModel[i]
        }
        return upscaleModel.length > 0 ? upscaleModel[0] : ({ value: "", scale: 1 })
    }
    readonly property bool hasModelChoice: decompressBox.currentIndex > 0 || root.upscaleId !== ""
    readonly property int chosenScale: upscaleChoice.scale || 1

    // Time estimates. Each model's secondsPerMegapixel was measured on one reference CPU; the factor
    // scales that to this machine once a preview has been timed.
    property var speed: EditorState.restore.speed()
    readonly property real sourceMegapixels: Math.max(1, (preview.width || sourceWidth) * (preview.height || sourceHeight)) / 1e6
    // Fastest and slowest upscaler, for the picker's relative speed meter.
    readonly property var speedRange: {
        let lo = 0, hi = 0
        for (let i = 0; i < upscaleModel.length; ++i) {
            const spm = upscaleModel[i].spm || 0
            if (spm <= 0)
                continue
            lo = lo === 0 ? spm : Math.min(lo, spm)
            hi = Math.max(hi, spm)
        }
        return { lo: lo, hi: hi }
    }

    function secondsPerFrame(spm) {
        return spm > 0 ? spm * root.sourceMegapixels * (root.speed.factor || 1) : 0
    }

    // 1 (slowest) to 5 (fastest) on a log scale between the slowest and fastest installed upscaler.
    function speedLevel(spm) {
        if (spm <= 0 || root.speedRange.hi <= root.speedRange.lo)
            return spm > 0 ? 5 : 0
        const t = (Math.log(spm) - Math.log(root.speedRange.lo))
                  / (Math.log(root.speedRange.hi) - Math.log(root.speedRange.lo))
        return Math.round(5 - 4 * t)
    }

    function formatDuration(seconds) {
        if (seconds < 1)
            return qsTr("under a second")
        if (seconds < 90)
            return qsTr("%1 s").arg(Math.round(seconds))
        const minutes = Math.round(seconds / 60)
        if (minutes < 90)
            return qsTr("%1 min").arg(minutes)
        return qsTr("%1 h %2 min").arg(Math.floor(minutes / 60)).arg(minutes % 60)
    }

    function formatPerFrame(seconds) {
        return seconds < 10 ? qsTr("%1 s per frame").arg(seconds.toFixed(1))
                            : qsTr("%1 s per frame").arg(Math.round(seconds))
    }

    // Upper bound for the whole run: every frame at the estimated rate plus a margin, since a busy
    // machine or a dark, detailed clip runs slower than the reference frame did. 0 when unknown.
    readonly property real maxSeconds: {
        let perFrame = 0
        const decompress = root.decompressModel[decompressBox.currentIndex] || {}
        for (const m of [decompress, root.upscaleChoice]) {
            if (!m.value)
                continue
            if (!(m.spm > 0))
                return 0
            perFrame += secondsPerFrame(m.spm)
        }
        return perFrame * Math.ceil(root.rangeSeconds * root.sourceFps) * 1.25
    }
    // The decoded frame is upright, which the stored source size is not for a rotated video.
    readonly property int outputWidth: (preview.width || sourceWidth) * chosenScale
    readonly property int outputHeight: (preview.height || sourceHeight) * chosenScale

    width: 1100
    height: 720
    minimumWidth: 780
    minimumHeight: 520
    title: qsTr("Enhance video")
    color: Theme.c.restoreWindow.background

    function openFor(track, clip) {
        root.forAsset = false
        root.labAssetId = EditorState.clipAt(track, clip).assetId || ""
        root.openTarget(EditorState.clipAt(track, clip).id || "")
    }

    function openForAsset(assetId) {
        root.forAsset = true
        root.labAssetId = assetId
        root.openTarget(assetId)
    }

    function openTarget(id) {
        const info = EditorState.restore.targetInfo(id)
        root.targetId = id
        root.rangeSeconds = info.seconds || 0
        root.sourceWidth = info.width || 0
        root.sourceHeight = info.height || 0
        root.sourceFps = info.fps || 30
        root.speed = EditorState.restore.speed()
        root.previewedWith = ""
        picker.visible = false
        frameSlider.value = 0
        stage.resetView()
        root.refreshModels()
        root.jobRevision++
        EditorState.restore.setPreviewFrame(id, 0)
        root.show()
        root.raise()
        root.requestActivate()
    }

    function refreshModels() {
        root.runtimeReady = Addons.runtimeAvailable()
        const decompress = [{ label: qsTr("None"), value: "", scale: 1 }]
        const upscale = [{ label: qsTr("None"), name: qsTr("No upscaling"), value: "", scale: 1,
                           summary: qsTr("Keep the original size."), content: [] }]
        const models = root.runtimeReady ? EditorState.restore.models() : []
        for (let i = 0; i < models.length; ++i) {
            const m = models[i]
            const label = m.custom ? qsTr("%1 (custom, experimental)").arg(m.name) : m.name
            const entry = { label: label, name: m.name, value: m.id, scale: m.scale, custom: m.custom,
                            summary: m.summary, thumbnail: m.thumbnail, content: m.content,
                            spm: m.secondsPerMegapixel }
            if (m.task === "decompress")
                decompress.push(entry)
            else
                upscale.push(entry)
        }
        root.decompressModel = decompress
        root.upscaleModel = upscale
        if (!upscale.some(m => m.value === root.upscaleId))
            root.upscaleId = ""
    }

    onClosing: {
        // A full run keeps going and lands in the bin; a preview nobody can see does not.
        if (root.running && root.job.kind === "restore-preview")
            EditorState.restore.cancel(root.targetId)
        EditorState.restore.endPreview()
    }

    Connections {
        target: EditorState.restore
        function onJobChanged(targetId) {
            if (targetId === root.targetId)
                root.jobRevision++
        }
        function onPreviewChanged() {
            root.previewRevision++
            // A finished preview refines the speed estimate.
            root.speed = EditorState.restore.speed()
        }
    }

    Connections {
        target: Addons
        function onKindChanged(kind) {
            if (kind === "restore-model" || kind === "onnxruntime")
                root.refreshModels()
        }
    }

    Row {
        anchors.fill: parent
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingLg

        RestoreCompareView {
            id: compareView
            tool: root
            sidebarWidth: sidebar.width
        }

        // ----- Controls ---------------------------------------------------------------------
        RestoreControlColumn {
            id: sidebar
            tool: root
            pickerPanel: picker
        }
    }

    // ----- Upscaler picker, laid over the before/after while it is open ---------------------
    Shortcut {
        sequence: "Esc"
        enabled: picker.visible
        onActivated: picker.visible = false
    }

    RestoreUpscalerPicker {
        id: picker
        tool: root
        sidebarWidth: sidebar.width
    }
}
