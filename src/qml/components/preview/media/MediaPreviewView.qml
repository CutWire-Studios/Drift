import QtQuick
import QtQuick.Window
import Drift
// Crop is stored in the project and always previews the original source. Frames come from
// EditorState.assetPreview's single-clip player (the timeline's own FFmpeg decode, paused
// timeline, capped frame size) rather than a separate QtMultimedia pipeline per window.
Item {
    id: root

    signal closeRequested()

    // Off in the dock panel, where Space/I/O would collide with the editor's own shortcuts.
    property bool shortcutsEnabled: true
    property string clipId: ""
    property int assetIndex: -1
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
    // Straighten, degrees clockwise (Clip::sourceFrameAngle). Timeline clips only: a bin item's
    // crop is rendered into a new file, which has no way to turn the picture.
    property real cropAngle: 0
    // The angle the clip had when opened, so levelling a saved tilt back to 0° is still a change.
    property real openedAngle: 0
    // A full-size crop is normally clean, but Reset must still be savable so it can replace a
    // previously stored crop with the whole picture.
    property bool frameResetPending: false
    // Crop framing starts constrained to the source video ratio. Turning the lock back on uses
    // the current width and updates height immediately.
    property bool cropRatioLocked: true

    // The trim already saved non-destructively on the asset (AssetLibrary::setAssetTrim) — the
    // baseline "no edit yet" reverts to, since a plain trim never re-encodes the file and so is
    // never reflected in durationSeconds the way an old encode-based save used to be.
    property real persistedInSeconds: 0
    property real persistedOutSeconds: 0

    readonly property bool isImage: kind === "image"
    readonly property bool isAudio: kind === "audio"
    readonly property bool isVideo: kind === "video"
    readonly property bool canCrop: !isAudio
    readonly property bool canStraighten: canCrop && clipId.length > 0
    readonly property bool canTrim: clipId.length === 0 && !isImage && durationSeconds > 0.05
    // Timeline clips cannot be trimmed from the crop editor, but they still need the
    // filmstrip/ruler so the user can scrub to the exact frame being cropped.
    readonly property bool canScrub: !isImage && durationSeconds > 0.05
    // The crop editor presents a selected interval, whether it comes from a bin trim or
    // a placed timeline clip. Transport time is therefore relative to that interval, never the
    // source file's absolute/project timestamp.
    readonly property real mediaRangeStart: canScrub ? inSeconds : 0
    readonly property real mediaRangeDuration: canScrub
                                           ? Math.max(0, outSeconds - inSeconds) : 0

    readonly property int displayW: {
        const rot = Math.abs(root.effectiveRotation)
        return (rot === 90 || rot === 270) ? root.sourceHeight : root.sourceWidth
    }
    readonly property int displayH: {
        const rot = Math.abs(root.effectiveRotation)
        return (rot === 90 || rot === 270) ? root.sourceWidth : root.sourceHeight
    }

    readonly property bool cropDirty: cropX > 0.001 || cropY > 0.001
                                      || cropW < 0.999 || cropH < 0.999
                                      || Math.abs(cropAngle) > 0.01
    readonly property bool trimDirty: canTrim
                                      && (Math.abs(inSeconds - persistedInSeconds) > 0.02
                                          || Math.abs(outSeconds - persistedOutSeconds) > 0.02)
    readonly property bool dirty: cropDirty || trimDirty || frameResetPending
                                  || Math.abs(cropAngle - openedAngle) > 0.01
    readonly property bool saving: EditorState.bin.editing && !EditorState.bin.editIsConversion

    // Bin videos end on a second page that offers an upscale; everything else saves directly.
    readonly property bool hasFinishPage: isVideo && clipId.length === 0
    property int page: 0
    // Set while this window's "Upscale" renders the trimmed copy it hands to the enhance window.
    property bool renderingCopy: false
    readonly property int outputWidth: Math.max(1, Math.round(displayW * cropW))
    readonly property int outputHeight: Math.max(1, Math.round(displayH * cropH))
    readonly property bool suggestUpscale: Math.min(outputWidth, outputHeight) < 700
    readonly property real position: EditorState.assetPreview.position
    readonly property bool playing: EditorState.assetPreview.playing

    function openFor(index) {
        const asset = AssetLibrary.assetAt(index)
        if (!asset || Object.keys(asset).length === 0)
            return
        EditorState.assetPreview.windowOpen = true
        EditorState.assetPreview.pause()
        root.clipId = ""
        root.page = 0
        root.renderingCopy = false
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
        resetEdits()
        root.openedAngle = 0
        const frame = asset.sourceFrame
        if (root.canCrop && frame) {
            root.cropX = frame.x; root.cropY = frame.y
            root.cropW = frame.width; root.cropH = frame.height
        }
        root.frameResetPending = false
        EditorState.assetPreview.begin(index)
        root.seekTo(root.inSeconds)
    }

    // Steps the bin's rotation correction by 90° and keeps it — independent of crop/trim, which
    // still need Save. Kept only for video: image/audio clips have no lossless pixel-rotation
    // path on the timeline (see AppController::applyAssetLayout / Clip::rotationCorrection).
    function rotate90() {
        if (root.assetIndex < 0 || !root.isVideo)
            return
        const next = (root.effectiveRotation + 90) % 360
        if (EditorState.setAssetRotation(root.assetIndex, next)) {
            root.rotationOverride = next
            root.effectiveRotation = next
            root.restartPreview()
        }
    }

    // The preview clip carries the rotation baked in, so a rotation change re-begins it.
    function restartPreview() {
        const wasPlaying = root.playing
        const at = root.position
        EditorState.assetPreview.begin(root.assetIndex)
        root.seekTo(at)
        if (wasPlaying)
            EditorState.assetPreview.play()
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
        root.cropAngle = 0
        root.frameResetPending = true
    }

    function lockCropRatioFromWidth() {
        // cropW/cropH are normalized against the same source, so a frame with the source's
        // original ratio has equal normalized width and height.
        const size = Math.max(0.08, Math.min(1, root.cropW))
        root.cropX = Math.max(0, Math.min(1 - size, root.cropX))
        root.cropY = Math.max(0, Math.min(1 - size, root.cropY))
        root.cropW = size
        root.cropH = size
    }

    function openClip(track, index) {
        const clip = EditorState.clipAt(track, index)
        if (!clip || (clip.kind !== "video" && clip.kind !== "image"))
            return
        EditorState.assetPreview.pause()
        root.clipId = clip.id
        root.page = 0
        root.renderingCopy = false
        root.assetIndex = -1
        root.assetId = ""
        root.kind = clip.kind
        root.sourcePath = clip.path
        root.assetName = clip.name
        root.sourceWidth = clip.sourceWidth
        root.sourceHeight = clip.sourceHeight
        root.rotationDegrees = clip.sourceRotation
        root.rotationOverride = -1
        root.effectiveRotation = clip.sourceRotation
        root.durationSeconds = clip.sourceDuration
        root.filmstripPath = clip.filmstripPath || ""
        root.inSeconds = clip.inPoint
        root.outSeconds = clip.outPoint
        const frame = clip.sourceFrame
        root.cropX = frame.x; root.cropY = frame.y
        root.cropW = frame.width; root.cropH = frame.height
        root.cropAngle = clip.sourceFrameAngle || 0
        root.openedAngle = root.cropAngle
        root.frameResetPending = false
        EditorState.assetPreview.beginClip(track, index)
        root.seekTo(root.inSeconds)
    }

    function formatTime(seconds) {
        const s = Math.max(0, seconds)
        const total = Math.floor(s)
        const m = Math.floor(total / 60)
        const sec = total % 60
        const cs = Math.floor((s - total) * 100)
        function pad(n) { return n.toString().padStart(2, "0") }
        return pad(m) + ":" + pad(sec) + "." + pad(cs)
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

    function upscale() {
        EditorState.assetPreview.pause()
        if (AssetLibrary.assetAt(root.assetIndex).id !== root.assetId)
            return
        // Nothing trimmed or cropped: the original is already the video to enhance.
        if (!root.cropDirty && root.inSeconds < 0.02 && root.outSeconds > root.durationSeconds - 0.02) {
            const id = root.assetId
            root.closeRequested()
            ShellBridge.main.openRestoreAsset(id)
            return
        }
        root.renderingCopy = EditorState.bin.renderCopy(root.assetIndex, root.inSeconds,
                                                         root.canTrim ? root.outSeconds : -1,
                                                         root.cropX, root.cropY, root.cropW, root.cropH)
    }

    function togglePlay() {
        if (root.isImage)
            return
        if (root.playing) {
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
        if (!root.playing || root.isImage)
            return
        if (root.position >= root.outSeconds - 0.01)
            seekTo(root.inSeconds)
    }

    function release() {
        EditorState.assetPreview.pause()
        EditorState.assetPreview.end()
        if (root.saving)
            EditorState.bin.cancelEdit()
        EditorState.assetPreview.windowOpen = false
    }

    Connections {
        target: EditorState.bin
        function onCopyRendered(assetId) {
            if (!root.renderingCopy)
                return
            root.renderingCopy = false
            root.closeRequested()
            ShellBridge.main.openRestoreAsset(assetId)
        }
        function onEditFinished(ok, message) {
            root.renderingCopy = false
            // A background frame-rate conversion is not this window's save.
            if (!ok || EditorState.bin.editIsConversion)
                return
            root.closeRequested()
        }
    }

    Connections {
        target: EditorState
        function onProjectReset() {
            root.closeRequested()
        }
    }

    // The rotated thumbnail/filmstrip regenerate on a background job (MediaThumbnail::generate),
    // so the strip below needs to pick up the new file once it lands. The rotation itself is
    // re-read too: an undo of the rotate while this window is open lands here as well.
    Connections {
        target: AssetLibrary
        function onAssetMetadataChanged(assetId) {
            if (assetId !== root.assetId)
                return
            root.filmstripPath = AssetLibrary.filmstripAt(root.assetIndex)
            const asset = AssetLibrary.assetAt(root.assetIndex)
            if (!asset || asset.id !== root.assetId)
                return
            if (asset.rotationOverride === root.rotationOverride
                    && asset.effectiveRotation === root.effectiveRotation)
                return
            root.rotationOverride = asset.rotationOverride
            root.effectiveRotation = asset.effectiveRotation
            root.restartPreview()
        }
    }

    Shortcut {
        sequence: "Space"
        enabled: root.shortcutsEnabled && root.page === 0
        onActivated: root.togglePlay()
    }
    Shortcut {
        sequence: "I"
        enabled: root.shortcutsEnabled && root.canTrim && !root.saving && root.page === 0
        onActivated: {
            root.inSeconds = Math.max(0, root.position)
            root.clampRange()
        }
    }
    Shortcut {
        sequence: "O"
        enabled: root.shortcutsEnabled && root.canTrim && !root.saving && root.page === 0
        onActivated: {
            root.outSeconds = Math.max(root.inSeconds, root.position)
            root.clampRange()
        }
    }
    Shortcut {
        sequence: "Esc"
        enabled: root.shortcutsEnabled && !root.saving
        onActivated: root.closeRequested()
    }

    Column {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: footer.top
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingLg
        visible: root.page === 0

        ThemedLabel {
            id: hintLabel
            width: parent.width
            wrapMode: Text.WordWrap
            text: root.isAudio
                  ? qsTr("Play the clip and drag the ends to keep only the part you want. Save replaces this item in the media bin.")
                  : root.isImage
                    ? qsTr("Drag the box to crop. The original image stays available, so the crop can be changed later.")
                    : qsTr("Drag the box to crop. The original video stays available, so the crop can be changed later.")
        }

        Rectangle {
            id: stage
            width: parent.width
            height: {
                let h = parent.height - hintLabel.height - Theme.spacingLg
                h -= transport.height + Theme.spacingLg
                // stripBlock, not strip: the ruler above the filmstrip strip is part of the same
                // visible block now and has to come out of this budget too, or its extra height
                // overflows into the transport row above and the footer below.
                if (root.canScrub)
                    h -= stripBlock.height + Theme.spacingLg
                if (root.canStraighten)
                    h -= straightenRow.height + Theme.spacingLg
                return Math.max(80, h)
            }
            radius: Theme.c.mediaPreviewWindow.stageRadius
            color: Theme.c.mediaPreviewWindow.stageBackground
            clip: true

            // Stills come straight from the file; video frames arrive already rotated from the
            // preview player, so both fit the stage the same way.
            Image {
                id: still
                visible: !root.isAudio
                anchors.fill: parent
                // The crop box stays level; the picture turns under it, about its centre.
                rotation: root.canStraighten ? root.cropAngle : 0
                fillMode: Image.PreserveAspectFit
                asynchronous: root.isImage
                // Frame pixels change behind one URL, so only a still may be cached.
                cache: root.isImage
                // Capped at the screen: a 48 MP photo decoded whole is ~190 MB.
                sourceSize: root.isImage
                            ? Qt.size(Math.ceil(Screen.width * Screen.devicePixelRatio),
                                      Math.ceil(Screen.height * Screen.devicePixelRatio))
                            : Qt.size(0, 0)
                source: {
                    if (root.isImage)
                        return root.sourcePath.length > 0 ? EditorState.imageUrl(root.sourcePath) : ""
                    if (!root.isVideo || !EditorState.assetPreview.active)
                        return ""
                    return "image://clippreview/frame?rev=" + EditorState.assetPreview.revision
                }
            }

            MediaPreviewAudioPlaceholder {
                visible: root.isAudio
                anchors.centerIn: parent
                assetName: root.assetName
            }

            // Crop frame, mapped onto the fitted picture so letterboxing is not part of the crop.
            Item {
                id: cropHost
                visible: root.canCrop
                x: (stage.width - still.paintedWidth) / 2
                y: (stage.height - still.paintedHeight) / 2
                width: still.paintedWidth
                height: still.paintedHeight

                readonly property real frameX: root.cropX * width
                readonly property real frameY: root.cropY * height
                readonly property real frameW: root.cropW * width
                readonly property real frameH: root.cropH * height
                readonly property real minFrac: 0.08

                function setCrop(nx, ny, nw, nh) {
                    const min = cropHost.minFrac
                    nw = Math.max(min, Math.min(1, nw))
                    nh = Math.max(min, Math.min(1, nh))
                    nx = Math.max(0, Math.min(1 - nw, nx))
                    ny = Math.max(0, Math.min(1 - nh, ny))
                    root.cropX = nx
                    root.cropY = ny
                    root.cropW = nw
                    root.cropH = nh
                }

                // Dim outside the keep-rect.
                MediaPreviewCropScrim {
                    host: cropHost
                }

                MediaPreviewCropFrame {
                    host: cropHost
                    editor: root
                    editing: !root.saving
                }

                Repeater {
                    model: [
                        { x: 0, y: 0, dx: -1, dy: -1 },
                        { x: 0.5, y: 0, dx: 0, dy: -1 },
                        { x: 1, y: 0, dx: 1, dy: -1 },
                        { x: 0, y: 0.5, dx: -1, dy: 0 },
                        { x: 1, y: 0.5, dx: 1, dy: 0 },
                        { x: 0, y: 1, dx: -1, dy: 1 },
                        { x: 0.5, y: 1, dx: 0, dy: 1 },
                        { x: 1, y: 1, dx: 1, dy: 1 }
                    ]
                    MediaPreviewCropGrip {
                        host: cropHost
                        editor: root
                        editing: !root.saving
                    }
                }
            }
        }

        Row {
            id: straightenRow
            visible: root.canStraighten
            width: parent.width
            height: visible ? implicitHeight : 0
            spacing: Theme.spacingMd

            ThemedLabel {
                id: straightenLabel
                anchors.verticalCenter: parent.verticalCenter
                size: "sm"
                tone: "muted"
                text: qsTr("Straighten")
            }

            ThemedSlider {
                id: straightenSlider
                label: qsTr("Straighten")
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(80, parent.width - straightenLabel.implicitWidth - straightenValue.width
                                    - straightenReset.width - parent.spacing * 3)
                from: -45
                to: 45
                stepSize: 0.1
                enabled: !root.saving
                valueFormatter: v => Number(v).toFixed(1) + "°"
                value: root.cropAngle
                onMoved: root.cropAngle = Math.round(value * 10) / 10
            }

            ThemedLabel {
                id: straightenValue
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(implicitWidth, 52)
                font.family: Theme.monoFontFamily
                size: "xs"
                tone: "muted"
                text: Number(root.cropAngle).toFixed(1) + "°"
            }

            IconButton {
                id: straightenReset
                anchors.verticalCenter: parent.verticalCenter
                glyph: Theme.icons.reset
                tooltip: qsTr("Level (0°)")
                buttonSize: 30
                iconSize: Theme.iconSizeSm
                variant: "ghost"
                enabled: !root.saving && root.cropAngle !== 0
                onClicked: root.cropAngle = 0
            }
        }

        Row {
            id: transport
            width: parent.width
            spacing: Theme.spacingMd

            IconButton {
                id: playButton
                visible: !root.isImage
                width: visible ? implicitWidth : 0
                anchors.verticalCenter: parent.verticalCenter
                glyph: root.playing
                       ? Theme.icons.pause : Theme.icons.play
                tooltip: root.playing ? qsTr("Pause") : qsTr("Play")
                enabled: !root.saving
                onClicked: root.togglePlay()
            }

            ThemedLabel {
                id: timeLabel
                visible: !root.isImage
                width: visible ? implicitWidth : 0
                anchors.verticalCenter: parent.verticalCenter
                text: root.formatTime(Math.max(0, root.position - root.mediaRangeStart))
                      + "  /  " + root.formatTime(root.mediaRangeDuration)
                size: "sm"
                tone: "default"
            }

            ThemedButton {
                id: setInButton
                visible: root.canTrim
                width: visible ? implicitWidth : 0
                variant: "ghost"
                text: qsTr("Set In")
                glyph: Theme.icons.setStart
                enabled: !root.saving
                onClicked: {
                    root.inSeconds = root.position
                    root.clampRange()
                }
            }
            ThemedButton {
                id: setOutButton
                visible: root.canTrim
                width: visible ? implicitWidth : 0
                variant: "ghost"
                text: qsTr("Set Out")
                glyph: Theme.icons.setEnd
                enabled: !root.saving
                onClicked: {
                    root.outSeconds = root.position
                    root.clampRange()
                }
            }

            ThemedButton {
                id: addSubclipButton
                visible: root.isVideo && root.clipId.length === 0
                width: visible ? implicitWidth : 0
                variant: "ghost"
                text: qsTr("Add sub-clip")
                enabled: !root.saving && root.outSeconds > root.inSeconds
                         && (root.inSeconds > 0.001 || root.outSeconds < root.durationSeconds - 0.001)
                onClicked: {
                    if (EditorState.bin.addSubclip(root.assetId, root.inSeconds, root.outSeconds).length > 0)
                        EditorState.setLastMessage(qsTr("Sub-clip added"))
                }
            }

            ThemedButton {
                id: rotateButton
                visible: root.isVideo && root.clipId.length === 0
                width: visible ? implicitWidth : 0
                variant: "ghost"
                text: qsTr("Rotate")
                enabled: !root.saving
                onClicked: root.rotate90()
            }

            // Keep source information and frame controls at the far end of the transport row.
            Item {
                height: 1
                width: Math.max(0, parent.width - playButton.width - timeLabel.width
                                - setInButton.width - setOutButton.width - addSubclipButton.width
                                - rotateButton.width
                                - frameSizeLabel.implicitWidth - frameRatioLock.width
                                - resetButton.implicitWidth - parent.spacing * 9)
            }

            ThemedLabel {
                id: frameSizeLabel
                visible: root.canCrop
                anchors.verticalCenter: parent.verticalCenter
                font.family: Theme.monoFontFamily
                size: "xs"
                tone: "muted"
                text: qsTr("Original: %1×%2 • Crop: %3×%4")
                      .arg(root.displayW).arg(root.displayH).arg(root.outputWidth).arg(root.outputHeight)
            }

            IconButton {
                id: frameRatioLock
                visible: root.canCrop
                width: visible ? buttonSize : 0
                anchors.verticalCenter: parent.verticalCenter
                glyph: root.cropRatioLocked ? Theme.icons.lock : Theme.icons.lockOpen
                tooltip: root.cropRatioLocked
                         ? qsTr("Unlock crop ratio")
                         : qsTr("Lock crop ratio")
                active: root.cropRatioLocked
                buttonSize: 30
                iconSize: Theme.iconSizeSm
                variant: "ghost"
                onClicked: {
                    root.cropRatioLocked = !root.cropRatioLocked
                    if (root.cropRatioLocked)
                        root.lockCropRatioFromWidth()
                }
            }

            ThemedButton {
                id: resetButton
                variant: "ghost"
                text: qsTr("Reset")
                enabled: root.dirty && !root.saving
                onClicked: root.resetEdits()
            }
        }

        Column {
            id: stripBlock
            visible: root.canScrub
            width: parent.width
            spacing: 2

            // A source-frame edit from the timeline is deliberately restricted to that clip's
            // source interval. Bin preview retains the full source range for trim editing.
            readonly property real rangeStart: root.clipId.length > 0 ? root.inSeconds : 0
            readonly property real rangeEnd: root.clipId.length > 0 ? root.outSeconds : root.durationSeconds
            readonly property real dur: Math.max(0.001, rangeEnd - rangeStart)
            readonly property real pxPerSecond: width / dur
            readonly property real playX: ((root.position) - rangeStart) * pxPerSecond

            // Ticks stay legible regardless of the clip's length: the smallest "nice" step from
            // this list whose label spacing is still wide enough not to overlap the next one —
            // the same idea TimelinePanel's ruler uses for its zoom-adaptive ticks, simplified
            // here since this strip has a fixed width for the whole clip and never zooms/pans.
            readonly property var tickSteps: [0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300,
                                              600, 900, 1800, 3600]
            readonly property real tickStep: {
                const minLabelPx = 56
                for (const s of stripBlock.tickSteps) {
                    if (s * stripBlock.pxPerSecond >= minLabelPx)
                        return s
                }
                return stripBlock.tickSteps[stripBlock.tickSteps.length - 1]
            }

            function formatTick(seconds) {
                const s = Math.max(0, seconds)
                const total = Math.floor(s)
                const h = Math.floor(total / 3600)
                const m = Math.floor((total % 3600) / 60)
                const sec = total % 60
                function pad(n) { return n.toString().padStart(2, "0") }
                let text = pad(h) + ":" + pad(m) + ":" + pad(sec)
                if (stripBlock.tickStep < 1)
                    text += "." + pad(Math.floor((s - total) * 100))
                return text
            }

            // Time ruler — ticks/timestamps across the clip's full duration, same idea as the
            // main timeline's ruler, plus a playhead handle so the strip below reads as scrubbing
            // a timeline rather than just a static filmstrip.
            Item {
                id: timeRuler
                width: parent.width
                height: 20
                clip: true

                Repeater {
                    model: Math.floor(stripBlock.dur / stripBlock.tickStep) + 1
                    MediaPreviewRulerTick {
                        rangeStart: stripBlock.rangeStart
                        step: stripBlock.tickStep
                        pxPerSecond: stripBlock.pxPerSecond
                        formatTick: (seconds) => stripBlock.formatTick(seconds)
                    }
                }

                // Playhead handle: a small flag at the top of the line that continues down
                // through the strip below, matching the timeline's playhead silhouette.
                MediaPreviewPlayheadCap {
                    playX: stripBlock.playX
                }
            }

            Rectangle {
                id: strip
                width: parent.width
                height: 64
                radius: Theme.c.mediaPreviewWindow.stripRadius
                color: Theme.c.mediaPreviewWindow.stripBackground
                border.width: Theme.c.mediaPreviewWindow.stripBorderWidth
                border.color: Theme.c.mediaPreviewWindow.stripBorder
                clip: true

                ClipFilmstrip {
                    anchors.fill: parent
                    anchors.margins: Theme.borderWidth
                    visible: root.filmstripPath.length > 0
                    filmstripPath: root.filmstripPath
                    frameWidth: Math.max(1, width / frameCount)
                    sourcePath: root.sourcePath
                    rotationCorrection: (root.effectiveRotation - root.rotationDegrees + 360) % 360
                    inPoint: stripBlock.rangeStart
                    outPoint: stripBlock.rangeEnd
                    sourceDuration: root.durationSeconds
                }

                readonly property real inX: ((root.inSeconds - stripBlock.rangeStart) / stripBlock.dur) * width
                readonly property real outX: ((root.outSeconds - stripBlock.rangeStart) / stripBlock.dur) * width

                MediaPreviewTrimShade {
                    leading: true
                    edge: strip.inX
                    span: strip.width
                }
                MediaPreviewTrimShade {
                    leading: false
                    edge: strip.outX
                    span: strip.width
                }
                MediaPreviewTrimSpan {
                    inX: strip.inX
                    outX: strip.outX
                }
                MediaPreviewPlayheadLine {
                    playX: stripBlock.playX
                }

                MouseArea {
                anchors.fill: parent
                enabled: !root.saving
                cursorShape: Qt.PointingHandCursor
                onPressed: (mouse) => {
                    const t = stripBlock.rangeStart
                              + (mouse.x / Math.max(1, width)) * stripBlock.dur
                    root.seekTo(t)
                }
                onPositionChanged: (mouse) => {
                    if (!pressed)
                        return
                    const t = stripBlock.rangeStart
                              + (mouse.x / Math.max(1, width)) * stripBlock.dur
                    root.seekTo(Math.max(stripBlock.rangeStart, Math.min(stripBlock.rangeEnd, t)))
                }
            }

            Repeater {
                model: [
                    { edge: "in" },
                    { edge: "out" }
                ]
                MediaPreviewTrimHandle {
                    editor: root
                    track: strip
                    inX: strip.inX
                    outX: strip.outX
                    editing: !root.saving
                }
            }
        }
        }
    }

    Column {
        anchors.horizontalCenter: parent.horizontalCenter
        // Centred in the space above the footer.
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: -(footer.height + Theme.spacingLg) / 2
        width: Math.min(parent.width - Theme.spacingLg * 2, 520)
        spacing: Theme.spacingLg
        visible: root.page === 1

        ThemedLabel {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            size: "base"
            tone: "default"
            text: qsTr("Upscale this video?")
        }

        // The grade on the left is the shorter side, the number the upscale suggestion is
        // judged by; orange while it is under the threshold, green once it clears it.
        Item {
            width: parent.width
            height: resolutionPill.height

            MediaPreviewResolutionPill {
                id: resolutionPill
                anchors.horizontalCenter: parent.horizontalCenter
                suggestUpscale: root.suggestUpscale
                shortSide: Math.min(root.outputWidth, root.outputHeight)
                outputWidth: root.outputWidth
                outputHeight: root.outputHeight
            }
        }

        ThemedLabel {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("Length %1").arg(root.formatTime(root.outSeconds - root.inSeconds))
        }

        ThemedLabel {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            size: "sm"
            tone: "default"
            text: root.suggestUpscale
                  ? qsTr("This video is under 700 pixels on its shorter side. Upscaling it with an AI model can make it look sharper.")
                  : qsTr("This resolution is already good for most projects. You can still upscale it.")
        }

        ThemedLabel {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: qsTr("Done keeps the original video and stores this range and crop. Upscale renders them as a new video in the media bin, then opens it in the Enhance window.")
        }
    }

    Row {
        id: footer
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingMd

        ThemedLabel {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - cancelBtn.width - saveBtn.width - parent.spacing * 2
                   - (backBtn.visible ? backBtn.width + parent.spacing : 0)
                   - (upscaleBtn.visible ? upscaleBtn.width + parent.spacing : 0)
            elide: Text.ElideRight
            tone: "muted"
            text: root.saving
                  ? (EditorState.bin.editStatus.length > 0
                     ? EditorState.bin.editStatus
                     : qsTr("Saving…"))
                  : root.page === 1 ? ""
                  : root.hasFinishPage ? qsTr("Choose the part and crop to keep, then Next.")
                  : root.dirty
                    ? (root.canCrop ? qsTr("Save keeps the original file and stores this crop.") : qsTr("Save keeps the original file and stores this trim."))
                    : (root.clipId.length > 0 ? qsTr("Adjust the crop or Reset to restore the full picture.") : qsTr("Nothing to save — drag this item onto the timeline when you are ready."))
        }

        ThemedButton {
            id: cancelBtn
            variant: "ghost"
            text: root.saving ? qsTr("Cancel") : qsTr("Close")
            onClicked: {
                if (root.saving)
                    EditorState.bin.cancelEdit()
                else
                    root.closeRequested()
            }
        }

        ThemedButton {
            id: backBtn
            visible: root.page === 1
            variant: "ghost"
            text: qsTr("Back")
            enabled: !root.saving
            onClicked: root.page = 0
        }

        ThemedButton {
            id: upscaleBtn
            visible: root.page === 1
            variant: root.suggestUpscale ? "primary" : "secondary"
            text: qsTr("Upscale…")
            enabled: !root.saving
            onClicked: root.upscale()
        }

        ThemedButton {
            id: saveBtn
            variant: root.hasFinishPage && root.page === 1 && root.suggestUpscale ? "secondary" : "primary"
            text: !root.hasFinishPage ? qsTr("Save") : root.page === 0 ? qsTr("Next") : qsTr("Done")
            enabled: !root.saving && (root.hasFinishPage || (root.dirty && (root.assetIndex >= 0 || root.clipId.length > 0)))
            onClicked: {
                EditorState.assetPreview.pause()
                if (root.hasFinishPage && root.page === 0) {
                    root.page = 1
                    return
                }
                if (!root.dirty) {
                    root.closeRequested()
                    return
                }
                if (root.clipId.length > 0) {
                    if (EditorState.setClipSourceFrame(root.clipId, root.cropX, root.cropY, root.cropW, root.cropH,
                                                       root.cropAngle))
                        root.closeRequested()
                    return
                }
                if (AssetLibrary.assetAt(root.assetIndex).id !== root.assetId)
                    return
                EditorState.bin.saveEdit(root.assetIndex, root.inSeconds,
                                          root.canTrim ? root.outSeconds : -1,
                                          root.cropX, root.cropY, root.cropW, root.cropH)
            }
        }
    }

    MediaPreviewSavingScrim {
        visible: root.saving
        status: EditorState.bin.editStatus.length > 0
                ? EditorState.bin.editStatus : qsTr("Saving…")
        progress: EditorState.bin.editProgress
    }
}
