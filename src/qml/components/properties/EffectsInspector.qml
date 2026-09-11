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

    // The name prompt lives in PropertiesPanel, so the inspector only says which stack the
    // user asked to save. -1 means the whole clip rather than one effect.
    signal saveEffectPresetRequested(int effectIndex)

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

        // The face warp effects follow baked landmarks and pass the frame through untouched
        // without them, so the scan is offered here — beside the stack that needs it — rather
        // than as a step on every clip. Adding a face effect starts the scan by itself
        // (AppController::addEffect); this is what is left to say when that could not happen:
        // the model is missing, the scan was cancelled, or the track predates what the effect
        // reads. Hidden entirely when no face effect is in the stack.
        Column {
            id: faceSection
            // Present whenever a face effect is, so re-detecting and clearing stay reachable;
            // it is the warnings below that appear only when the track is missing or too old.
            visible: faceSection.usesFaceEffect
            width: parent.width
            spacing: Theme.spacingSm

            // The model is an addon, but it can equally come from a bundled models/face or
            // DRIFT_FACE_MODEL_DIR, so ask the engine rather than the addon registry. That answer
            // is not a binding, hence the reset below when an addon of this kind appears.
            // Folded together because every control below is gated on the same answer;
            // runtimeReady is kept apart only to say which half is missing.
            property bool runtimeReady: Addons.runtimeAvailable()
            property bool faceReady: EditorState.faceDetectionAvailable()
                                     && Addons.runtimeAvailable()

            // Landmarks are baked onto the media clip, and the selection here is the adjustment
            // pinned to it — clipToMap reports the linked clip's state for exactly this.
            property bool canTrack: {
                void root.clipDataRevision
                const data = EditorState.selectedClipData
                return data && data.canFaceTrack === true
            }
            property bool hasTrack: {
                void root.clipDataRevision
                const data = EditorState.selectedClipData
                return data && data.hasFaceTrack === true
            }
            // A track baked before contours existed still drives the warps, so it is not stale in
            // general — only the Beauty effects have nothing to work with, and they pass through.
            property bool trackHasContours: {
                void root.clipDataRevision
                const data = EditorState.selectedClipData
                return data && data.faceTrackHasContours === true
            }
            // Same idea as contours: a pre-mesh track still drives warps and makeup, but the 3D
            // Face Mesh effect has nothing to warp until the clip is scanned again.
            property bool trackHasMesh: {
                void root.clipDataRevision
                const data = EditorState.selectedClipData
                return data && data.faceTrackHasMesh === true
            }

            readonly property var beautyIds: ["face_lipstick", "face_blush", "face_teeth_whiten",
                                              "face_eyeliner", "face_eyeshadow", "face_brow_tint",
                                              "face_eye_color", "face_beautify"]

            // One pass over the stack for all three answers: whether anything here needs a track
            // at all, and whether what needs it needs a *newer* one.
            readonly property var faceUse: {
                void root.clipDataRevision
                const effects = EditorState.selectedClipEffects || []
                let any = false
                let beauty = false
                let mesh = false
                for (let i = 0; i < effects.length; i++) {
                    const id = effects[i].catalogId || ""
                    if (id.indexOf("face_") !== 0)
                        continue
                    any = true
                    if (faceSection.beautyIds.indexOf(id) >= 0)
                        beauty = true
                    if (id === "face_mesh_3d")
                        mesh = true
                }
                return { any: any, beauty: beauty, mesh: mesh }
            }
            readonly property bool usesFaceEffect: faceUse.any

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

            // A standalone adjustment layer covers everything below it, so there is no one clip
            // whose faces could be traced. The effect is inert here and no scan would fix it.
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                visible: !faceSection.canTrack
                text: qsTr("Face effects follow one clip's faces. Add this to a clip rather than to an adjustment layer.")
                color: Theme.warning
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            // The effect is in the stack and doing nothing. Said as a warning, not a hint: the
            // preview looks untouched and there is no other clue why.
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                visible: faceSection.canTrack && faceSection.faceReady && !faceSection.hasTrack
                         && !EditorState.faceDetecting
                text: qsTr("These effects follow a face, so the clip has to be scanned before they do anything.")
                color: Theme.warning
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            // The Beauty effects need the lip and eyelid contours, which tracks baked by older
            // builds do not carry. They pass the frame through untouched in that case, so without
            // this the effect reads as broken rather than as needing one more scan.
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                visible: faceSection.canTrack && faceSection.faceReady && faceSection.hasTrack
                         && !faceSection.trackHasContours && faceSection.faceUse.beauty
                         && !EditorState.faceDetecting
                text: qsTr("This clip was scanned before makeup was supported. Re-detect faces to enable the Beauty effects.")
                color: Theme.warning
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            // The 3D Face Mesh effect needs the 468-vertex blob, which tracks baked by older
            // builds do not carry. It skips drawing in that case, so without this the effect
            // reads as broken rather than as needing one more scan.
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                visible: faceSection.canTrack && faceSection.faceReady && faceSection.hasTrack
                         && !faceSection.trackHasMesh && faceSection.faceUse.mesh
                         && !EditorState.faceDetecting
                text: qsTr("This clip was scanned before 3D face mesh was supported. Re-detect faces to enable the 3D Face Mesh effect.")
                color: Theme.warning
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            ThemedButton {
                visible: faceSection.canTrack && faceSection.faceReady && !EditorState.faceDetecting
                width: parent.width
                text: faceSection.hasTrack ? qsTr("Re-detect faces") : qsTr("Scan for faces…")
                variant: faceSection.hasTrack ? "ghost" : "secondary"
                onClicked: EditorState.detectFacesForClip(
                               EditorState.selectedTrack, EditorState.selectedClip)
            }

            ThemedButton {
                visible: faceSection.canTrack && faceSection.faceReady && faceSection.hasTrack
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
                visible: faceSection.canTrack && !faceSection.faceReady
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
        // selectedClipEffects as a fresh QVariantList (same as AudioEffectsInspector).
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

                    // Save-as-preset lives here rather than in the header row: a fifth ghost
                    // button already crowds a 22px row at panel width, and a sixth would leave
                    // the label permanently elided.
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: effectCardMenu.popup()
                    }
                    ThemedContextMenu {
                        id: effectCardMenu
                        ThemedMenuItem {
                            text: qsTr("Copy this effect")
                            icon.name: Theme.icons.copy
                            onTriggered: EditorState.copyEffectToClipboard(
                                             EditorState.selectedTrack, EditorState.selectedClip,
                                             effectCard.index)
                        }
                        ThemedMenuItem {
                            text: qsTr("Save as preset…")
                            icon.name: Theme.icons.save
                            onTriggered: root.saveEffectPresetRequested(effectCard.index)
                        }
                    }

                    Row {
                        id: effectHeader
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 8
                        anchors.rightMargin: 4
                        spacing: 2

                        Text {
                            // An effect from an addon that is not installed has no catalog entry,
                            // so it renders with no params at all; saying so beats a blank card.
                            text: effectCard.effectData.missing
                                  ? qsTr("%1 (not installed)").arg(effectCard.effectData.label)
                                  : effectCard.effectData.label
                            color: effectCard.effectEnabled
                                   ? Theme.panelForeground : Theme.mutedForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeSm
                            font.weight: Font.Medium
                            width: parent.width - 22 * 5 - 8
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
                            glyph: Theme.icons.copy
                            variant: "ghost"
                            buttonSize: 22
                            iconSize: 12
                            tooltip: qsTr("Copy this effect")
                            onClicked: EditorState.copyEffectToClipboard(
                                           EditorState.selectedTrack, EditorState.selectedClip,
                                           effectCard.index)
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

        // Paste is offered even with an empty clipboard: checking costs a synchronous round-trip
        // to whichever process owns the selection, so the button asks only when it is pressed.
        Row {
            visible: root.hasSelection
            width: parent.width
            spacing: Theme.spacingSm

            ThemedButton {
                text: qsTr("Paste effects")
                glyph: Theme.icons.clipboardPaste
                variant: "secondary"
                onClicked: EditorState.pasteEffectsFromClipboard(
                               EditorState.selectedTrack, EditorState.selectedClip)
            }
            ThemedButton {
                text: qsTr("Save as preset…")
                glyph: Theme.icons.save
                variant: "secondary"
                visible: root.selectedEffects.length > 0
                onClicked: root.saveEffectPresetRequested(-1)
            }
        }

    }
}
