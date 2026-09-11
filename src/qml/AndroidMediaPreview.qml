import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
import "components"

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
    readonly property bool canTrim: !isImage && durationSeconds > 0.05
    readonly property bool canPlay: !isImage

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
    readonly property bool trimDirty: canTrim
                                      && (Math.abs(inSeconds - persistedInSeconds) > 0.02
                                          || Math.abs(outSeconds - persistedOutSeconds) > 0.02)
    readonly property bool dirty: cropDirty || trimDirty
    readonly property bool saving: EditorState.editingAsset
    readonly property real position: EditorState.assetPreviewPosition

    function openFor(index) {
        const asset = AssetLibrary.assetAt(index)
        if (!asset || Object.keys(asset).length === 0) {
            root.closed()
            return
        }
        EditorState.assetPreviewWindowOpen = true
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
        EditorState.beginAssetPreview(index)
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
        const wasPlaying = EditorState.assetPreviewPlaying
        const at = root.position
        root.rotationOverride = nextOverride
        root.effectiveRotation = next
        EditorState.beginAssetPreview(root.assetIndex)
        root.seekTo(at)
        if (wasPlaying)
            EditorState.playAssetPreview()
    }

    function close() {
        EditorState.pauseAssetPreview()
        EditorState.endAssetPreview()
        root.assetIndex = -1
        EditorState.assetPreviewWindowOpen = false
        root.closed()
    }

    function resetEdits() {
        root.inSeconds = root.persistedInSeconds
        root.outSeconds = root.persistedOutSeconds
        root.cropX = 0
        root.cropY = 0
        root.cropW = 1
        root.cropH = 1
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
        EditorState.seekAssetPreview(Math.max(0, seconds))
    }

    function togglePlay() {
        if (!root.canPlay)
            return
        if (EditorState.assetPreviewPlaying) {
            EditorState.pauseAssetPreview()
            return
        }
        const at = root.position
        if (at < root.inSeconds - 0.02 || at >= root.outSeconds - 0.02)
            seekTo(root.inSeconds)
        EditorState.playAssetPreview()
    }

    // Playback stays inside the kept range, so what plays is what a save would keep.
    onPositionChanged: {
        if (!EditorState.assetPreviewPlaying || root.isImage)
            return
        if (root.position >= root.outSeconds - 0.01)
            seekTo(root.inSeconds)
    }

    Connections {
        target: EditorState
        function onAssetEditFinished(ok, message) {
            if (ok)
                root.close()
        }
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
        color: Theme.appBackground
    }

    readonly property real safeTop: SafeArea.margins.top
    readonly property real safeBottom: SafeArea.margins.bottom
    readonly property real safeLeft: SafeArea.margins.left
    readonly property real safeRight: SafeArea.margins.right

    // ----- Header ---------------------------------------------------------------------------
    Item {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: root.safeLeft
        anchors.rightMargin: root.safeRight
        anchors.topMargin: root.safeTop
        height: Theme.androidTopBarHeight

        Rectangle {
            anchors.fill: parent
            color: Theme.panelBackground
        }

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.panelBorder
        }

        IconButton {
            id: backButton
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingSm
            anchors.verticalCenter: parent.verticalCenter
            buttonSize: Theme.androidIconButtonSize
            glyph: Theme.icons.chevronLeft
            tooltip: qsTr("Back")
            enabled: !root.saving
            onClicked: root.close()
        }

        ThemedLabel {
            anchors.left: backButton.right
            anchors.right: saveButton.left
            anchors.leftMargin: Theme.spacingSm
            anchors.rightMargin: Theme.spacingSm
            anchors.verticalCenter: parent.verticalCenter
            text: root.assetName
            // ThemedLabel wraps by default, and a bin name is long enough to take the header
            // to two lines and spill out of it.
            wrapMode: Text.NoWrap
            elide: Text.ElideMiddle
            size: "sm"
            tone: "default"
        }

        ThemedButton {
            id: saveButton
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingMd
            anchors.verticalCenter: parent.verticalCenter
            variant: "primary"
            text: qsTr("Save")
            enabled: root.dirty && !root.saving && root.assetIndex >= 0
            onClicked: {
                EditorState.pauseAssetPreview()
                EditorState.saveAssetEdit(root.assetIndex, root.inSeconds,
                                          root.canTrim ? root.outSeconds : -1,
                                          root.cropX, root.cropY, root.cropW, root.cropH)
            }
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
        radius: Theme.radiusMd
        color: Theme.overlayColor
        clip: true

        readonly property real aspect: {
            if (root.displayW > 0 && root.displayH > 0)
                return root.displayW / root.displayH
            if (EditorState.assetPreviewFrameSize.height > 0)
                return EditorState.assetPreviewFrameSize.width
                       / EditorState.assetPreviewFrameSize.height
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
                if (!EditorState.assetPreviewActive)
                    return ""
                return "image://clippreview/frame?rev=" + EditorState.assetPreviewRevision
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
                iconColor: Theme.mutedForeground
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
        Item {
            id: cropHost
            visible: root.canCrop && root.mode === "crop"
            x: stage.fit.x
            y: stage.fit.y
            width: stage.fit.w
            height: stage.fit.h

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

            Rectangle { width: cropHost.frameX; height: parent.height; color: Theme.scrimStrong }
            Rectangle {
                x: cropHost.frameX + cropHost.frameW
                width: Math.max(0, parent.width - x)
                height: parent.height
                color: Theme.scrimStrong
            }
            Rectangle {
                x: cropHost.frameX
                width: cropHost.frameW
                height: cropHost.frameY
                color: Theme.scrimStrong
            }
            Rectangle {
                x: cropHost.frameX
                y: cropHost.frameY + cropHost.frameH
                width: cropHost.frameW
                height: Math.max(0, parent.height - y)
                color: Theme.scrimStrong
            }

            Rectangle {
                x: cropHost.frameX
                y: cropHost.frameY
                width: cropHost.frameW
                height: cropHost.frameH
                color: "transparent"
                border.width: Theme.borderWidthFocus
                border.color: Theme.primary
            }

            MouseArea {
                x: cropHost.frameX
                y: cropHost.frameY
                width: cropHost.frameW
                height: cropHost.frameH
                enabled: !root.saving
                property real grabX: 0
                property real grabY: 0
                onPressed: (mouse) => {
                    grabX = mouse.x
                    grabY = mouse.y
                    Haptics.pickUp()
                }
                onReleased: Haptics.drop()
                onCanceled: Haptics.drop()
                onPositionChanged: (mouse) => {
                    if (!pressed || cropHost.width <= 0)
                        return
                    const dx = (mouse.x - grabX) / cropHost.width
                    const dy = (mouse.y - grabY) / cropHost.height
                    cropHost.setCrop(root.cropX + dx, root.cropY + dy, root.cropW, root.cropH)
                }
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
                Rectangle {
                    required property var modelData
                    // Fingers, not a mouse pointer: the desktop grip is 10px.
                    readonly property int grip: 20
                    width: grip
                    height: grip
                    radius: Theme.radiusXs
                    color: Theme.primary
                    x: cropHost.frameX + modelData.x * cropHost.frameW - width / 2
                    y: cropHost.frameY + modelData.y * cropHost.frameH - height / 2
                    z: 2

                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -14
                        enabled: !root.saving
                        preventStealing: true
                        property real startX: 0
                        property real startY: 0
                        property real startW: 1
                        property real startH: 1
                        property real origX: 0
                        property real origY: 0
                        onPressed: {
                            startX = root.cropX
                            startY = root.cropY
                            startW = root.cropW
                            startH = root.cropH
                            origX = mouseX
                            origY = mouseY
                            Haptics.pickUp()
                        }
                        onPositionChanged: {
                            if (!pressed || cropHost.width <= 0)
                                return
                            const dx = (mouseX - origX) / cropHost.width
                            const dy = (mouseY - origY) / cropHost.height
                            let nx = startX
                            let ny = startY
                            let nw = startW
                            let nh = startH
                            if (modelData.dx < 0) {
                                nx = startX + dx
                                nw = startW - dx
                            } else if (modelData.dx > 0) {
                                nw = startW + dx
                            }
                            if (modelData.dy < 0) {
                                ny = startY + dy
                                nh = startH - dy
                            } else if (modelData.dy > 0) {
                                nh = startH + dy
                            }
                            cropHost.setCrop(nx, ny, nw, nh)
                        }
                        onReleased: Haptics.drop()
                        onCanceled: Haptics.drop()
                    }
                }
            }
        }

        // Play/pause over the picture, where a phone player puts it.
        Rectangle {
            visible: root.canPlay && root.mode !== "crop"
            anchors.centerIn: parent
            width: 64
            height: 64
            radius: width / 2
            color: Theme.scrimStrong
            opacity: EditorState.assetPreviewPlaying ? 0 : 1

            Behavior on opacity {
                NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
            }

            IconGlyph {
                anchors.centerIn: parent
                glyph: Theme.icons.play
                iconSize: Theme.iconSizeLg
                iconColor: Theme.onMedia
            }
        }

        MouseArea {
            anchors.fill: parent
            enabled: root.canPlay && root.mode !== "crop" && !root.saving
            onClicked: {
                Haptics.confirm()
                root.togglePlay()
            }
        }

        Rectangle {
            visible: root.mode === "crop"
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.margins: Theme.spacingMd
            color: Theme.scrimStrong
            radius: Theme.radiusSm
            width: cropSizeLabel.implicitWidth + Theme.spacingLg
            height: cropSizeLabel.implicitHeight + Theme.spacingSm
            Text {
                id: cropSizeLabel
                anchors.centerIn: parent
                color: Theme.onMedia
                font.family: Theme.monoFontFamily
                font.pixelSize: Theme.fontSizeXs
                text: {
                    const w = Math.max(1, Math.round(root.displayW * root.cropW))
                    const h = Math.max(1, Math.round(root.displayH * root.cropH))
                    return w + "×" + h
                }
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
                    EditorState.pauseAssetPreview()
                    root.mode = "crop"
                }
            }
        }

        ThemedButton {
            width: parent.width
            height: Theme.controlHeight
            visible: root.isVideo
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
                radius: Theme.radiusSm
                color: Theme.panelBackground
                border.width: Theme.borderWidth
                border.color: Theme.panelBorder
                clip: true

                readonly property real dur: Math.max(0.001, root.durationSeconds)
                readonly property real inX: (root.inSeconds / dur) * width
                readonly property real outX: (root.outSeconds / dur) * width
                readonly property real playX: (root.position / dur) * width

                ClipFilmstrip {
                    anchors.fill: parent
                    anchors.margins: Theme.borderWidth
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
                    color: Theme.scrimStrong
                }
                Rectangle {
                    x: strip.outX
                    width: Math.max(0, parent.width - x)
                    height: parent.height
                    color: Theme.scrimStrong
                }
                Rectangle {
                    x: strip.inX
                    width: Math.max(2, strip.outX - strip.inX)
                    height: parent.height
                    color: "transparent"
                    border.width: Theme.borderWidthFocus
                    border.color: Theme.primary
                }
                Rectangle {
                    x: strip.playX - 1
                    width: 2
                    height: parent.height
                    color: Theme.onMedia
                }

                MouseArea {
                    anchors.fill: parent
                    anchors.leftMargin: 22
                    anchors.rightMargin: 22
                    enabled: !root.saving
                    onPressed: (mouse) => {
                        EditorState.pauseAssetPreview()
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
                    Rectangle {
                        required property var modelData
                        readonly property bool isIn: modelData.edge === "in"
                        width: 22
                        height: parent.height
                        x: (isIn ? strip.inX : strip.outX - width)
                        color: Theme.primary
                        radius: Theme.radiusXs
                        z: 3

                        Column {
                            anchors.centerIn: parent
                            spacing: 3
                            Repeater {
                                model: 3
                                Rectangle {
                                    width: 10
                                    height: 2
                                    radius: 1
                                    color: Theme.primaryForeground
                                    opacity: 0.9
                                }
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -12
                            enabled: !root.saving
                            preventStealing: true
                            onPressed: {
                                EditorState.pauseAssetPreview()
                                Haptics.pickUp()
                            }
                            onReleased: Haptics.drop()
                            onCanceled: Haptics.drop()
                            onPositionChanged: (mouse) => {
                                if (!pressed)
                                    return
                                const t = ((parent.x + mouse.x + (parent.isIn ? 0 : parent.width))
                                           / Math.max(1, strip.width)) * root.durationSeconds
                                if (parent.isIn)
                                    root.inSeconds = t
                                else
                                    root.outSeconds = t
                                root.clampRange()
                                root.seekTo(parent.isIn ? root.inSeconds : root.outSeconds)
                            }
                        }
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
                  ? (EditorState.assetEditStatus.length > 0
                     ? EditorState.assetEditStatus : qsTr("Saving…"))
                  : root.dirty
                    ? qsTr("Save keeps your changes as a new file in this project.")
                    : qsTr("Nothing changed yet. Trim or crop above, or go back and drag this onto the timeline.")
        }
    }

    // ----- Saving ---------------------------------------------------------------------------
    Rectangle {
        visible: root.saving
        anchors.fill: parent
        color: Theme.scrimColor
        z: 10

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
        }

        Column {
            anchors.centerIn: parent
            spacing: Theme.spacingLg
            width: Math.min(parent.width - Theme.spacing3xl * 2, 320)

            ThemedLabel {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: EditorState.assetEditStatus.length > 0
                      ? EditorState.assetEditStatus : qsTr("Saving…")
                size: "sm"
            }
            ThemedProgressBar {
                width: parent.width
                value: EditorState.assetEditProgress
            }
            ThemedButton {
                anchors.horizontalCenter: parent.horizontalCenter
                variant: "ghost"
                text: qsTr("Cancel")
                onClicked: EditorState.cancelAssetEdit()
            }
        }
    }
}
