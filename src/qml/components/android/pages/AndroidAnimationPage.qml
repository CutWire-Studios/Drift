import QtQuick
import QtQuick.Controls.Basic
import Drift
// Per-edge fade (opacity and volume, any clip) and motion animation (visual clips).
Item {
    id: root

    property var scaffold: null
    property int trackIndex: -1
    property int clipIndex: -1

    property string edge: "in"
    property int clipDataRevision: 0

    readonly property var clipData: {
        void clipDataRevision
        return EditorState.selectedClipData
    }
    readonly property bool hasClip: !!clipData && clipData.duration !== undefined
    readonly property string clipKind: hasClip ? (clipData.kind || "") : ""
    readonly property bool supportsAnim: hasClip && clipKind !== "audio" && clipKind !== "subtitle"
    readonly property real clipDuration: hasClip ? clipData.duration : 0
    readonly property real maxLength: Math.max(0.1, Math.min(10, clipDuration))

    readonly property bool isIn: edge === "in"
    readonly property string animKey: isIn ? "animIn" : "animOut"
    readonly property real fadeIn: hasClip ? (clipData.fadeIn || 0) : 0
    readonly property real fadeOut: hasClip ? (clipData.fadeOut || 0) : 0
    readonly property string fadeCurve: hasClip ? ((isIn ? clipData.fadeInCurve : clipData.fadeOutCurve) || "smooth") : "smooth"
    readonly property var anim: {
        const a = hasClip ? clipData[animKey] : null
        return a ? a : { kind: "none", duration: 0.5, curve: "smooth" }
    }
    readonly property string animKind: anim.kind || "none"
    readonly property var kindModel: {
        const base = [{ id: "none", label: qsTr("None") }].concat(
            EditorState.clipAnimationKinds().filter(e => !e.directions || e.directions.indexOf(root.edge) >= 0))
        return base.some(e => e.id === root.animKind)
               ? base : base.concat([{ id: root.animKind, label: root.animKind }])
    }
    readonly property var curveModel: EditorState.curveStyles("fade")
    readonly property var kindIcons: ({
        "none": Theme.icons.x,
        "slideUp": Theme.icons.chevronUp,
        "slideDown": Theme.icons.chevronDown,
        "slideLeft": Theme.icons.chevronLeft,
        "slideRight": Theme.icons.chevronRight,
        "zoomIn": Theme.icons.zoomIn,
        "zoomOut": Theme.icons.zoomOut,
        "pop": Theme.icons.sparkles,
        "spinCW": Theme.icons.redo,
        "spinCCW": Theme.icons.undo,
        "bounce": Theme.icons.chevronsUpDown
    })

    function formatSeconds(v) {
        return qsTr("%1 s").arg(Number(v).toFixed(2))
    }

    function setFades(fadeInSeconds, fadeOutSeconds) {
        const inClamped = Math.max(0, Math.min(fadeInSeconds, root.clipDuration - fadeOutSeconds))
        const outClamped = Math.max(0, Math.min(fadeOutSeconds, root.clipDuration - inClamped))
        EditorState.previewSetClipFade(EditorState.selectedTrack, EditorState.selectedClip,
                                       inClamped, outClamped)
    }

    function chooseFadeCurve(id) {
        EditorState.setClipFadeCurve(EditorState.selectedTrack, EditorState.selectedClip,
                                     root.edge, id, [])
        if (id === "custom")
            ShellBridge.main.openFadeCurve(EditorState.selectedTrack, EditorState.selectedClip,
                                           root.edge, "fade")
    }

    function chooseAnimCurve(id) {
        EditorState.setClipAnimation(EditorState.selectedTrack, EditorState.selectedClip,
                                     root.animKey, { curve: id })
        if (id === "custom")
            ShellBridge.main.openFadeCurve(EditorState.selectedTrack, EditorState.selectedClip,
                                           root.edge, "animation")
    }

    function chooseKind(id) {
        if (id === "none")
            EditorState.setClipAnimation(EditorState.selectedTrack, EditorState.selectedClip,
                                         root.animKey, { kind: "none" })
        else
            EditorState.applyClipAnimationPreset(EditorState.selectedTrack, EditorState.selectedClip,
                                                 root.animKey, id)
        root.previewAnimation(root.animKey, id, root.anim.duration || 0.5)
    }

    property bool previewActive: false
    property double previewSavedPlayhead: 0
    property double previewLastPlayhead: 0

    function previewAnimation(which, kind, duration) {
        if (kind === "none" || !root.hasClip)
            return
        const length = Math.min(duration, root.clipDuration)
        if (length <= 0)
            return
        if (!root.previewActive)
            root.previewSavedPlayhead = EditorState.playheadSeconds
        root.previewActive = false
        EditorState.playing = false
        EditorState.playheadSeconds = which === "animIn"
                ? root.clipData.start
                : root.clipData.start + root.clipDuration - length
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

    Component.onDestruction: {
        if (root.previewActive) {
            previewTimer.stop()
            root.endPreview()
        }
    }

    Timer {
        id: previewTimer
        repeat: false
        onTriggered: root.endPreview()
    }

    Connections {
        target: EditorState
        function onSelectedClipDataChanged() { root.clipDataRevision++ }
        function onClipPropertiesPreviewed() { root.clipDataRevision++ }
        function onSelectionChanged() { root.clipDataRevision++ }
        function onTracksChanged() { root.clipDataRevision++ }
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

    component CurveChips: Flow {
        id: chips

        property string curve: "smooth"
        property bool chipsEnabled: true

        signal chosen(string id)

        width: parent ? parent.width : 0
        spacing: Theme.spacingSm
        enabled: chips.chipsEnabled
        opacity: chips.chipsEnabled ? 1 : 0.5

        Repeater {
            model: root.curveModel
            delegate: ThemedChip {
                required property var modelData
                chipHeight: Theme.androidMinTouchTarget
                text: modelData.label
                selected: modelData.id === chips.curve
                          || (modelData.id === "custom" && chips.curve === "bezier")
                onClicked: chips.chosen(modelData.id)
            }
        }
    }

    Flickable {
        id: scroll
        anchors.fill: parent
        contentWidth: width
        contentHeight: pageColumn.height + Theme.pagePadding * 2
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar { }

        Column {
            id: pageColumn
            x: Theme.pagePadding
            y: Theme.pagePadding
            width: scroll.width - Theme.pagePadding * 2
            spacing: Theme.spacing2xl

            Row {
                width: parent.width
                spacing: Theme.spacingSm

                Repeater {
                    model: [
                        { id: "in", label: qsTr("In") },
                        { id: "out", label: qsTr("Out") }
                    ]
                    delegate: ThemedChip {
                        required property var modelData
                        width: (parent.width - Theme.spacingSm) / 2
                        chipHeight: Theme.androidMinTouchTarget
                        text: modelData.label
                        selected: root.edge === modelData.id
                        onClicked: root.edge = modelData.id
                    }
                }
            }

            Column {
                width: parent.width
                spacing: Theme.spacingXl

                ThemedLabel {
                    width: parent.width
                    text: root.supportsAnim ? qsTr("Fade also ramps the volume.") : qsTr("Fade ramps the volume.")
                }

                AndroidFadeSliderRow {
                    id: fadeRow
                    width: parent.width
                    modelData: ({ label: root.isIn ? qsTr("Fade in") : qsTr("Fade out") })
                    current: root.isIn ? root.fadeIn : root.fadeOut
                    maxFade: root.maxLength
                    formatSeconds: function(v) { return root.formatSeconds(v) }
                    onPreviewBegan: EditorState.beginPreviewDrag(fadeRow.modelData.label)
                    onPreviewEnded: EditorState.commitPreviewDrag()
                    onMoved: (value) => {
                        if (root.isIn)
                            root.setFades(value, root.fadeOut)
                        else
                            root.setFades(root.fadeIn, value)
                    }
                }

                ThemedLabel {
                    text: qsTr("Fade curve")
                }

                CurveChips {
                    curve: root.fadeCurve
                    onChosen: (id) => root.chooseFadeCurve(id)
                }
            }

            Column {
                visible: root.supportsAnim
                width: parent.width
                spacing: Theme.spacingXl

                ThemedLabel {
                    text: qsTr("Animation")
                }

                Grid {
                    id: tileGrid
                    width: parent.width
                    columns: 3
                    spacing: Theme.spacingSm

                    Repeater {
                        model: root.kindModel
                        delegate: Rectangle {
                            id: tile
                            required property var modelData
                            readonly property bool chosen: modelData.id === root.animKind

                            width: (tileGrid.width - tileGrid.spacing * (tileGrid.columns - 1)) / tileGrid.columns
                            height: Math.max(Theme.androidMinTouchTarget * 1.5, tileContent.implicitHeight + Theme.spacingLg * 2)
                            radius: Theme.c.androidAnimationPage.tileRadius
                            color: tile.chosen ? Theme.c.androidAnimationPage.tileBackgroundSelected
                                               : Theme.c.androidAnimationPage.tileBackground
                            border.width: Theme.c.androidAnimationPage.tileBorderWidth
                            border.color: tile.chosen ? Theme.c.androidAnimationPage.tileBorderSelected
                                                      : Theme.c.androidAnimationPage.tileBorder
                            scale: tapHandler.pressed ? Theme.pressScale : 1.0

                            Behavior on scale {
                                NumberAnimation { duration: Theme.durationPress; easing.type: Theme.easing }
                            }

                            Accessible.role: Accessible.RadioButton
                            Accessible.name: modelData.label
                            Accessible.checked: tile.chosen

                            Column {
                                id: tileContent
                                anchors.centerIn: parent
                                width: parent.width - Theme.spacingSm * 2
                                spacing: Theme.spacingXs

                                IconGlyph {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    glyph: root.kindIcons[tile.modelData.id] ?? Theme.icons.sparkles
                                    iconSize: Theme.iconSizeXl
                                    iconColor: tile.chosen ? Theme.c.androidAnimationPage.tileForegroundSelected
                                                           : Theme.c.androidAnimationPage.foreground
                                }

                                Text {
                                    width: parent.width
                                    text: tile.modelData.label
                                    color: tile.chosen ? Theme.c.androidAnimationPage.tileForegroundSelected
                                                       : Theme.c.androidAnimationPage.foreground
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeXs
                                    horizontalAlignment: Text.AlignHCenter
                                    elide: Text.ElideRight
                                }
                            }

                            TapHandler {
                                id: tapHandler
                                onTapped: {
                                    Haptics.select()
                                    root.chooseKind(tile.modelData.id)
                                }
                            }
                        }
                    }
                }

                Column {
                    width: parent.width
                    spacing: Theme.spacingSm
                    enabled: root.animKind !== "none"
                    opacity: enabled ? 1 : 0.5

                    Item {
                        width: parent.width
                        height: Math.max(durationLabel.implicitHeight, durationValue.implicitHeight)

                        ThemedLabel {
                            id: durationLabel
                            anchors.left: parent.left
                            text: qsTr("Duration")
                            size: "sm"
                        }
                        Text {
                            id: durationValue
                            anchors.right: parent.right
                            text: root.formatSeconds(durationSlider.value)
                            color: Theme.c.androidAnimationPage.foregroundMuted
                            font.family: Theme.monoFontFamily
                            font.pixelSize: Theme.fontSizeSm
                        }
                    }

                    ThemedSlider {
                        id: durationSlider
                        width: parent.width
                        from: 0.1
                        to: root.maxLength
                        stepSize: 0.05
                        label: qsTr("Duration")
                        valueFormatter: root.formatSeconds
                        Binding on value {
                            when: !durationSlider.pressed
                            value: Math.min(root.maxLength, root.anim.duration || 0.5)
                        }
                        onPressedChanged: {
                            if (pressed)
                                return
                            EditorState.setClipAnimation(EditorState.selectedTrack, EditorState.selectedClip,
                                                         root.animKey, { duration: durationSlider.value })
                            root.previewAnimation(root.animKey, root.animKind, durationSlider.value)
                        }
                    }
                }

                Column {
                    width: parent.width
                    spacing: Theme.spacingSm

                    ThemedLabel {
                        text: qsTr("Animation curve")
                    }

                    CurveChips {
                        curve: root.anim.curve || "smooth"
                        chipsEnabled: root.animKind !== "none"
                        onChosen: (id) => root.chooseAnimCurve(id)
                    }
                }

                ThemedLabel {
                    visible: root.clipKind === "text"
                    width: parent.width
                    text: qsTr("Letter and word animations live in Text animation. This moves the whole clip.")
                }
            }
        }
    }
}
