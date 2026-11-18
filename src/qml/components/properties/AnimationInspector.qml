import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
// Per-edge In / Out: a fade (opacity and volume, any clip) and a motion animation (visual clips).
Item {
    id: root

    property int clipDataRevision: 0
    readonly property var clipData: {
        void clipDataRevision
        return EditorState.selectedClipData
    }
    readonly property bool hasSelection: !!clipData && Object.keys(clipData).length > 0
    readonly property string clipKind: hasSelection ? (clipData.kind || "") : ""
    readonly property bool supportsBodyAnim: clipKind === "video" || clipKind === "image"
                                             || clipKind === "shape" || clipKind === "text"
    readonly property bool supportsFade: supportsBodyAnim || clipKind === "audio"
    readonly property real clipDuration: hasSelection ? (clipData.duration || 0) : 0
    readonly property real fadeIn: hasSelection ? (clipData.fadeIn || 0) : 0
    readonly property real fadeOut: hasSelection ? (clipData.fadeOut || 0) : 0
    readonly property real maxFade: Math.max(0.05, Math.min(10, clipDuration))

    readonly property var animKindList: [{ id: "none", label: qsTr("None") }]
                                        .concat(EditorState.clipAnimationKinds())
    readonly property var animIn: {
        void clipDataRevision
        return (clipData && clipData.animIn)
               ? clipData.animIn
               : { kind: "none", duration: 0.5, curve: "smooth" }
    }
    readonly property var animOut: {
        void clipDataRevision
        return (clipData && clipData.animOut)
               ? clipData.animOut
               : { kind: "none", duration: 0.5, curve: "smooth" }
    }

    height: column.height
    implicitHeight: column.height

    function applyBoth(kind) {
        EditorState.applyClipAnimationPreset(EditorState.selectedTrack, EditorState.selectedClip,
                                             "both", kind)
        root.previewAnimation("animIn", kind, 0.5)
    }

    property bool previewActive: false
    property double previewSavedPlayhead: 0
    property double previewLastPlayhead: 0

    function previewAnimation(which, kind, duration) {
        if (kind === "none" || !root.hasSelection)
            return
        const clipLength = root.clipData.duration || 0
        const length = Math.min(duration, clipLength)
        if (length <= 0)
            return
        if (!root.previewActive)
            root.previewSavedPlayhead = EditorState.playheadSeconds
        root.previewActive = false
        EditorState.playing = false
        EditorState.playheadSeconds = which === "animIn"
                ? root.clipData.start
                : root.clipData.start + clipLength - length
        root.previewLastPlayhead = EditorState.playheadSeconds
        root.previewActive = true
        EditorState.playing = true
        previewTimer.interval = Math.round(length * 1000)
        previewTimer.restart()
    }

    function endPreview() {
        root.previewActive = false
        EditorState.playing = false
        EditorState.playheadSeconds = root.previewSavedPlayhead
    }

    function cancelPreview() {
        root.previewActive = false
        previewTimer.stop()
    }

    Timer {
        id: previewTimer
        repeat: false
        onTriggered: root.endPreview()
    }

    Connections {
        target: EditorState
        function onSelectionChanged() { root.clipDataRevision++ }
        function onSelectedClipDataChanged() { root.clipDataRevision++ }
        function onTracksChanged() { root.clipDataRevision++ }
        // Playback ticks move the playhead forward in small steps; a seek jumps it.
        function onPlayheadSecondsChanged() {
            if (!root.previewActive)
                return
            const now = EditorState.playheadSeconds
            if (now < root.previewLastPlayhead || now > root.previewLastPlayhead + 0.5)
                root.cancelPreview()
            else
                root.previewLastPlayhead = now
        }
        function onPlayingChanged() {
            if (root.previewActive && !EditorState.playing)
                root.cancelPreview()
        }
    }

    function setFades(fadeInSeconds, fadeOutSeconds, preview) {
        const inClamped = Math.max(0, Math.min(fadeInSeconds, root.clipDuration - fadeOutSeconds))
        const outClamped = Math.max(0, Math.min(fadeOutSeconds, root.clipDuration - inClamped))
        if (preview)
            EditorState.previewSetClipFade(EditorState.selectedTrack, EditorState.selectedClip,
                                           inClamped, outClamped)
        else
            EditorState.setClipFade(EditorState.selectedTrack, EditorState.selectedClip,
                                    inClamped, outClamped)
    }

    component EdgeSection: Column {
        id: sec

        required property Item inspector
        required property string edge

        readonly property bool isIn: sec.edge === "in"
        readonly property string animKey: sec.isIn ? "animIn" : "animOut"
        readonly property var anim: sec.isIn ? sec.inspector.animIn : sec.inspector.animOut
        readonly property real fadeSec: sec.isIn ? sec.inspector.fadeIn : sec.inspector.fadeOut
        readonly property string fadeCurve: (sec.isIn ? sec.inspector.clipData.fadeInCurve
                                                      : sec.inspector.clipData.fadeOutCurve) || "smooth"
        readonly property var fadeHandles: (sec.isIn ? sec.inspector.clipData.fadeInHandles
                                                      : sec.inspector.clipData.fadeOutHandles) || []
        readonly property string animKind: sec.anim.kind || "none"
        readonly property var kindModel: {
            const dir = sec.edge
            const base = sec.inspector.animKindList.filter(e => !e.directions || e.directions.indexOf(dir) >= 0)
            return base.some(e => e.id === sec.animKind)
                   ? base : base.concat([{ id: sec.animKind, label: sec.animKind }])
        }
        readonly property real curveWidth: Math.max(40, sec.width * 0.3)

        width: parent ? parent.width : 0
        spacing: Theme.spacingSm

        function setFade(seconds, preview) {
            if (sec.isIn)
                sec.inspector.setFades(seconds, sec.inspector.fadeOut, preview)
            else
                sec.inspector.setFades(sec.inspector.fadeIn, seconds, preview)
        }

        Text {
            text: qsTr("Fade")
            color: Theme.c.animationInspector.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        Row {
            width: parent.width
            spacing: 6

            ThemedSlider {
                id: fadeSlider
                lockWhilePlaying: true
                label: sec.isIn ? qsTr("Fade in") : qsTr("Fade out")
                width: Math.max(40, parent.width - fadeField.width - sec.curveWidth - parent.spacing * 2)
                anchors.verticalCenter: parent.verticalCenter
                from: 0
                to: sec.inspector.maxFade
                stepSize: 0.05
                valueFormatter: function (v) { return Number(v).toFixed(2) + " s" }
                Binding on value {
                    when: !fadeSlider.pressed
                    value: sec.fadeSec
                }
                onMoved: sec.setFade(value, true)
                onPressedChanged: {
                    if (pressed)
                        EditorState.beginPreviewDrag(sec.isIn ? qsTr("Fade in") : qsTr("Fade out"))
                    else
                        EditorState.commitPreviewDrag()
                }
            }
            ThemedNumberField {
                id: fadeField
                width: 56
                anchors.verticalCenter: parent.verticalCenter
                from: 0
                to: sec.inspector.maxFade
                unit: "s"
                decimals: 2
                step: 0.05
                Binding on value {
                    when: !fadeField.activeFocus
                    value: sec.fadeSec
                }
                onEdited: v => {
                    sec.setFade(v, false)
                    sec.inspector.previewAnimation(sec.animKey, "fade", v)
                }
            }
            CurveField {
                width: sec.curveWidth
                anchors.verticalCenter: parent.verticalCenter
                domain: "fade"
                curve: sec.fadeCurve
                handles: sec.fadeHandles
                onCurveChosen: (curve, handles) => {
                    EditorState.setClipFadeCurve(EditorState.selectedTrack, EditorState.selectedClip,
                                                 sec.edge, curve, handles)
                    if (curve === "custom")
                        ShellBridge.main.openFadeCurve(EditorState.selectedTrack,
                                                       EditorState.selectedClip, sec.edge, "fade")
                }
            }
        }

        RowLayout {
            visible: sec.inspector.supportsBodyAnim
            width: parent.width
            spacing: Theme.spacingXs

            Text {
                Layout.fillWidth: true
                text: qsTr("Animation")
                color: Theme.c.animationInspector.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
            IconButton {
                visible: !ShellBridge.phoneShell && Forge.canEdit("animation", sec.animKind)
                glyph: Theme.icons.pencil
                variant: "ghost"
                tooltip: qsTr("Edit this animation in Forge")
                onClicked: Forge.openPackage("animation", sec.animKind)
            }
            IconButton {
                visible: !ShellBridge.phoneShell
                glyph: Theme.icons.sparkles
                variant: "ghost"
                tooltip: qsTr("Create a new animation in Forge")
                onClicked: Forge.createNew("animation")
            }
        }

        Row {
            visible: sec.inspector.supportsBodyAnim
            width: parent.width
            spacing: 6

            ThemedComboBox {
                width: Math.max(40, (parent.width - parent.spacing * 2 - 56) / 2)
                textRole: "label"
                model: sec.kindModel
                currentIndex: Math.max(0, sec.kindModel.findIndex(e => e.id === sec.animKind))
                onActivated: {
                    const kind = sec.kindModel[currentIndex].id
                    EditorState.setClipAnimation(EditorState.selectedTrack, EditorState.selectedClip,
                                                 sec.animKey, { kind: kind })
                    sec.inspector.previewAnimation(sec.animKey, kind, sec.anim.duration || 0.5)
                }
            }
            CurveField {
                width: Math.max(40, (parent.width - parent.spacing * 2 - 56) / 2)
                anchors.verticalCenter: parent.verticalCenter
                enabled: sec.animKind !== "none"
                opacity: enabled ? 1 : 0.5
                domain: "fade"
                curve: sec.anim.curve || "smooth"
                handles: sec.anim.handles || []
                onCurveChosen: (curve, handles) => {
                    const patch = { curve: curve }
                    if (handles.length === 4)
                        patch.handles = handles
                    EditorState.setClipAnimation(EditorState.selectedTrack, EditorState.selectedClip,
                                                 sec.animKey, patch)
                    if (curve === "custom")
                        ShellBridge.main.openFadeCurve(EditorState.selectedTrack,
                                                       EditorState.selectedClip, sec.edge, "animation")
                }
            }
            ThemedNumberField {
                id: animDurationField
                width: 56
                enabled: sec.animKind !== "none"
                opacity: enabled ? 1 : 0.5
                from: 0
                to: 10
                unit: "s"
                decimals: 2
                step: 0.05
                Binding on value {
                    when: !animDurationField.activeFocus
                    value: sec.anim.duration || 0.5
                }
                onEdited: v => EditorState.setClipAnimation(EditorState.selectedTrack,
                                                            EditorState.selectedClip, sec.animKey,
                                                            { duration: v })
            }
        }
    }

    Column {
        id: column
        width: root.width
        spacing: Theme.spacingXl

        EmptyState {
            visible: !root.supportsFade
            width: parent.width
            compact: true
            glyph: Theme.icons.sparkles
            title: qsTr("Not available")
            hint: qsTr("Animation applies to video, image, shape, text, and audio.")
        }

        Text {
            visible: root.supportsFade
            width: parent.width
            wrapMode: Text.WordWrap
            text: root.supportsBodyAnim
                  ? qsTr("Fade and motion combine — fade also ramps the volume.")
                  : qsTr("Fade ramps the volume in and out.")
            color: Theme.c.animationInspector.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        Flow {
            visible: root.supportsFade
            width: parent.width
            spacing: 6

            Repeater {
                model: root.supportsBodyAnim
                       ? [
                           { label: qsTr("Fade"), kind: "fade" },
                           { label: qsTr("Slide up"), kind: "slideUp" },
                           { label: qsTr("Pop"), kind: "pop" },
                           { label: qsTr("Zoom in"), kind: "zoomIn" },
                           { label: qsTr("Bounce"), kind: "bounce" },
                           { label: qsTr("Clear"), kind: "none" }
                       ]
                       : [
                           { label: qsTr("Fade"), kind: "fade" },
                           { label: qsTr("Clear"), kind: "none" }
                       ]
                delegate: ThemedChip {
                    required property var modelData
                    text: modelData.label
                    selected: {
                        if (modelData.kind === "none")
                            return false
                        if (modelData.kind === "fade")
                            return root.fadeIn > 0 && root.fadeOut > 0
                                   && root.animIn.kind === "none" && root.animOut.kind === "none"
                        return root.animIn.kind === modelData.kind
                               && root.animOut.kind === modelData.kind
                    }
                    onClicked: root.applyBoth(modelData.kind)
                }
            }
        }

        Text {
            visible: root.supportsFade
            text: qsTr("In")
            SearchAnchor {
                searchId: "animation-in"
                title: qsTr("Fade and animation in")
                keywords: "intro enter fade slide pop zoom bounce volume curve duration"
            }
            SearchAnchor {
                searchId: "animation-custom-curve"
                title: qsTr("Custom curve")
                keywords: "easing fade animation curve bezier"
            }
            color: Theme.c.animationInspector.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        EdgeSection {
            visible: root.supportsFade
            inspector: root
            edge: "in"
        }

        Text {
            visible: root.supportsFade
            text: qsTr("Out")
            SearchAnchor {
                searchId: "animation-out"
                title: qsTr("Fade and animation out")
                keywords: "outro exit fade slide pop zoom volume curve duration"
            }
            color: Theme.c.animationInspector.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        EdgeSection {
            visible: root.supportsFade
            inspector: root
            edge: "out"
        }

        Text {
            visible: root.supportsBodyAnim && root.clipKind === "text"
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTr("Letter and word animations live in the Text tab, under Animate. This moves the whole clip.")
            color: Theme.c.animationInspector.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }
}
