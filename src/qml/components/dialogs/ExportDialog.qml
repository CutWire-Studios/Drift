import QtQuick
import QtQuick.Controls.Basic
import Drift

ThemedDialog {
    id: root

    // Combo and tag fields live in the section components. These aliases keep
    // syncComboIndices() and buildSettings() addressing them by the same names.
    property alias frameRateCombo: videoSection.frameRateCombo
    property alias gifFrameRateCombo: gifSection.gifFrameRateCombo
    property alias videoCodecCombo: videoSection.videoCodecCombo
    property alias audioCodecCombo: videoSection.audioCodecCombo
    property alias audioBitrateCombo: videoSection.audioBitrateCombo
    property alias presetCombo: videoSection.presetCombo
    property alias audioOnlyCodecCombo: audioSection.audioOnlyCodecCombo
    property alias audioOnlyBitrateCombo: audioSection.audioOnlyBitrateCombo
    property alias tagTitleField: audioSection.tagTitleField
    property alias tagArtistField: audioSection.tagArtistField
    property alias tagAlbumField: audioSection.tagAlbumField
    property alias tagCommentField: audioSection.tagCommentField

    title: qsTr("Export")
    acceptText: qsTr("Export")
    preferredWidth: Theme.dialogWidthLg

    property string exportMode: "video" // "video" | "audio" | "gif"
    property string scaleId: "source"
    property int targetHeight: 0
    property int videoBitrateKbps: 12000
    property string videoCodecId: "h264"
    property string rateControl: "crf"
    property int crf: 18
    property string videoPreset: "medium"
    property string audioCodecId: "aac"
    property int audioBitrateKbps: 192
    property bool advancedOpen: false
    property bool exportWorkAreaOnly: false

    // Export frame rate. fpsNum 0 means "follow the project fps". Rational so the
    // NTSC rates (30000/1001 and friends) survive the round-trip.
    property string frameRateId: "project"
    property int fpsNum: 0
    property int fpsDen: 1
    property var frameRateOptions: []
    readonly property bool frameRateCustom: frameRateId === "custom"

    // Bitrate combo: preset kbps values, or "custom"
    property string audioBitrateChoice: "192"
    readonly property var audioBitratePresets: [96, 128, 160, 192, 256, 320]
    readonly property var audioBitrateModel: [
        { label: qsTr("96 kbps"), value: "96" },
        { label: qsTr("128 kbps"), value: "128" },
        { label: qsTr("160 kbps"), value: "160" },
        { label: qsTr("192 kbps"), value: "192" },
        { label: qsTr("256 kbps"), value: "256" },
        { label: qsTr("320 kbps"), value: "320" },
        { label: qsTr("Custom"), value: "custom" }
    ]

    property var videoCodecs: []
    property var audioCodecs: []
    property var scaleOptions: []
    property var currentVideoCodec: ({})
    property var currentAudioCodec: ({})

    readonly property bool videoLossless: !!(currentVideoCodec && currentVideoCodec.lossless)
    readonly property bool videoHasAlpha: !!(currentVideoCodec && currentVideoCodec.hasAlpha)
    readonly property bool videoSupportsCrf: !!(currentVideoCodec && currentVideoCodec.supportsCrf)
    readonly property bool videoSupportsBitrate: !!(currentVideoCodec && currentVideoCodec.supportsBitrate)
    readonly property bool videoSupportsPreset: !!(currentVideoCodec && currentVideoCodec.supportsPreset)
    readonly property var videoPresetList: (currentVideoCodec && currentVideoCodec.presets) ? currentVideoCodec.presets : []
    readonly property bool audioLossless: !!(currentAudioCodec && currentAudioCodec.lossless)
    readonly property bool isAudioOnly: exportMode === "audio"
    readonly property bool isGifExport: exportMode === "gif"

    function codecById(list, id) {
        for (var i = 0; i < list.length; ++i) {
            if (list[i].id === id)
                return list[i]
        }
        return null
    }

    function firstAvailableId(list, preferred) {
        var pref = codecById(list, preferred)
        if (pref && pref.available)
            return preferred
        for (var i = 0; i < list.length; ++i) {
            if (list[i].available)
                return list[i].id
        }
        return preferred
    }

    // Catalog from the engine plus a Custom… escape hatch for rates not listed.
    readonly property var frameRateModel: {
        var list = []
        for (var i = 0; i < frameRateOptions.length; ++i)
            list.push(frameRateOptions[i])
        list.push({ id: "custom", label: qsTr("Custom…"), fpsNum: 0, fpsDen: 1 })
        return list
    }

    // Picks the catalog entry matching num/den, else "custom" for a hand-typed rate.
    function frameRateIdFor(num, den) {
        if (!(num > 0) || !(den > 0))
            return "project"
        for (var i = 0; i < frameRateOptions.length; ++i) {
            var o = frameRateOptions[i]
            if (o.fpsNum === num && o.fpsDen === den)
                return o.id
        }
        return "custom"
    }

    function applyFrameRateChoice(entry) {
        if (!entry)
            return
        frameRateId = entry.id
        if (entry.id === "custom") {
            // Keep whatever rate was showing; only fall back when following the project.
            if (!(fpsNum > 0)) {
                fpsNum = EditorState.projectFile.projectFps()
                fpsDen = 1
            }
        } else {
            fpsNum = entry.fpsNum
            fpsDen = entry.fpsDen
        }
    }

    function syncAudioBitrateChoice() {
        var s = String(audioBitrateKbps)
        for (var i = 0; i < audioBitratePresets.length; ++i) {
            if (audioBitratePresets[i] === audioBitrateKbps) {
                audioBitrateChoice = s
                return
            }
        }
        audioBitrateChoice = "custom"
    }

    function applyAudioBitrateChoice(choice) {
        audioBitrateChoice = choice
        if (choice !== "custom") {
            var n = parseInt(choice, 10)
            if (!isNaN(n))
                audioBitrateKbps = n
        }
    }

    function refreshCodecMeta() {
        currentVideoCodec = codecById(videoCodecs, videoCodecId) || {}
        currentAudioCodec = codecById(audioCodecs, audioCodecId) || {}
        if (videoLossless)
            rateControl = "crf"
        else if (!videoSupportsCrf && videoSupportsBitrate)
            rateControl = "bitrate"
        else if (videoSupportsCrf && rateControl !== "bitrate")
            rateControl = "crf"
    }

    function applyVideoCodecDefaults(codec) {
        if (!codec)
            return
        if (codec.defaultCrf !== undefined)
            crf = codec.defaultCrf
        if (codec.defaultPreset)
            videoPreset = codec.defaultPreset
        if (codec.lossless)
            rateControl = "crf"
        else if (!codec.supportsCrf && codec.supportsBitrate)
            rateControl = "bitrate"
        else if (codec.supportsCrf)
            rateControl = "crf"
    }

    function formatWorkAreaTime(seconds) {
        const clamped = Math.max(0, seconds)
        const m = Math.floor(clamped / 60)
        const s = clamped - m * 60
        const whole = Math.floor(s)
        const frac = Math.floor((s - whole) * 100)
        return String(m).padStart(2, "0") + ":" + String(whole).padStart(2, "0") + "."
               + String(frac).padStart(2, "0")
    }

    function applyGifDefaults() {
        if (!(fpsNum > 0) || frameRateId === "project") {
            fpsNum = 15
            fpsDen = 1
            frameRateId = frameRateIdFor(15, 1)
        }
        if (EditorState.workAreaActive)
            exportWorkAreaOnly = true
    }

    function openDialog() {
        videoCodecs = EditorState.exportVideoCodecs()
        audioCodecs = EditorState.exportAudioCodecs()
        scaleOptions = EditorState.exportScaleOptions()
        frameRateOptions = EditorState.exportFrameRateOptions()

        var defaults = EditorState.exportDefaultSettings()
        var remembered = EditorState.lastExportSettings()
        var hasRemembered = !!(remembered
                               && (remembered.scaleId || remembered.videoCodecId
                                   || remembered.audioCodecId || remembered.gifExport))
        var src = hasRemembered ? remembered : defaults

        // Always open on Video unless the last export was audio-only or GIF.
        exportMode = hasRemembered && src.gifExport ? "gif"
                : hasRemembered && src.audioOnly ? "audio" : "video"
        videoCodecId = firstAvailableId(videoCodecs, src.videoCodecId || defaults.videoCodecId || "h264")
        audioCodecId = firstAvailableId(audioCodecs, src.audioCodecId || defaults.audioCodecId || "aac")
        rateControl = src.rateControl || defaults.rateControl || "crf"
        crf = (src.crf !== undefined && src.crf !== null) ? src.crf : (defaults.crf || 18)
        if (videoCodecId === "h264")
            crf = Math.max(1, crf)
        videoBitrateKbps = src.videoBitrateKbps || defaults.videoBitrateKbps || 12000
        videoPreset = src.videoPreset || defaults.videoPreset || "medium"
        audioBitrateKbps = src.audioBitrateKbps || defaults.audioBitrateKbps || 192

        // Older builds saved x264 CRF 0 with a prohibitively slow preset.
        // Some builds already clamped that value to 1, so migrate both legacy
        // combinations to the fast, usable default encoder and quality.
        if (hasRemembered && src.videoCodecId === "h264"
                && src.rateControl === "crf" && Number(src.crf) <= 1
                && (src.videoPreset === "veryslow" || src.videoPreset === "placebo")) {
            videoCodecId = firstAvailableId(videoCodecs, defaults.videoCodecId)
            applyVideoCodecDefaults(codecById(videoCodecs, videoCodecId))
        }
        syncAudioBitrateChoice()

        var restoredNum = (src.fpsNum !== undefined && src.fpsNum !== null) ? src.fpsNum : 0
        var restoredDen = (src.fpsDen !== undefined && src.fpsDen !== null) ? src.fpsDen : 1
        if (!(restoredNum > 0) || !(restoredDen > 0)) {
            restoredNum = 0
            restoredDen = 1
        }
        fpsNum = restoredNum
        fpsDen = restoredDen
        frameRateId = frameRateIdFor(restoredNum, restoredDen)
        if (exportMode === "gif")
            applyGifDefaults()

        advancedOpen = false
        exportWorkAreaOnly = EditorState.workAreaActive
                && (remembered.exportWorkAreaOnly === true)

        var meta = EditorState.projectFile.projectMetadata || {}
        tagTitleField.text = meta.title || EditorState.projectFile.projectName || ""
        tagArtistField.text = meta.author || ""
        tagAlbumField.text = ""
        tagCommentField.text = meta.description || ""

        var restoredScale = false
        if (hasRemembered && src.scaleId && scaleOptions.length > 0) {
            for (var i = 0; i < scaleOptions.length; ++i) {
                if (scaleOptions[i].id === src.scaleId) {
                    scaleId = scaleOptions[i].id
                    targetHeight = scaleOptions[i].targetHeight
                    // Keep the remembered bitrate when Advanced was customized; otherwise use
                    // the chip's suggested bitrate for that scale.
                    if (src.videoBitrateKbps === undefined || src.videoBitrateKbps === null)
                        videoBitrateKbps = scaleOptions[i].videoBitrateKbps || videoBitrateKbps
                    restoredScale = true
                    break
                }
            }
        }
        if (!restoredScale) {
            if (scaleOptions.length > 0) {
                scaleId = scaleOptions[0].id
                targetHeight = scaleOptions[0].targetHeight
                if (!hasRemembered && scaleOptions[0].videoBitrateKbps)
                    videoBitrateKbps = scaleOptions[0].videoBitrateKbps
            } else {
                scaleId = "source"
                targetHeight = 0
            }
        }

        // Codec catalog defaults only on a fresh dialog — otherwise they wipe the remembered CRF.
        if (!hasRemembered)
            applyVideoCodecDefaults(codecById(videoCodecs, videoCodecId))
        refreshCodecMeta()
        syncComboIndices()
        open()
    }

    function openAudioOnly() {
        openDialog()
        exportMode = "audio"
        syncComboIndices()
    }

    function syncComboIndices() {
        if (frameRateCombo)
            frameRateCombo.currentIndex = Math.max(0, frameRateCombo.indexOfValue(frameRateId))
        if (gifFrameRateCombo)
            gifFrameRateCombo.currentIndex = Math.max(0, gifFrameRateCombo.indexOfValue(frameRateId))
        if (videoCodecCombo)
            videoCodecCombo.currentIndex = Math.max(0, videoCodecCombo.indexOfValue(videoCodecId))
        if (audioCodecCombo)
            audioCodecCombo.currentIndex = Math.max(0, audioCodecCombo.indexOfValue(audioCodecId))
        if (audioOnlyCodecCombo)
            audioOnlyCodecCombo.currentIndex = Math.max(0, audioOnlyCodecCombo.indexOfValue(audioCodecId))
        if (audioBitrateCombo)
            audioBitrateCombo.currentIndex = Math.max(0, audioBitrateCombo.indexOfValue(audioBitrateChoice))
        if (audioOnlyBitrateCombo)
            audioOnlyBitrateCombo.currentIndex = Math.max(0, audioOnlyBitrateCombo.indexOfValue(audioBitrateChoice))
        if (videoSupportsPreset && videoPresetList.length > 0 && presetCombo) {
            var pi = videoPresetList.indexOf(videoPreset)
            presetCombo.currentIndex = pi >= 0 ? pi : Math.max(0, videoPresetList.indexOf(
                                                                   currentVideoCodec.defaultPreset || ""))
            if (presetCombo.currentIndex >= 0 && presetCombo.currentIndex < videoPresetList.length)
                videoPreset = videoPresetList[presetCombo.currentIndex]
        }
    }

    function buildSettings() {
        var s = {
            "scaleId": scaleId,
            "targetHeight": targetHeight,
            "fpsNum": isAudioOnly ? 0 : fpsNum,
            "fpsDen": isAudioOnly ? 1 : fpsDen,
            "videoCodecId": videoCodecId,
            "rateControl": videoLossless ? "crf" : rateControl,
            "crf": crf,
            "videoBitrateKbps": videoBitrateKbps,
            "videoPreset": videoPreset,
            "audioCodecId": audioCodecId,
            "audioBitrateKbps": audioBitrateKbps,
            "audioOnly": isAudioOnly,
            "gifExport": isGifExport,
            "exportWorkAreaOnly": exportWorkAreaOnly && EditorState.workAreaActive
        }
        if (s.exportWorkAreaOnly) {
            s.startUs = Math.round(EditorState.workAreaInSeconds * 1000000)
            s.endUs = Math.round(EditorState.workAreaOutSeconds * 1000000)
        }
        if (isAudioOnly) {
            if (tagTitleField.text.length)
                s.metadataTitle = tagTitleField.text
            if (tagArtistField.text.length)
                s.metadataArtist = tagArtistField.text
            if (tagAlbumField.text.length)
                s.metadataAlbum = tagAlbumField.text
            if (tagCommentField.text.length)
                s.metadataComment = tagCommentField.text
        }
        return s
    }

    onAccepted: {
        var container = isGifExport ? "gif"
                : isAudioOnly
                  ? EditorState.exportPreferredAudioOnlyContainer(audioCodecId)
                  : EditorState.exportPreferredContainer(videoCodecId, audioCodecId)
        var filters = EditorState.exportSaveFilters(container, isAudioOnly)
        var suffix = EditorState.exportDefaultSuffix(container, isAudioOnly)
        var dialogTitle = isGifExport ? qsTr("Export GIF")
                : isAudioOnly ? qsTr("Export Audio") : qsTr("Export Video")
        // SAF offers no overwrite, so a repeated default name would only ever pick up " (1)".
        var suggestedName = Qt.platform.os === "android"
                ? EditorState.projectFile.projectName + " " + Qt.formatDateTime(new Date(), "yyyyMMdd_HHmmss")
                : EditorState.projectFile.projectName
        var url = FileDialogs.saveFile(dialogTitle, filters,
                                       suggestedName, suffix,
                                       EditorState.lastExportFolder())
        if (url != "") {
            EditorState.exportWithSettings(url, buildSettings())
            Toasts.info(qsTr("Export started…"))
        } else {
            Toasts.info(qsTr("Export cancelled."))
        }
    }

    // With Advanced open this content is ~800px tall against a 560px minimum
    // window height, so it has to scroll: unscrolled, the dialog overflowed the
    // window and took the Export/Cancel buttons off-screen with it.
    contentItem: Flickable {
        id: contentFlick
        width: parent ? parent.width : Theme.dialogWidthLg
        implicitHeight: Math.min(exportColumn.height, root.availableContentHeight)
        contentWidth: width
        contentHeight: exportColumn.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        // Bumped by ThemedSlider while a handle is dragged, so the dialog
        // doesn't steal the drag.
        property int dragLocks: 0
        interactive: contentHeight > height && dragLocks === 0
        ScrollBar.vertical: AppScrollBar {
            policy: contentFlick.contentHeight > contentFlick.height
                    ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
        }

        Column {
            id: exportColumn
            spacing: Theme.spacingXl
            width: contentFlick.width

            ExportDialogModeBar { dialog: root }

            ExportDialogWorkAreaCheck { dialog: root }

            ExportDialogVideoSection {
                id: videoSection
                dialog: root
            }

            ExportDialogGifSection {
                id: gifSection
                dialog: root
            }

            ExportDialogAudioSection {
                id: audioSection
                dialog: root
            }
        }
    }
}
