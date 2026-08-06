import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
import ".."

Item {
    id: root

    // Raised by the empty state; Main wires it to the assets panel so
    // "Browse effects" actually takes the user somewhere.
    signal browseEffectsRequested()

    property int clipDataRevision: 0
    readonly property var clipData: {
        void clipDataRevision
        return EditorState.selectedClipData
    }
    readonly property bool hasSelection: !!clipData && Object.keys(clipData).length > 0
    readonly property string clipKind: hasSelection ? (clipData.kind || "") : ""
    // Depend on clipDataRevision the same way clipData does: the Repeater is keyed on
    // selectedEffects.length, so a same-length param edit would otherwise leave stale delegates.
    readonly property var selectedEffects: {
        void clipDataRevision
        return EditorState.selectedClipEffects
    }

    height: contentCol.height
    implicitHeight: contentCol.height

    function refreshFields() {}

    Connections {
        target: EditorState
        function onSelectionChanged() { root.clipDataRevision++ }
        function onSelectedClipDataChanged() { root.clipDataRevision++ }
        function onTracksChanged() { root.clipDataRevision++ }
    }

    Column {
        id: contentCol
        width: root.width
        spacing: Theme.spacingXl

        // The face warp effects follow baked landmarks, so the clip has to be
        // scanned before any of them do anything. This sits above the effect list
        // because that ordering is the workflow: detect, then apply.
        Column {
            id: faceSection
            visible: root.clipKind === "video"
            width: parent.width
            spacing: Theme.spacingSm

            // The model is an addon, but it can equally come from a bundled
            // models/face or DRIFT_FACE_MODEL_DIR, so ask the engine rather than
            // the addon registry. That answer is not a binding, hence the reset
            // below when an addon of this kind appears.
            // Folded together because every control below is gated on the same
            // answer; runtimeReady is kept apart only to say which half is missing.
            property bool runtimeReady: Addons.runtimeAvailable()
            property bool faceReady: EditorState.faceDetectionAvailable()
                                     && Addons.runtimeAvailable()
            property bool hasTrack: {
                const data = EditorState.selectedClipData
                return data && data.hasFaceTrack === true
            }
            // A track baked before contours existed still drives the warps, so it is not stale in
            // general — only the Beauty effects have nothing to work with, and they pass through.
            property bool trackHasContours: {
                const data = EditorState.selectedClipData
                return data && data.faceTrackHasContours === true
            }
            property bool usesBeautyEffect: {
                root.clipDataRevision
                const effects = EditorState.selectedClipEffects || []
                for (let i = 0; i < effects.length; i++) {
                    if ((effects[i].catalogId || "").indexOf("face_") === 0
                            && beautyIds.indexOf(effects[i].catalogId) >= 0)
                        return true
                }
                return false
            }
            readonly property var beautyIds: ["face_lipstick", "face_blush", "face_teeth_whiten",
                                              "face_eyeliner", "face_eyeshadow", "face_brow_tint",
                                              "face_eye_color", "face_beautify"]

            Connections {
                target: Addons
                function onKindChanged(kind) {
                    if (kind !== "face-model" && kind !== "onnxruntime")
                        return
                    faceSection.runtimeReady = Addons.runtimeAvailable()
                    faceSection.faceReady = EditorState.faceDetectionAvailable()
                                            && faceSection.runtimeReady
                }
            }

            Text {
                width: parent.width
                text: qsTr("Face tracking")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                visible: faceSection.faceReady && !faceSection.hasTrack
                         && !EditorState.faceDetecting
                text: qsTr("Scan this clip once, then the Funny Face effects will follow the face through it.")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            // The Beauty effects need the lip and eyelid contours, which tracks baked by older
            // builds do not carry. They pass the frame through untouched in that case, so without
            // this the effect reads as broken rather than as needing one more scan.
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                visible: faceSection.faceReady && faceSection.hasTrack
                         && !faceSection.trackHasContours && faceSection.usesBeautyEffect
                         && !EditorState.faceDetecting
                text: qsTr("This clip was scanned before makeup was supported. Re-detect faces to enable the Beauty effects.")
                color: Theme.warning
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            ThemedButton {
                visible: faceSection.faceReady && !EditorState.faceDetecting
                width: parent.width
                text: faceSection.hasTrack ? qsTr("Re-detect faces") : qsTr("Detect faces…")
                variant: faceSection.hasTrack ? "ghost" : "secondary"
                onClicked: EditorState.detectFacesForClip(
                               EditorState.selectedTrack, EditorState.selectedClip)
            }

            ThemedButton {
                visible: faceSection.faceReady && faceSection.hasTrack
                         && !EditorState.faceDetecting
                width: parent.width
                text: qsTr("Clear face track")
                variant: "ghost"
                onClicked: EditorState.clearFaceTrack(
                               EditorState.selectedTrack, EditorState.selectedClip)
            }

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                visible: EditorState.faceDetecting
                text: EditorState.faceDetectStatus
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            ThemedProgressBar {
                visible: EditorState.faceDetecting
                width: parent.width
                value: EditorState.faceDetectProgress
            }

            ThemedButton {
                visible: EditorState.faceDetecting
                width: parent.width
                text: qsTr("Cancel")
                variant: "ghost"
                onClicked: EditorState.cancelFaceDetection()
            }

            ThemedButton {
                visible: !faceSection.faceReady
                width: parent.width
                text: faceSection.runtimeReady
                      ? qsTr("Download face detection (about 5 MB)")
                      : qsTr("Install AI engine first")
                variant: "primary"
                onClicked: root.Window.window.openAddonManager(
                    faceSection.runtimeReady ? "face-model" : "onnxruntime")
            }
        }

        Column {
            width: parent.width
            spacing: 10
            visible: root.selectedEffects.length > 0

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("Move to a time, set a value, then click the diamond to add a keyframe. With Auto keyframes on, dragging a slider also creates them.")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            ThemedChip {
                text: qsTr("Auto keyframes")
                selected: EditorState.autoKeyEnabled
                onClicked: EditorState.autoKeyEnabled = !EditorState.autoKeyEnabled
            }
        }

        // Has a CTA now: the copy told the user to go to the
        // Effects library but gave them no way to get there.
        EmptyState {
            width: parent.width
            visible: root.selectedEffects.length === 0
            glyph: Theme.icons.wand
            title: qsTr("No effects yet")
            hint: qsTr("Drag a preset from the Effects library onto this clip, or click a preset card.")
            actionText: qsTr("Browse effects")
            onActionTriggered: root.browseEffectsRequested()
        }

        // Integer models keep delegates alive across preview ticks that rebuild
        // selectedClipEffects as a fresh QVariantList (same as AudioInspector).
        Repeater {
            model: root.selectedEffects.length
            delegate: Column {
                id: effectCard
                required property int index
                readonly property var effectData: root.selectedEffects[index] || ({})
                readonly property var effectParams: effectData.params || []
                readonly property bool effectEnabled: effectData.enabled !== false
                width: root.width
                spacing: 6

                Rectangle {
                    width: parent.width
                    height: effectHeader.implicitHeight + 8
                    radius: Theme.radiusSm
                    color: Theme.panelAccent

                    Row {
                        id: effectHeader
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 8
                        anchors.rightMargin: 4
                        spacing: 2

                        Text {
                            text: effectCard.effectData.label
                            color: effectCard.effectEnabled
                                   ? Theme.panelForeground : Theme.mutedForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeSm
                            font.weight: Font.Medium
                            width: parent.width - 22 * 4 - 8
                            elide: Text.ElideRight
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        IconButton {
                            glyph: Theme.icons.chevronUp
                            variant: "ghost"
                            buttonSize: 22
                            iconSize: 12
                            enabled: effectCard.index > 0
                            tooltip: qsTr("Move effect up")
                            onClicked: EditorState.moveEffect(
                                           EditorState.selectedTrack, EditorState.selectedClip,
                                           effectCard.index, effectCard.index - 1)
                        }
                        IconButton {
                            glyph: Theme.icons.chevronDown
                            variant: "ghost"
                            buttonSize: 22
                            iconSize: 12
                            enabled: effectCard.index < root.selectedEffects.length - 1
                            tooltip: qsTr("Move effect down")
                            onClicked: EditorState.moveEffect(
                                           EditorState.selectedTrack, EditorState.selectedClip,
                                           effectCard.index, effectCard.index + 1)
                        }
                        IconButton {
                            glyph: effectCard.effectEnabled ? Theme.icons.eye : Theme.icons.eyeOff
                            variant: "ghost"
                            buttonSize: 22
                            iconSize: 12
                            tooltip: effectCard.effectEnabled
                                     ? qsTr("Disable effect") : qsTr("Enable effect")
                            onClicked: EditorState.setEffectEnabled(
                                           EditorState.selectedTrack, EditorState.selectedClip,
                                           effectCard.index, !effectCard.effectEnabled)
                        }
                        IconButton {
                            glyph: Theme.icons.x
                            variant: "ghost"
                            buttonSize: 22
                            iconSize: 12
                            tooltip: qsTr("Remove effect")
                            onClicked: EditorState.removeEffect(
                                           EditorState.selectedTrack, EditorState.selectedClip,
                                           effectCard.index)
                        }
                    }
                }

                Column {
                    width: parent.width
                    spacing: 6
                    opacity: effectCard.effectEnabled ? 1 : 0.45

                    Repeater {
                        model: effectCard.effectParams.length
                        delegate: Column {
                            id: paramRow
                            required property int index
                            readonly property var paramData: effectCard.effectParams[index] || ({})
                            width: root.width
                            spacing: 4

                            // Booleans have nothing to interpolate, so they keep the
                            // plain switch and stay off the keyframe strip.
                            Row {
                                visible: paramRow.paramData.type === "bool"
                                width: parent.width
                                spacing: 8
                                Text {
                                    width: parent.width - 48
                                    elide: Text.ElideRight
                                    text: paramRow.paramData.label
                                    color: Theme.mutedForeground
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeXs
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Text {
                                    width: 40
                                    horizontalAlignment: Text.AlignRight
                                    text: paramRow.paramData.value ? qsTr("On") : qsTr("Off")
                                    color: Theme.panelForeground
                                    font.family: Theme.monoFontFamily
                                    font.pixelSize: Theme.fontSizeXs
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }

                            ThemedSwitch {
                                visible: paramRow.paramData.type === "bool"
                                checked: !!paramRow.paramData.value
                                onToggled: EditorState.setEffectParam(
                                               EditorState.selectedTrack, EditorState.selectedClip,
                                               effectCard.index, paramRow.paramData.key, checked ? 1 : 0)
                            }

                            // A shade is picked, not dialled, so colours get the swatch and stay
                            // off the keyframe strip — the track type is double all the way down.
                            Row {
                                visible: paramRow.paramData.type === "color"
                                width: parent.width
                                spacing: 8
                                Text {
                                    width: parent.width - 148
                                    elide: Text.ElideRight
                                    text: paramRow.paramData.label
                                    color: Theme.mutedForeground
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeXs
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                ColorSwatchField {
                                    anchors.verticalCenter: parent.verticalCenter
                                    hex: paramRow.paramData.value || "#ffffff"
                                    tooltip: qsTr("Choose %1").arg(paramRow.paramData.label)
                                    onEdited: value => EditorState.setEffectColorParam(
                                                  EditorState.selectedTrack, EditorState.selectedClip,
                                                  effectCard.index, paramRow.paramData.key, value)
                                }
                            }

                            // File paths (face-prop .glb): basename + Choose / Clear. Not keyframed.
                            Row {
                                visible: paramRow.paramData.type === "file"
                                width: parent.width
                                spacing: 8
                                Text {
                                    width: parent.width - 148
                                    elide: Text.ElideMiddle
                                    text: {
                                        const p = paramRow.paramData.value || ""
                                        if (!p)
                                            return paramRow.paramData.label + qsTr(": (none)")
                                        const parts = String(p).split(/[/\\]/)
                                        return parts[parts.length - 1] || p
                                    }
                                    color: paramRow.paramData.missing ? Theme.destructive
                                                                     : Theme.mutedForeground
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeXs
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                IconButton {
                                    glyph: Theme.icons.folder
                                    variant: "ghost"
                                    buttonSize: 22
                                    iconSize: 12
                                    tooltip: qsTr("Choose file")
                                    anchors.verticalCenter: parent.verticalCenter
                                    onClicked: {
                                        const filters = paramRow.paramData.fileFilters || ["All files (*)"]
                                        const url = FileDialogs.openFile(
                                            qsTr("Choose %1").arg(paramRow.paramData.label), filters)
                                        if (!url || url.toString() === "")
                                            return
                                        EditorState.setEffectStringParam(
                                            EditorState.selectedTrack, EditorState.selectedClip,
                                            effectCard.index, paramRow.paramData.key, url)
                                    }
                                }
                                IconButton {
                                    glyph: Theme.icons.x
                                    variant: "ghost"
                                    buttonSize: 22
                                    iconSize: 12
                                    tooltip: qsTr("Clear")
                                    enabled: !!(paramRow.paramData.value)
                                    anchors.verticalCenter: parent.verticalCenter
                                    onClicked: EditorState.setEffectStringParam(
                                                   EditorState.selectedTrack, EditorState.selectedClip,
                                                   effectCard.index, paramRow.paramData.key, "")
                                }
                            }

                            PropertyKeyframeRow {
                                visible: paramRow.paramData.type === "float"
                                width: parent.width
                                // `def` is the param's static value, which the row falls
                                // back to whenever the track holds no keys.
                                propDef: ({
                                    key: paramRow.paramData.prop,
                                    label: paramRow.paramData.label,
                                    def: paramRow.paramData.value,
                                    decimals: Math.abs(paramRow.paramData.max
                                                       - paramRow.paramData.min) >= 10 ? 1 : 2
                                })
                                keyframeList: (paramRow.paramData.keyframes
                                               && paramRow.paramData.keyframes.points) || []
                                useSlider: true
                                sliderFrom: paramRow.paramData.min
                                sliderTo: paramRow.paramData.max
                            }
                        }
                    }
                }
            }
        }
    }
}
