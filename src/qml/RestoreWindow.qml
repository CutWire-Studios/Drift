import QtQuick
import QtQuick.Controls
import QtQuick.Window
import Drift 1.0
import "components"

// Compression removal and upscaling for one clip's used range, or a whole bin video. One frame can
// be previewed first, original and enhanced split by a draggable divider under a shared zoom; the
// full run adds the result to the media bin as a new item and leaves the source alone.
Window {
    id: root

    // The main window, for its addon manager.
    property var host: null
    // A clip id or a bin video's asset id; see AppController::restoreVideo.
    property string targetId: ""
    property bool forAsset: false
    property real rangeSeconds: 0
    property int sourceWidth: 0
    property int sourceHeight: 0
    property real sourceFps: 30

    // Rebuilt rather than bound: restoreModels() is a plain call, so nothing would re-evaluate it
    // when an addon is installed or a model is dropped into the folder while this window is open.
    property var decompressModel: []
    property var upscaleModel: []
    property bool runtimeReady: Addons.runtimeAvailable()

    property int jobRevision: 0
    readonly property var job: { jobRevision; return root.targetId ? EditorState.restoreJob(root.targetId) : ({}) }
    readonly property bool running: job.active === true

    property int previewRevision: 0
    readonly property var preview: { previewRevision; return EditorState.restorePreviewState() }
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
    property var speed: EditorState.restoreSpeed()
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
    color: Theme.appBackground

    function openFor(track, clip) {
        root.forAsset = false
        root.openTarget(EditorState.clipAt(track, clip).id || "")
    }

    function openForAsset(assetId) {
        root.forAsset = true
        root.openTarget(assetId)
    }

    function openTarget(id) {
        const info = EditorState.restoreTargetInfo(id)
        root.targetId = id
        root.rangeSeconds = info.seconds || 0
        root.sourceWidth = info.width || 0
        root.sourceHeight = info.height || 0
        root.sourceFps = info.fps || 30
        root.speed = EditorState.restoreSpeed()
        root.previewedWith = ""
        picker.visible = false
        frameSlider.value = 0
        stage.resetView()
        root.refreshModels()
        root.jobRevision++
        EditorState.setRestorePreviewFrame(id, 0)
        root.show()
        root.raise()
        root.requestActivate()
    }

    function refreshModels() {
        root.runtimeReady = Addons.runtimeAvailable()
        const decompress = [{ label: qsTr("None"), value: "", scale: 1 }]
        const upscale = [{ label: qsTr("None"), name: qsTr("No upscaling"), value: "", scale: 1,
                           summary: qsTr("Keep the original size."), content: [] }]
        const models = root.runtimeReady ? EditorState.restoreModels() : []
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
            EditorState.cancelRestore(root.targetId)
        EditorState.endRestorePreview()
    }

    Connections {
        target: EditorState
        function onRestoreJobChanged(targetId) {
            if (targetId === root.targetId)
                root.jobRevision++
        }
        function onRestorePreviewChanged() {
            root.previewRevision++
            // A finished preview refines the speed estimate.
            root.speed = EditorState.restoreSpeed()
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

        // ----- Before / after ---------------------------------------------------------------
        Column {
            width: parent.width - sidebar.width - Theme.spacingLg
            height: parent.height
            spacing: Theme.spacingMd

            Item {
                id: stage
                width: parent.width
                height: parent.height - scrubRow.height - Theme.spacingMd
                clip: true

                readonly property real frameW: root.preview.width || 16
                readonly property real frameH: root.preview.height || 9
                readonly property real aspect: frameW / frameH
                readonly property real fitW: Math.min(width, height * aspect)
                readonly property real fitH: fitW / aspect

                // Zoom 1 is the letterboxed fit. The cap reaches a few screen pixels per pixel of
                // the enhanced frame, which is where the difference between the two shows.
                property real zoom: 1
                readonly property real maxZoom: Math.max(8, 4 * Math.max(root.preview.enhancedWidth || 0, frameW) / Math.max(1, fitW))
                property real panX: 0
                property real panY: 0
                // Divider position, 0..1 of the stage width.
                property real split: 0.5

                readonly property real viewX: (width - fitW * zoom) / 2 + panX
                readonly property real viewY: (height - fitH * zoom) / 2 + panY
                readonly property real splitX: split * width

                function resetView() {
                    zoom = 1
                    panX = 0
                    panY = 0
                }

                // Zooms about a stage point, keeping the frame pixel under it in place.
                function zoomAt(px, py, factor) {
                    const next = Math.max(1, Math.min(maxZoom, zoom * factor))
                    const fx = (px - viewX) / zoom
                    const fy = (py - viewY) / zoom
                    zoom = next
                    panX = px - fx * next - (width - fitW * next) / 2
                    panY = py - fy * next - (height - fitH * next) / 2
                    if (next === 1) {
                        panX = 0
                        panY = 0
                    }
                }

                Rectangle {
                    anchors.fill: parent
                    color: Theme.panelBackground
                    radius: Theme.radiusMd
                }

                Image {
                    x: stage.viewX
                    y: stage.viewY
                    width: stage.fitW * stage.zoom
                    height: stage.fitH * stage.zoom
                    fillMode: Image.Stretch
                    cache: false
                    smooth: true
                    // The revision defeats QML's URL-keyed image cache.
                    source: root.preview.width > 0
                            ? "image://segment/restore-original?rev=" + root.preview.revision
                            : ""
                }

                // Right of the divider: the enhanced frame laid over the same rectangle, so both
                // halves line up pixel for pixel at any zoom.
                Item {
                    x: stage.splitX
                    width: stage.width - stage.splitX
                    height: stage.height
                    clip: true
                    visible: root.hasEnhanced

                    Image {
                        x: stage.viewX - stage.splitX
                        y: stage.viewY
                        width: stage.fitW * stage.zoom
                        height: stage.fitH * stage.zoom
                        fillMode: Image.Stretch
                        cache: false
                        smooth: true
                        source: root.hasEnhanced
                                ? "image://segment/restore-enhanced?rev=" + root.preview.revision
                                : ""
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    property real lastX: 0
                    property real lastY: 0
                    cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                    onPressed: function (mouse) {
                        lastX = mouse.x
                        lastY = mouse.y
                    }
                    onPositionChanged: function (mouse) {
                        if (stage.zoom <= 1)
                            return
                        stage.panX += mouse.x - lastX
                        stage.panY += mouse.y - lastY
                        lastX = mouse.x
                        lastY = mouse.y
                    }
                    onDoubleClicked: stage.resetView()
                    onWheel: function (wheel) {
                        stage.zoomAt(wheel.x, wheel.y, wheel.angleDelta.y > 0 ? 1.25 : 0.8)
                    }
                }

                // Divider.
                Rectangle {
                    x: stage.splitX - width / 2
                    width: 2
                    height: stage.height
                    color: Theme.primary
                    visible: root.hasEnhanced

                    Rectangle {
                        anchors.centerIn: parent
                        width: 24
                        height: 24
                        radius: 12
                        color: Theme.primary

                        IconGlyph {
                            anchors.centerIn: parent
                            glyph: "move-horizontal"
                            iconSize: 14
                            iconColor: Theme.primaryForeground
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        anchors.leftMargin: -10
                        anchors.rightMargin: -10
                        cursorShape: Qt.SplitHCursor
                        onPositionChanged: function (mouse) {
                            const p = mapToItem(stage, mouse.x, mouse.y)
                            stage.split = Math.max(0, Math.min(1, p.x / stage.width))
                        }
                    }
                }

                ThemedChip {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.margins: Theme.spacingMd
                    text: qsTr("Original")
                    variant: "secondary"
                    visible: root.preview.width > 0
                }

                ThemedChip {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.spacingMd
                    text: root.previewedWith !== root.chosenModels
                          ? qsTr("Enhanced — out of date, preview again")
                          : qsTr("Enhanced %1 × %2").arg(root.preview.enhancedWidth).arg(root.preview.enhancedHeight)
                    visible: root.hasEnhanced
                }

                Rectangle {
                    anchors.centerIn: parent
                    visible: !frameSlider.pressed
                             && (root.preview.loading || (!root.hasEnhanced && root.preview.width > 0) || root.running)
                    width: stageLabel.width + Theme.spacingXl
                    height: stageLabel.height + Theme.spacingLg
                    radius: Theme.radiusMd
                    color: Theme.scrimStrong

                    ThemedLabel {
                        id: stageLabel
                        anchors.centerIn: parent
                        tone: "default"
                        text: root.preview.loading ? qsTr("Loading this frame…")
                              : root.running ? (root.job.status || qsTr("Working…"))
                              : qsTr("Choose models, then Preview to compare this frame")
                    }
                }
            }

            Row {
                id: scrubRow
                width: parent.width
                spacing: Theme.spacingMd

                ThemedLabel {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Frame")
                }

                ThemedSlider {
                    id: frameSlider
                    label: qsTr("Frame")
                    width: parent.width - 300
                    anchors.verticalCenter: parent.verticalCenter
                    from: 0
                    to: Math.max(0.001, root.rangeSeconds)
                    enabled: !root.running
                    onMoved: EditorState.setRestorePreviewFrame(root.targetId, value)
                }

                ThemedLabel {
                    anchors.verticalCenter: parent.verticalCenter
                    text: frameSlider.value.toFixed(2) + qsTr("s")
                }

                ThemedButton {
                    anchors.verticalCenter: parent.verticalCenter
                    variant: "ghost"
                    text: qsTr("Fit")
                    enabled: stage.zoom > 1
                    onClicked: stage.resetView()
                }
            }
        }

        // ----- Controls ---------------------------------------------------------------------
        Item {
            id: sidebar
            width: 300
            height: parent.height

            Column {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: footer.top
                anchors.bottomMargin: Theme.spacingLg
                spacing: Theme.spacingLg

                ThemedLabel {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: root.forAsset
                          ? qsTr("Preview one frame, then enhance the whole video. The result is added to the media bin. Enhancing is slow — minutes per second of video without a GPU.")
                          : qsTr("Preview one frame, then enhance the part of the clip used on the timeline. The result is added to the media bin. Enhancing is slow — minutes per second of video without a GPU.")
                }

                Column {
                    width: parent.width
                    spacing: Theme.spacingSm

                    ThemedLabel { text: qsTr("Remove compression") }

                    ThemedComboBox {
                        id: decompressBox
                        width: parent.width
                        enabled: !root.running
                        textRole: "label"
                        valueRole: "value"
                        model: root.decompressModel
                    }
                }

                Column {
                    width: parent.width
                    spacing: Theme.spacingSm

                    ThemedLabel { text: qsTr("Upscale") }

                    UpscaleModelCard {
                        width: parent.width
                        model: root.upscaleChoice
                        speedLevel: root.speedLevel(root.upscaleChoice.spm || 0)
                        perFrameText: root.upscaleChoice.spm > 0
                                      ? root.formatPerFrame(root.secondsPerFrame(root.upscaleChoice.spm)) : ""
                        selected: picker.visible
                        trailingGlyph: Theme.icons.chevronDown
                        enabled: !root.running
                        onClicked: picker.visible = !picker.visible
                    }
                }

                ThemedLabel {
                    width: parent.width
                    visible: root.sourceWidth > 0
                    tone: "default"
                    wrapMode: Text.WordWrap
                    text: qsTr("Output: %1 × %2").arg(root.outputWidth).arg(root.outputHeight)
                          + (root.outputWidth * root.outputHeight > 3840 * 2160
                             ? qsTr(" — larger than 4K, which is slow to edit and export") : "")
                }

                ThemedLabel {
                    width: parent.width
                    visible: root.maxSeconds > 0
                    wrapMode: Text.WordWrap
                    text: (root.speed.calibrated
                           ? qsTr("Up to about %1 on this computer's CPU.")
                           : qsTr("Up to about %1 on a typical laptop CPU. Preview a frame for an estimate for this computer."))
                          .arg(root.formatDuration(root.maxSeconds))
                }

                ThemedButton {
                    width: parent.width
                    variant: "secondary"
                    text: qsTr("Preview this frame")
                    enabled: !root.running && root.preview.width > 0 && !root.preview.loading
                             && root.hasModelChoice
                    onClicked: {
                        if (EditorState.previewRestore(root.targetId,
                                                       decompressBox.currentValue || "",
                                                       root.upscaleId) !== "")
                            root.previewedWith = root.chosenModels
                    }
                }

                Column {
                    width: parent.width
                    spacing: Theme.spacingSm
                    visible: root.running || !!root.job.error

                    LabelledProgressRing {
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: root.running
                        ringSize: 56
                        value: root.job.progress || 0
                        indeterminate: !(root.job.progress > 0)
                    }

                    ThemedLabel {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: root.running ? (root.job.status || "") : (root.job.error || "")
                    }
                }
            }

            Row {
                id: footer
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                spacing: Theme.spacingMd

                ThemedButton {
                    variant: root.running ? "destructive" : "ghost"
                    text: root.running ? qsTr("Stop") : qsTr("Close")
                    onClicked: {
                        if (root.running)
                            EditorState.cancelRestore(root.targetId)
                        else
                            root.close()
                    }
                }

                ThemedButton {
                    variant: "primary"
                    text: qsTr("Enhance clip")
                    enabled: !root.running && root.hasModelChoice
                    onClicked: EditorState.restoreVideo(root.targetId,
                                                        decompressBox.currentValue || "",
                                                        root.upscaleId)
                }
            }
        }
    }

    // ----- Upscaler picker, laid over the before/after while it is open ---------------------
    Shortcut {
        sequence: "Esc"
        enabled: picker.visible
        onActivated: picker.visible = false
    }

    Rectangle {
        id: picker
        visible: false
        x: Theme.spacingLg
        y: Theme.spacingLg
        width: root.width - sidebar.width - Theme.spacingLg * 3
        height: root.height - Theme.spacingLg * 2
        radius: Theme.radiusMd
        color: Theme.appBackground
        border.width: 1
        border.color: Theme.panelBorder

        property string filter: ""
        // Only the content types something installed is made for.
        readonly property var filters: {
            const order = ["anime", "live", "cg", "general"]
            const seen = {}
            for (const m of root.upscaleModel)
                for (const c of (m.content || []))
                    seen[c] = true
            return order.filter(c => seen[c])
        }
        readonly property var labels: ({
            anime: qsTr("Anime and drawings"),
            live: qsTr("Live action"),
            cg: qsTr("3D animation and games"),
            general: qsTr("General")
        })
        readonly property var shown: root.upscaleModel.filter(
            m => !picker.filter || !m.value || (m.content || []).indexOf(picker.filter) >= 0)

        onVisibleChanged: if (visible) filter = ""

        Column {
            id: pickerHeader
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.spacingLg
            spacing: Theme.spacingMd

            Item {
                width: parent.width
                height: closePicker.height

                ThemedLabel {
                    anchors.verticalCenter: parent.verticalCenter
                    size: "base"
                    tone: "default"
                    text: qsTr("Choose an upscaler")
                }

                IconButton {
                    id: closePicker
                    anchors.right: parent.right
                    glyph: Theme.icons.x
                    tooltip: qsTr("Close")
                    onClicked: picker.visible = false
                }
            }

            Flow {
                width: parent.width
                spacing: Theme.spacingSm
                visible: picker.filters.length > 1

                ThemedChip {
                    text: qsTr("All")
                    selected: picker.filter === ""
                    onClicked: picker.filter = ""
                }

                Repeater {
                    model: picker.filters
                    delegate: ThemedChip {
                        required property string modelData
                        text: picker.labels[modelData]
                        selected: picker.filter === modelData
                        onClicked: picker.filter = modelData
                    }
                }
            }
        }

        GridView {
            id: grid
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: pickerHeader.bottom
            anchors.bottom: pickerFooter.top
            anchors.margins: Theme.spacingMd
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            readonly property int columns: Math.max(2, Math.floor(width / 240))
            cellWidth: Math.floor(width / columns)
            // Thumbnail plus name, content, two lines of summary and the speed meter.
            cellHeight: Math.round((cellWidth - Theme.spacingSm * 4) * 9 / 16) + 128
            model: picker.shown

            delegate: Item {
                required property var modelData
                width: grid.cellWidth
                height: grid.cellHeight

                UpscaleModelCard {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingSm
                    model: parent.modelData
                    selected: parent.modelData.value === root.upscaleId
                    speedLevel: root.speedLevel(parent.modelData.spm || 0)
                    perFrameText: parent.modelData.spm > 0
                                  ? root.formatPerFrame(root.secondsPerFrame(parent.modelData.spm)) : ""
                    onClicked: {
                        root.upscaleId = parent.modelData.value
                        picker.visible = false
                    }
                }
            }

            ScrollBar.vertical: AppScrollBar { }
        }

        Column {
            id: pickerFooter
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: Theme.spacingLg
            spacing: Theme.spacingSm

            ThemedLabel {
                width: parent.width
                visible: root.runtimeReady
                wrapMode: Text.WordWrap
                text: qsTr("Speeds are per frame of this clip. Custom models are experimental and may not work. Drop an ONNX export (fp32 or fp16, RGB, 1x/2x/4x) into the folder; put the scale in the file name, e.g. \"2x_Name.onnx\".")
            }

            Flow {
                width: parent.width
                spacing: Theme.spacingSm

                ThemedButton {
                    variant: "ghost"
                    text: root.runtimeReady ? qsTr("Get models (openmodeldb.info)") : qsTr("Install AI engine first")
                    onClicked: {
                        if (root.runtimeReady) {
                            Qt.openUrlExternally("https://openmodeldb.info")
                        } else {
                            // The manager opens in the main window, which this one would cover.
                            root.host.openAddonManager("onnxruntime")
                            root.close()
                        }
                    }
                }

                ThemedButton {
                    variant: "ghost"
                    text: qsTr("Open custom models folder")
                    visible: root.runtimeReady
                    onClicked: Qt.openUrlExternally("file://" + EditorState.restoreModelsFolder())
                }

                ThemedButton {
                    variant: "ghost"
                    text: qsTr("Refresh model list")
                    visible: root.runtimeReady
                    onClicked: root.refreshModels()
                }
            }
        }
    }
}
