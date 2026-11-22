import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
// Phone preview-and-edit for one media-bin row: watch it, keep a range, crop the picture,
// then save over the bin row.
//
// A page rather than the desktop MediaPreviewWindow. On Android a second top-level Window is
// handed the whole screen with no insets, so that one drew its header under the status bar and
// its controls off the right edge; and its QtMultimedia VideoOutput paints black there, since
// the video frame's texture belongs to the main window's scene graph. This renders frames from
// EditorState's own single-clip player through image://clippreview instead, which is the same
// route SpeedCurveWindow takes and the same FFmpeg the timeline decodes with.
Item {
    id: root

    // Pushed onto the shell's StackView, which is what actually covers the editor: an item
    // layered over the stack still lost the top bar and the rail to it.
    signal closed()

    property int assetIndex: -1
    // Set when cropping a timeline clip (openClip) rather than editing a bin row. The crop then
    // lands on that clip alone, and trim and rotate do not apply: the clip has its own in/out.
    property string clipId: ""
    // The clip's frame as opened, which is what "unchanged" means in clip mode.
    property rect clipBaseFrame: Qt.rect(0, 0, 1, 1)
    property string assetId: ""
    property string kind: ""
    property string sourcePath: ""
    property string assetName: ""
    property string filmstripPath: ""
    property real durationSeconds: 0
    property int sourceWidth: 0
    property int sourceHeight: 0
    // Native: the file's own probed display-matrix rotation. Override: the bin-preview
    // correction, -1 when none. Effective is whichever of the two actually applies.
    property int rotationDegrees: 0
    property int rotationOverride: -1
    property int effectiveRotation: 0

    property real inSeconds: 0
    property real outSeconds: 0
    property real cropX: 0
    property real cropY: 0
    property real cropW: 1
    property real cropH: 1
    property bool cropRatioLocked: true

    // The trim already saved non-destructively on the asset (AssetLibrary::setAssetTrim) — the
    // baseline "no edit yet" reverts to, since a plain trim never re-encodes the file and so is
    // never reflected in durationSeconds the way an old encode-based save used to be.
    property real persistedInSeconds: 0
    property real persistedOutSeconds: 0

    // "trim" | "crop". Crop starts off: the handles used to be live over the picture the
    // moment the screen opened, with nothing saying what they were.
    property string mode: "trim"

    readonly property bool isImage: kind === "image"
    readonly property bool isAudio: kind === "audio"
    readonly property bool isVideo: kind === "video"
    readonly property bool canCrop: !isAudio
    readonly property bool canTrim: clipId.length === 0 && !isImage && durationSeconds > 0.05
    readonly property bool canPlay: !isImage

    readonly property int displayW: {
        const rot = Math.abs(root.effectiveRotation)
        return (rot === 90 || rot === 270) ? root.sourceHeight : root.sourceWidth
    }
    readonly property int displayH: {
        const rot = Math.abs(root.effectiveRotation)
        return (rot === 90 || rot === 270) ? root.sourceWidth : root.sourceHeight
    }

    readonly property bool cropDirty: clipId.length > 0
                                      ? (Math.abs(cropX - clipBaseFrame.x) > 0.001
                                         || Math.abs(cropY - clipBaseFrame.y) > 0.001
                                         || Math.abs(cropW - clipBaseFrame.width) > 0.001
                                         || Math.abs(cropH - clipBaseFrame.height) > 0.001)
                                      : (cropX > 0.001 || cropY > 0.001
                                         || cropW < 0.999 || cropH < 0.999)
    readonly property bool trimDirty: canTrim
                                      && (Math.abs(inSeconds - persistedInSeconds) > 0.02
                                          || Math.abs(outSeconds - persistedOutSeconds) > 0.02)
    readonly property bool dirty: cropDirty || trimDirty
    readonly property bool saving: EditorState.bin.editing && !EditorState.bin.editIsConversion
    readonly property real position: EditorState.assetPreview.position

    function openFor(index) {
        const asset = AssetLibrary.assetAt(index)
        if (!asset || Object.keys(asset).length === 0) {
            root.closed()
            return
        }
        EditorState.assetPreview.windowOpen = true
        root.clipId = ""
        root.assetIndex = index
        root.assetId = asset.id || ""
        root.kind = asset.kind || ""
        root.sourcePath = asset.path || ""
        root.assetName = asset.name || ""
        root.filmstripPath = asset.filmstripPath || ""
        root.durationSeconds = asset.durationSeconds || 0
        root.sourceWidth = asset.width || 0
        root.sourceHeight = asset.height || 0
        root.rotationDegrees = asset.rotationDegrees || 0
        root.rotationOverride = (asset.rotationOverride === undefined || asset.rotationOverride === null)
                                 ? -1 : asset.rotationOverride
        root.effectiveRotation = (asset.effectiveRotation === undefined || asset.effectiveRotation === null)
                                  ? root.rotationDegrees : asset.effectiveRotation
        root.persistedInSeconds = asset.trimInSeconds || 0
        root.persistedOutSeconds = (asset.trimOutSeconds === undefined || asset.trimOutSeconds === null
                                     || asset.trimOutSeconds < 0)
                                    ? root.durationSeconds : asset.trimOutSeconds
        root.mode = "trim"
        resetEdits()
        const frame = asset.sourceFrame
        if (!root.isAudio && frame) {
            root.cropX = frame.x; root.cropY = frame.y
            root.cropW = frame.width; root.cropH = frame.height
        }
        EditorState.assetPreview.begin(index)
    }

    function openClip(track, index) {
        const clip = EditorState.clipAt(track, index)
        if (!clip || (clip.kind !== "video" && clip.kind !== "image")) {
            root.closed()
            return
        }
        EditorState.assetPreview.pause()
        EditorState.assetPreview.windowOpen = true
        root.clipId = clip.id
        root.assetIndex = -1
        root.assetId = ""
        root.kind = clip.kind
        root.sourcePath = clip.path
        root.assetName = clip.name
        root.filmstripPath = clip.filmstripPath || ""
        root.durationSeconds = clip.sourceDuration
        root.sourceWidth = clip.sourceWidth
        root.sourceHeight = clip.sourceHeight
        root.rotationDegrees = clip.sourceRotation
        root.rotationOverride = -1
        root.effectiveRotation = clip.sourceRotation
        root.inSeconds = clip.inPoint
        root.outSeconds = clip.outPoint
        const frame = clip.sourceFrame
        root.clipBaseFrame = Qt.rect(frame.x, frame.y, frame.width, frame.height)
        root.cropX = frame.x; root.cropY = frame.y
        root.cropW = frame.width; root.cropH = frame.height
        root.mode = "crop"
        EditorState.assetPreview.beginClip(track, index)
        root.seekTo(root.inSeconds)
    }

    // Steps the bin's rotation correction by 90° and reopens the preview session so the player's
    // decoder picks up the new correction. Video only: image/audio clips have no lossless
    // pixel-rotation path on the timeline (see AppController::applyAssetLayout).
    function rotate90() {
        if (root.assetIndex < 0 || !root.isVideo)
            return
        EditorState.setAssetRotation(root.assetIndex, (root.effectiveRotation + 90) % 360)
    }

    // Picks up a rotation change from wherever it came from (the button above, or an undo) and
    // reopens the preview session at the same spot so the decoder applies it.
    function applyRotationFromAsset(asset) {
        const nextOverride = (asset.rotationOverride === undefined || asset.rotationOverride === null)
                              ? -1 : asset.rotationOverride
        const next = (asset.effectiveRotation === undefined || asset.effectiveRotation === null)
                      ? root.rotationDegrees : asset.effectiveRotation
        if (nextOverride === root.rotationOverride && next === root.effectiveRotation)
            return
        const wasPlaying = EditorState.assetPreview.playing
        const at = root.position
        root.rotationOverride = nextOverride
        root.effectiveRotation = next
        EditorState.assetPreview.begin(root.assetIndex)
        root.seekTo(at)
        if (wasPlaying)
            EditorState.assetPreview.play()
    }

    function close() {
        EditorState.assetPreview.pause()
        EditorState.assetPreview.end()
        root.assetIndex = -1
        EditorState.assetPreview.windowOpen = false
        root.closed()
    }

    function resetEdits() {
        if (root.clipId.length === 0) {
            root.inSeconds = root.persistedInSeconds
            root.outSeconds = root.persistedOutSeconds
        }
        root.cropX = 0
        root.cropY = 0
        root.cropW = 1
        root.cropH = 1
    }

    function lockCropRatioFromWidth() {
        const size = Math.max(0.08, Math.min(1, root.cropW))
        root.cropX = Math.max(0, Math.min(1 - size, root.cropX))
        root.cropY = Math.max(0, Math.min(1 - size, root.cropY))
        root.cropW = size
        root.cropH = size
    }

    function formatTime(seconds) {
        const s = Math.max(0, seconds)
        const total = Math.floor(s)
        const m = Math.floor(total / 60)
        const sec = total % 60
        function pad(n) { return n.toString().padStart(2, "0") }
        return m + ":" + pad(sec)
    }

    function clampRange() {
        const minSpan = root.isVideo ? 0.05 : 0.02
        const dur = Math.max(minSpan, root.durationSeconds)
        root.inSeconds = Math.max(0, Math.min(root.inSeconds, dur - minSpan))
        root.outSeconds = Math.max(root.inSeconds + minSpan, Math.min(root.outSeconds, dur))
    }

    function seekTo(seconds) {
        if (root.isImage)
            return
        EditorState.assetPreview.seek(Math.max(0, seconds))
    }

    function togglePlay() {
        if (!root.canPlay)
            return
        if (EditorState.assetPreview.playing) {
            EditorState.assetPreview.pause()
            return
        }
        const at = root.position
        if (at < root.inSeconds - 0.02 || at >= root.outSeconds - 0.02)
            seekTo(root.inSeconds)
        EditorState.assetPreview.play()
    }

    // Playback stays inside the kept range, so what plays is what a save would keep.
    onPositionChanged: {
        if (!EditorState.assetPreview.playing || root.isImage)
            return
        if (root.position >= root.outSeconds - 0.01)
            seekTo(root.inSeconds)
    }

    Connections {
        target: EditorState.bin
        function onEditFinished(ok, message) {
            // A background frame-rate conversion is not this sheet's save.
            if (ok && !EditorState.bin.editIsConversion)
                root.close()
        }
    }

    Connections {
        target: EditorState
        function onProjectReset() {
            root.close()
        }
    }

    // The rotated thumbnail/filmstrip regenerate on a background job (MediaThumbnail::generate),
    // so the strip below needs to pick up the new file once it lands.
    Connections {
        target: AssetLibrary
        function onAssetMetadataChanged(assetId) {
            if (assetId !== root.assetId)
                return
            root.filmstripPath = AssetLibrary.filmstripAt(root.assetIndex)
            const asset = AssetLibrary.assetAt(root.assetIndex)
            if (asset && asset.id === root.assetId)
                root.applyRotationFromAsset(asset)
        }
    }

    // Swallows anything aimed at the editor underneath.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: (wheel) => wheel.accepted = true
    }

    Rectangle {
        id: pageBackground
        anchors.fill: parent
        color: Theme.c.androidMediaPreview.background
    }

    readonly property real safeTop: SafeArea.margins.top
    readonly property real safeBottom: SafeArea.margins.bottom
    readonly property real safeLeft: SafeArea.margins.left
    readonly property real safeRight: SafeArea.margins.right

    // ----- Header ---------------------------------------------------------------------------
    AndroidMediaPreviewHeader {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: root.safeLeft
        anchors.rightMargin: root.safeRight
        anchors.topMargin: root.safeTop
        height: Theme.androidTopBarHeight
        assetName: root.assetName
        backEnabled: !root.saving
        canSave: root.dirty && !root.saving && (root.assetIndex >= 0 || root.clipId.length > 0)
        onBackRequested: root.close()
        onSaveRequested: {
            EditorState.assetPreview.pause()
            if (root.clipId.length > 0) {
                if (EditorState.setClipSourceFrame(root.clipId, root.cropX, root.cropY, root.cropW, root.cropH))
                    root.close()
                return
            }
            EditorState.bin.saveEdit(root.assetIndex, root.inSeconds,
                                      root.canTrim ? root.outSeconds : -1,
                                      root.cropX, root.cropY, root.cropW, root.cropH)
        }
    }

    // ----- Stage ----------------------------------------------------------------------------
    Rectangle {
        id: stage
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: controls.top
        anchors.topMargin: Theme.spacingMd
        anchors.bottomMargin: Theme.spacingMd
        anchors.leftMargin: root.safeLeft + Theme.spacingMd
        anchors.rightMargin: root.safeRight + Theme.spacingMd
        radius: Theme.c.androidMediaPreview.stageRadius
        color: Theme.c.androidMediaPreview.stageBackground
        clip: true

        readonly property real aspect: {
            if (root.displayW > 0 && root.displayH > 0)
                return root.displayW / root.displayH
            if (EditorState.assetPreview.frameSize.height > 0)
                return EditorState.assetPreview.frameSize.width
                       / EditorState.assetPreview.frameSize.height
            return 16 / 9
        }

        // The picture as drawn, letterboxing excluded — the crop rect is mapped onto this.
        readonly property var fit: {
            const scale = Math.min(width / Math.max(1, stage.aspect), height)
            const h = Math.max(1, scale)
            const w = h * stage.aspect
            return { x: (width - w) / 2, y: (height - h) / 2, w: w, h: h }
        }

        Image {
            id: frame
            visible: !root.isAudio
            x: stage.fit.x
            y: stage.fit.y
            width: stage.fit.w
            height: stage.fit.h
            fillMode: Image.PreserveAspectFit
            // Frame pixels change behind one URL, so neither the cache nor a stable source works.
            cache: root.isImage
            source: {
                if (root.isImage)
                    return root.sourcePath.length > 0
                           ? EditorState.imageUrl(root.sourcePath) : ""
                if (!EditorState.assetPreview.active)
                    return ""
                return "image://clippreview/frame?rev=" + EditorState.assetPreview.revision
            }
        }

        Column {
            visible: root.isAudio
            anchors.centerIn: parent
            spacing: Theme.spacingLg
            IconGlyph {
                anchors.horizontalCenter: parent.horizontalCenter
                glyph: Theme.icons.music
                iconSize: Theme.iconSizeXl * 2
                iconColor: Theme.c.androidMediaPreview.audioIcon
            }
            ThemedLabel {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Audio only — trim it below")
                tone: "muted"
                size: "sm"
            }
        }

        // Crop frame. Only while the Crop tab is open, so the picture is a plain picture until
        // the user asks to crop it.
        CropFrame {
            id: cropHost
            visible: root.canCrop && root.mode === "crop"
            x: stage.fit.x
            y: stage.fit.y
            width: stage.fit.w
            height: stage.fit.h
            touch: true
            spaceSize: Qt.size(1, 1)
            contentSize: Qt.size(Math.max(1, root.displayW), Math.max(1, root.displayH))
            crop: Qt.rect(root.cropX, root.cropY, root.cropW, root.cropH)
            aspect: root.cropRatioLocked ? root.displayW / Math.max(1, root.displayH) : 0
            minSize: 0.08
            editable: !root.saving
            onEdited: (c) => {
                root.cropX = c.x
                root.cropY = c.y
                root.cropW = c.width
                root.cropH = c.height
            }
        }

        IconButton {
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: Theme.spacingSm
            z: 4
            visible: !root.isAudio && root.mode === "crop"
            glyph: root.cropRatioLocked ? Theme.icons.lock : Theme.icons.lockOpen
            tooltip: root.cropRatioLocked
                     ? qsTr("Unlock crop ratio")
                     : qsTr("Lock crop ratio")
            active: root.cropRatioLocked
            buttonSize: 36
            iconSize: Theme.iconSizeSm
            variant: "ghost"
            onClicked: {
                root.cropRatioLocked = !root.cropRatioLocked
                if (root.cropRatioLocked)
                    root.lockCropRatioFromWidth()
            }
        }

        // Play/pause over the picture, where a phone player puts it.
        AndroidMediaPlayBadge {
            visible: root.canPlay && root.mode !== "crop"
            anchors.centerIn: parent
            playing: EditorState.assetPreview.playing
        }

        MouseArea {
            anchors.fill: parent
            enabled: root.canPlay && root.mode !== "crop" && !root.saving
            onClicked: {
                Haptics.confirm()
                root.togglePlay()
            }
        }

        AndroidOnMediaReadout {
            visible: root.mode === "crop"
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.margins: Theme.spacingMd
            text: {
                const w = Math.max(1, Math.round(root.displayW * root.cropW))
                const h = Math.max(1, Math.round(root.displayH * root.cropH))
                return w + "×" + h
            }
        }
    }

    // ----- Controls -------------------------------------------------------------------------
    Column {
        id: controls
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: root.safeLeft + Theme.spacingMd
        anchors.rightMargin: root.safeRight + Theme.spacingMd
        anchors.bottomMargin: root.safeBottom + Theme.spacingMd
        spacing: Theme.spacingMd

        // Mode picker. Two full-width targets rather than a row of desktop-sized buttons that
        // ran off the screen edge.
        Row {
            id: modeRow
            width: parent.width
            visible: root.canTrim || root.canCrop
            spacing: Theme.spacingSm

            readonly property int slots: (root.canTrim ? 1 : 0) + (root.canCrop ? 1 : 0)
            readonly property real slotWidth:
                (width - spacing * Math.max(0, slots - 1)) / Math.max(1, slots)

            ThemedButton {
                visible: root.canTrim
                width: modeRow.slotWidth
                height: Theme.controlHeight
                variant: root.mode === "trim" ? "primary" : "secondary"
                text: qsTr("Trim")
                glyph: Theme.icons.scissors
                enabled: !root.saving
                onClicked: root.mode = "trim"
            }
            ThemedButton {
                visible: root.canCrop
                width: modeRow.slotWidth
                height: Theme.controlHeight
                variant: root.mode === "crop" ? "primary" : "secondary"
                text: qsTr("Crop")
                glyph: Theme.icons.crop
                enabled: !root.saving
                onClicked: {
                    EditorState.assetPreview.pause()
                    root.mode = "crop"
                }
            }
        }

        ThemedButton {
            width: parent.width
            height: Theme.controlHeight
            visible: root.isVideo && root.clipId.length === 0
            variant: "secondary"
            text: qsTr("Rotate")
            enabled: !root.saving
            onClicked: root.rotate90()
        }

        // ----- Trim -------------------------------------------------------------------------
        Column {
            width: parent.width
            visible: root.mode === "trim" && root.canTrim
            spacing: Theme.spacingSm

            ThemedLabel {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                size: "sm"
                tone: "muted"
                text: qsTr("Keeping %1 to %2  ·  %3s")
                      .arg(root.formatTime(root.inSeconds))
                      .arg(root.formatTime(root.outSeconds))
                      .arg((root.outSeconds - root.inSeconds).toFixed(1))
            }

            Rectangle {
                id: strip
                width: parent.width
                height: 72
                radius: Theme.c.androidMediaPreview.stripRadius
                color: Theme.c.androidMediaPreview.stripBackground
                border.width: Theme.c.androidMediaPreview.stripBorderWidth
                border.color: Theme.c.androidMediaPreview.stripBorder
                clip: true

                readonly property real dur: Math.max(0.001, root.durationSeconds)
                readonly property real inX: (root.inSeconds / dur) * width
                readonly property real outX: (root.outSeconds / dur) * width
                readonly property real playX: (root.position / dur) * width

                ClipFilmstrip {
                    anchors.fill: parent
                    anchors.margins: Theme.c.androidMediaPreview.stripBorderWidth
                    visible: root.filmstripPath.length > 0
                    filmstripPath: root.filmstripPath
                    frameWidth: Math.max(1, width / frameCount)
                    sourcePath: root.sourcePath
                    rotationCorrection: (root.effectiveRotation - root.rotationDegrees + 360) % 360
                    inPoint: 0
                    outPoint: root.durationSeconds
                    sourceDuration: root.durationSeconds
                }

                Rectangle {
                    width: strip.inX
                    height: parent.height
                    color: Theme.c.androidMediaPreview.trimShade
                }
                Rectangle {
                    x: strip.outX
                    width: Math.max(0, parent.width - x)
                    height: parent.height
                    color: Theme.c.androidMediaPreview.trimShade
                }
                Rectangle {
                    x: strip.inX
                    width: Math.max(2, strip.outX - strip.inX)
                    height: parent.height
                    color: "transparent"
                    border.width: Theme.c.androidMediaPreview.selectionBorderWidth
                    border.color: Theme.c.androidMediaPreview.selectionBorder
                }
                Rectangle {
                    x: strip.playX - 1
                    width: Theme.c.androidMediaPreview.playheadWidth
                    height: parent.height
                    color: Theme.c.androidMediaPreview.playhead
                }

                MouseArea {
                    anchors.fill: parent
                    anchors.leftMargin: 22
                    anchors.rightMargin: 22
                    enabled: !root.saving
                    onPressed: (mouse) => {
                        EditorState.assetPreview.pause()
                        Haptics.press()
                        root.seekTo(((mouse.x + 22) / Math.max(1, strip.width))
                                    * root.durationSeconds)
                    }
                    onPositionChanged: (mouse) => {
                        if (!pressed)
                            return
                        const t = ((mouse.x + 22) / Math.max(1, strip.width)) * root.durationSeconds
                        root.seekTo(Math.max(0, Math.min(root.durationSeconds, t)))
                    }
                }

                // Grabbable ends. The desktop hairline gave no hint that the range could be
                // dragged; these are thumb-width and carry a grip.
                Repeater {
                    model: [{ edge: "in" }, { edge: "out" }]
                    delegate: AndroidMediaTrimHandle {
                        preview: root
                        stripWidth: strip.width
                        inX: strip.inX
                        outX: strip.outX
                    }
                }
            }

            Row {
                width: parent.width
                spacing: Theme.spacingSm

                readonly property real slotWidth: (width - spacing * 2) / 3

                ThemedButton {
                    width: parent.slotWidth
                    height: Theme.controlHeight
                    variant: "secondary"
                    text: qsTr("Start here")
                    enabled: !root.saving
                    onClicked: {
                        root.inSeconds = root.position
                        root.clampRange()
                    }
                }
                ThemedButton {
                    width: parent.slotWidth
                    height: Theme.controlHeight
                    variant: "secondary"
                    text: qsTr("End here")
                    enabled: !root.saving
                    onClicked: {
                        root.outSeconds = root.position
                        root.clampRange()
                    }
                }
                ThemedButton {
                    width: parent.slotWidth
                    height: Theme.controlHeight
                    variant: "ghost"
                    text: qsTr("Undo trim")
                    enabled: root.trimDirty && !root.saving
                    onClicked: {
                        root.inSeconds = root.persistedInSeconds
                        root.outSeconds = root.persistedOutSeconds
                        root.seekTo(root.inSeconds)
                    }
                }
            }
        }

        // ----- Crop -------------------------------------------------------------------------
        Column {
            width: parent.width
            visible: root.mode === "crop" && root.canCrop
            spacing: Theme.spacingSm

            ThemedLabel {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                size: "sm"
                tone: "muted"
                text: qsTr("Drag inside the box to move it, corners to resize")
            }

            ThemedButton {
                width: parent.width
                height: Theme.controlHeight
                variant: "ghost"
                text: qsTr("Undo crop")
                enabled: root.cropDirty && !root.saving
                onClicked: {
                    root.cropX = 0
                    root.cropY = 0
                    root.cropW = 1
                    root.cropH = 1
                }
            }
        }

        ThemedLabel {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            size: "sm"
            tone: "muted"
            text: root.saving
                  ? (EditorState.bin.editStatus.length > 0
                     ? EditorState.bin.editStatus : qsTr("Saving…"))
                  : root.dirty
                    ? (!root.isAudio ? qsTr("Save keeps the original file and stores this crop.") : qsTr("Save keeps the original file and stores this trim."))
                    : qsTr("Nothing changed yet. Trim or crop above, or go back and drag this onto the timeline.")
        }
    }

    // ----- Saving ---------------------------------------------------------------------------
    AndroidMediaSavingScrim {
        visible: root.saving
        anchors.fill: parent
        z: 10
        progress: EditorState.bin.editProgress
        statusText: EditorState.bin.editStatus.length > 0
                    ? EditorState.bin.editStatus : qsTr("Saving…")
        onCancelRequested: EditorState.bin.cancelEdit()
    }
}
