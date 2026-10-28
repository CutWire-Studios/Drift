import QtQuick
import QtQuick.Effects
import Drift

// Click-to-pick object cutout on the target clip's frame. The prompt points and the live matte
// come from EditorState.segmentation, which encodes the frame under the playhead; propagation
// runs from the Masking panel.
Item {
    id: root

    property bool interacting: false

    // Snapshot from PreviewController::maskingFrame(): the selection's frame, and whether it is under the
    // playhead. Nothing is placed or accepted while it is not.
    property var editorState: ({})
    readonly property bool hasFrame: !!editorState.hasFrame
    // The selection's mask target (MaskingController::maskTarget). Assigned, not bound, so it
    // refreshes on the same signals as editorState.
    property var target: ({ kind: "none" })
    readonly property bool aiActive: target.kind === "clip"

    // The clip the segmentation session is open on, -1 when none. Kept here so that closing or
    // retargeting always ends the session that was actually begun.
    property int sessionTrack: -1
    property int sessionClip: -1

    property int hoverIndex: -1

    readonly property real canvasW: Math.max(1, editorState.canvasWidth || 1)
    readonly property real canvasH: Math.max(1, editorState.canvasHeight || 1)
    readonly property real sx: root.width / canvasW
    readonly property real sy: root.height / canvasH

    function refresh() {
        // Hidden: onVisibleChanged catches up when shown.
        if (!visible || interacting)
            return
        // Playhead ticks ~60 Hz; the frame and target are only needed when a click lands.
        if (EditorState.playing)
            return
        editorState = EditorState.preview.maskingFrame()
        target = EditorState.masking.maskTarget()
        syncSession()
    }

    function syncSession() {
        const wantTrack = visible && aiActive ? target.track : -1
        const wantClip = wantTrack >= 0 ? target.clip : -1
        if (wantTrack === sessionTrack && wantClip === sessionClip)
            return
        if (sessionTrack >= 0)
            EditorState.segmentation.endSession()
        sessionTrack = wantTrack
        sessionClip = wantClip
        if (sessionTrack >= 0)
            EditorState.segmentation.beginSessionAtPlayhead(sessionTrack, sessionClip)
    }

    // Scrubbing and playback each pay a multi-second encode per frame, so the session only follows
    // the playhead once it has rested for a moment.
    function scheduleFrame() {
        if (!EditorState.playing && !EditorState.scrubbing)
            frameTimer.restart()
    }

    // Index of the prompt point within 8 px of (x, y) in clip-frame pixels, nearest first, or -1.
    function pointAt(x, y) {
        const pts = EditorState.segmentation.sessionPoints
        const fw = Math.max(1, clipFrame.width)
        const fh = Math.max(1, clipFrame.height)
        let best = -1
        let bestDistance = 8
        for (let i = 0; i < pts.length; ++i) {
            const d = Math.hypot(pts[i].x * fw - x, pts[i].y * fh - y)
            if (d <= bestDistance) {
                best = i
                bestDistance = d
            }
        }
        return best
    }

    onVisibleChanged: {
        if (visible)
            refresh()
        syncSession()
    }
    Component.onCompleted: refresh()
    Component.onDestruction: {
        if (sessionTrack >= 0)
            EditorState.segmentation.endSession()
        sessionTrack = -1
        sessionClip = -1
    }

    Timer {
        id: frameTimer
        interval: 250
        repeat: false
        onTriggered: {
            if (root.sessionTrack >= 0 && !EditorState.playing && !EditorState.scrubbing)
                EditorState.segmentation.setFrameSeconds(EditorState.playheadSeconds)
        }
    }

    Connections {
        target: EditorState
        function onTracksChanged() { root.refresh() }
        function onClipPropertiesPreviewed(trackIndex, clipIndex, keys) {
            for (const k of keys) {
                if (k === "x" || k === "y" || k === "width" || k === "height" || k === "rotation"
                        || k === "rotationX" || k === "rotationY" || k === "z" || k === "perspective") {
                    root.refresh()
                    return
                }
            }
        }
        function onSelectionChanged() { root.refresh() }
        function onSelectedClipDataChanged() { root.refresh() }
        // A scrub moves the playhead per scroll event; catch up once when it ends.
        function onPlayheadSecondsChanged() {
            if (!EditorState.scrubbing)
                root.refresh()
            root.scheduleFrame()
        }
        function onScrubbingChanged() {
            if (!EditorState.scrubbing)
                root.refresh()
            root.scheduleFrame()
        }
        function onPlayingChanged() {
            if (!EditorState.playing) {
                root.refresh()
                root.scheduleFrame()
            }
        }
    }

    // The clip's destination rect on the canvas, rotated with it, as MaskOverlay places it.
    Item {
        id: clipFrame
        visible: root.hasFrame
        x: (root.editorState.x || 0) * root.sx
        y: (root.editorState.y || 0) * root.sy
        width: Math.max(1, (root.editorState.width || root.canvasW) * root.sx)
        height: Math.max(1, (root.editorState.height || root.canvasH) * root.sy)
        transformOrigin: Item.Center
        // A tilted clip frame goes through the clip's perspective instead of the flat spin.
        readonly property bool is3d: (root.editorState.rotationX || 0) !== 0
                                     || (root.editorState.rotationY || 0) !== 0
                                     || (root.editorState.z || 0) !== 0
        // Moved by a transform layer: composed through that parent as well, flat or not.
        readonly property bool hasParent: root.editorState.parentActive === true
        rotation: is3d || hasParent ? 0 : (root.editorState.rotation || 0)
        transform: Matrix4x4 {
            matrix: clipFrame.is3d || clipFrame.hasParent
                    ? EditorState.preview.clipPoseMatrix(root.editorState, root.editorState.x || 0,
                                                        root.editorState.y || 0,
                                                        root.editorState.width || root.canvasW,
                                                        root.editorState.height || root.canvasH,
                                                        root.editorState.rotation || 0,
                                                        root.sx, root.sy)
                    : Qt.matrix4x4()
        }

        MaskOverlayClipBounds { }

        // The "maskAlpha" variant carries the coverage in alpha over white, so colorizing it tints
        // only the object and leaves the rest of the frame untouched.
        Image {
            id: liveMask
            anchors.fill: parent
            source: EditorState.segmentation.previewMaskUrl.replace("/mask?", "/maskAlpha?")
            cache: false
            fillMode: Image.Stretch
            visible: false
            layer.enabled: true
        }

        MultiEffect {
            anchors.fill: parent
            source: liveMask
            visible: root.aiActive && EditorState.segmentation.previewMaskUrl !== ""
            colorization: 1.0
            colorizationColor: Theme.c.maskAiOverlay.tint
            opacity: 0.45
        }

        // Clicks on the frame: left adds an include point; right, Ctrl or Alt adds an exclude point.
        // A click within 8 px of an existing point removes that point instead.
        MouseArea {
            anchors.fill: parent
            enabled: root.aiActive && !EditorState.segmentation.running
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            cursorShape: Qt.CrossCursor
            onWheel: (wheel) => { wheel.accepted = false }

            onPositionChanged: (mouse) => {
                root.hoverIndex = root.pointAt(mouse.x, mouse.y)
            }
            onExited: root.hoverIndex = -1
            onClicked: (mouse) => {
                const hit = root.pointAt(mouse.x, mouse.y)
                if (hit >= 0) {
                    EditorState.segmentation.removePoint(hit)
                    root.hoverIndex = -1
                    return
                }
                const exclude = mouse.button === Qt.RightButton
                                || (mouse.modifiers & (Qt.ControlModifier | Qt.AltModifier)) !== 0
                EditorState.segmentation.addPoint(mouse.x / Math.max(1, clipFrame.width),
                                                  mouse.y / Math.max(1, clipFrame.height),
                                                  !exclude)
            }
        }

        Repeater {
            model: EditorState.segmentation.sessionPoints

            delegate: Rectangle {
                id: pointDot
                required property var modelData
                required property int index

                width: 12
                height: 12
                radius: width / 2
                x: modelData.x * clipFrame.width - width / 2
                y: modelData.y * clipFrame.height - height / 2
                scale: index === root.hoverIndex ? 1.35 : 1
                color: modelData.include ? Theme.c.maskAiOverlay.include : Theme.c.maskAiOverlay.exclude
                border.width: Theme.c.maskDrawOverlay.borderWidth
                border.color: Theme.c.maskAiOverlay.pointBorder

                Text {
                    anchors.centerIn: parent
                    text: pointDot.modelData.include ? "+" : "−"
                    color: Theme.c.maskAiOverlay.pointBorder
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    font.bold: true
                }
            }
        }

        // Status over the frame while a frame encodes, a mask infers, or the clip is tracked.
        Rectangle {
            id: busyBadge
            visible: root.aiActive && (EditorState.segmentation.running || EditorState.segmentation.previewBusy)
            x: Theme.spacingSm
            y: Theme.spacingSm
            width: busyRow.width + Theme.spacingMd * 2
            height: busyRow.height + Theme.spacingSm * 2
            radius: Theme.c.maskDrawOverlay.radius
            color: Theme.c.maskAiOverlay.badgeBackground

            Row {
                id: busyRow
                anchors.centerIn: parent
                spacing: Theme.spacingSm

                IconGlyph {
                    glyph: Theme.icons.spinner
                    iconSize: Theme.iconSizeSm
                    iconColor: Theme.c.maskAiOverlay.badgeForeground

                    RotationAnimator on rotation {
                        running: busyBadge.visible
                        loops: Animation.Infinite
                        from: 0
                        to: 360
                        duration: 900
                    }
                }

                Text {
                    text: EditorState.segmentation.running
                          ? qsTr("Tracking object… %1%").arg(Math.round(EditorState.segmentation.progress * 100))
                          : qsTr("Analyzing frame…")
                    color: Theme.c.maskAiOverlay.badgeForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
            }
        }
    }

    // Nothing to pick on: the tool is up, but the selection is not a clip, or it is off the playhead.
    Rectangle {
        visible: root.target.kind !== "clip" || !root.editorState.inside
        anchors.centerIn: parent
        width: noClipText.width + Theme.spacingLg * 2
        height: noClipText.height + Theme.spacingMd * 2
        radius: Theme.c.maskDrawOverlay.radius
        color: Theme.c.maskDrawOverlay.hintBackground

        Text {
            id: noClipText
            anchors.centerIn: parent
            text: root.target.kind !== "clip"
                  ? qsTr("Select a clip on the timeline to pick an object")
                  : qsTr("Move the playhead over the selected clip")
            color: Theme.c.maskDrawOverlay.hintForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }

    Rectangle {
        visible: root.aiActive && root.editorState.inside && !EditorState.segmentation.running
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spacingLg
        width: hintText.width + Theme.spacingLg * 2
        height: hintText.height + Theme.spacingMd * 2
        radius: Theme.c.maskDrawOverlay.radius
        color: Theme.c.maskDrawOverlay.hintBackground

        Text {
            id: hintText
            anchors.centerIn: parent
            text: EditorState.segmentation.sessionPoints.length === 0
                  ? qsTr("Click the object to select it · Right-click or Ctrl-click to exclude")
                  : qsTr("Add points to refine · then press “Cut out through whole clip” in the Masking panel")
            color: Theme.c.maskDrawOverlay.hintForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }
}
