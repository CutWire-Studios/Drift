import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
// Applied audio-effect stack for the selected clip. Browse presets in the
// assets panel Audio FX tab; edit parameters here.
Item {
    id: root

    signal browseAudioEffectsRequested()

    property int clipDataRevision: 0
    readonly property var clipData: {
        void clipDataRevision
        return EditorState.selectedClipData
    }
    readonly property bool hasSelection: !!clipData && Object.keys(clipData).length > 0
    readonly property string clipKind: hasSelection ? (clipData.kind || "") : ""
    // An audio-effects adjustment is the stack: it is not itself an audio clip, but the tab is
    // only ever offered for one whose linked clip has audio, so there is nothing to gate on.
    readonly property bool hasAudio: clipKind === "audio" || clipKind === "video"
                                     || (clipKind === "adjustment"
                                         && clipData.adjustmentKind === "audioEffects")
    readonly property var selectedAudioEffects: EditorState.selectedClipAudioEffects
    readonly property var audioFxCatalog: hasAudio ? EditorState.audioEffectCatalog() : []

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

        EmptyState {
            visible: !root.hasAudio
            width: parent.width
            compact: true
            glyph: Theme.icons.volumeOff
            title: qsTr("No audio")
            hint: qsTr("Audio effects apply to clips with an audio track.")
        }

        Text {
            visible: root.hasAudio && root.audioFxCatalog.length === 0
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTr("No audio effects installed. Get the Audio Effects pack from Extras.")
            color: Theme.c.audioEffectsInspector.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        ThemedButton {
            visible: root.hasAudio && root.audioFxCatalog.length === 0
            width: parent.width
            text: qsTr("Install audio effects")
            variant: "primary"
            onClicked: ShellBridge.main.openAddonManager("audio-effects")
        }

        EmptyState {
            width: parent.width
            visible: root.hasAudio && root.audioFxCatalog.length > 0
                     && root.selectedAudioEffects.length === 0
            glyph: Theme.icons.audioLines
            title: qsTr("No audio effects yet")
            hint: qsTr("Drag a preset from Audio FX onto this clip, or click a preset card.")
            actionText: qsTr("Browse audio effects")
            onActionTriggered: root.browseAudioEffectsRequested()
        }

        Column {
            width: parent.width
            spacing: 10
            visible: root.hasAudio && root.selectedAudioEffects.length > 0

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("Move to a time, set a value, then click the diamond to add a keyframe. With Auto keyframes on, dragging a slider also creates them.")
                color: Theme.c.audioEffectsInspector.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            ThemedChip {
                text: qsTr("Auto keyframes")
                selected: EditorState.autoKeyEnabled
                onClicked: EditorState.autoKeyEnabled = !EditorState.autoKeyEnabled
            }
        }

        // Integer models: previewSet* rebuilds selectedClipAudioEffects as a new
        // QVariantList on every tick. A list model would regenerate delegates and
        // destroy the pressed slider; a count only changes when effects are added/removed.
        SearchAnchor {
            searchId: "audiofx-settings"
            title: qsTr("Audio effect settings")
            keywords: "parameters"
        }
        Repeater {
            model: root.hasAudio ? root.selectedAudioEffects.length : 0
            delegate: Column {
                id: audioEffectCard
                required property int index
                readonly property var effectData: root.selectedAudioEffects[index] || ({})
                readonly property var effectParams: effectData.params || []
                readonly property bool effectEnabled: effectData.enabled !== false
                width: root.width
                spacing: 6

                AudioEffectHeader {
                    card: audioEffectCard
                    inspector: root
                }

                Column {
                    width: parent.width
                    spacing: 4
                    opacity: audioEffectCard.effectEnabled ? 1 : 0.45

                    Repeater {
                        model: audioEffectCard.effectParams.length
                        delegate: Column {
                            id: audioParamRow
                            required property int index
                            readonly property var paramData: audioEffectCard.effectParams[index] || ({})
                            width: root.width
                            spacing: 4

                            readonly property var keyframeList: (paramData.keyframes
                                                                 && paramData.keyframes.points) || []
                            readonly property bool animated: keyframeList.length > 0

                            // Switches key the playhead like the video inspector's: animated, a
                            // toggle writes a key; static, it sets the value.
                            Row {
                                visible: !!audioParamRow.paramData.isBoolean
                                width: parent.width
                                spacing: 8
                                ChannelKeyButton {
                                    anchors.verticalCenter: parent.verticalCenter
                                    keyframeList: audioParamRow.keyframeList
                                    label: audioParamRow.paramData.label
                                    onAddRequested: EditorState.setClipKeyframe(
                                                        EditorState.selectedTrack, EditorState.selectedClip,
                                                        audioParamRow.paramData.prop, EditorState.playheadSeconds,
                                                        audioParamRow.paramData.value ? 1 : 0)
                                    onRemoveRequested: EditorState.removeClipKeyframe(
                                                           EditorState.selectedTrack, EditorState.selectedClip,
                                                           audioParamRow.paramData.prop, EditorState.playheadSeconds)
                                }
                                Text {
                                    width: parent.width - 48 - 30
                                    elide: Text.ElideRight
                                    text: audioParamRow.paramData.label
                                    color: Theme.c.audioEffectsInspector.foregroundMuted
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeXs
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                ThemedSwitch {
                                    anchors.verticalCenter: parent.verticalCenter
                                    checked: audioParamRow.animated
                                             ? EditorState.propertyValueAt(
                                                   EditorState.selectedTrack, EditorState.selectedClip,
                                                   audioParamRow.paramData.prop,
                                                   EditorState.inspectorPlayheadSeconds,
                                                   audioParamRow.paramData.value ? 1 : 0) > 0.5
                                             : !!audioParamRow.paramData.value
                                    onToggled: audioParamRow.animated
                                               ? EditorState.setClipKeyframe(
                                                     EditorState.selectedTrack, EditorState.selectedClip,
                                                     audioParamRow.paramData.prop, EditorState.playheadSeconds,
                                                     checked ? 1 : 0)
                                               : EditorState.setAudioEffectParam(
                                                     EditorState.selectedTrack, EditorState.selectedClip,
                                                     audioEffectCard.index, audioParamRow.paramData.key,
                                                     checked ? 1 : 0)
                                }
                            }

                            PropertyKeyframeRow {
                                visible: !audioParamRow.paramData.isBoolean
                                width: parent.width
                                // `def` is the param's static value, which the row falls back to
                                // whenever the track holds no keys.
                                propDef: ({
                                    key: audioParamRow.paramData.prop || "",
                                    label: audioParamRow.paramData.label,
                                    def: audioParamRow.paramData.value,
                                    decimals: Math.abs(audioParamRow.paramData.max
                                                       - audioParamRow.paramData.min) >= 10 ? 1 : 2
                                })
                                keyframeList: audioParamRow.keyframeList
                                useSlider: true
                                editableWhilePlaying: true
                                sliderFrom: audioParamRow.paramData.min
                                sliderTo: audioParamRow.paramData.max
                            }
                        }
                    }
                }
            }
        }
    }
}
