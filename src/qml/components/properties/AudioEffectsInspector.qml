import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
import ".."

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
    readonly property bool hasAudio: clipKind === "audio" || clipKind === "video"
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
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        ThemedButton {
            visible: root.hasAudio && root.audioFxCatalog.length === 0
            width: parent.width
            text: qsTr("Install audio effects")
            variant: "primary"
            onClicked: root.Window.window.openAddonManager("audio-effects")
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

        // Integer models: previewSet* rebuilds selectedClipAudioEffects as a new
        // QVariantList on every tick. A list model would regenerate delegates and
        // destroy the pressed slider; a count only changes when effects are added/removed.
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

                Rectangle {
                    width: parent.width
                    height: audioEffectHeader.implicitHeight + 8
                    radius: Theme.radiusSm
                    color: Theme.panelAccent

                    Row {
                        id: audioEffectHeader
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 8
                        anchors.rightMargin: 4
                        spacing: 2

                        IconGlyph {
                            anchors.verticalCenter: parent.verticalCenter
                            glyph: audioEffectCard.effectData.icon || "audio-lines"
                            iconSize: 14
                            iconColor: Theme.mutedForeground
                            opacity: audioEffectCard.effectEnabled ? 1 : 0.5
                        }
                        Text {
                            text: audioEffectCard.effectData.missing
                                  ? qsTr("%1 (not installed)").arg(audioEffectCard.effectData.label)
                                  : audioEffectCard.effectData.label
                            color: audioEffectCard.effectEnabled
                                   ? Theme.panelForeground : Theme.mutedForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeSm
                            font.weight: Font.Medium
                            width: parent.width - 22 * 5 - 20 - 8
                            elide: Text.ElideRight
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        IconButton {
                            glyph: Theme.icons.chevronUp
                            variant: "ghost"
                            buttonSize: 22
                            iconSize: 12
                            enabled: audioEffectCard.index > 0
                            tooltip: qsTr("Move audio effect up")
                            onClicked: EditorState.moveAudioEffect(
                                           EditorState.selectedTrack, EditorState.selectedClip,
                                           audioEffectCard.index, audioEffectCard.index - 1)
                        }
                        IconButton {
                            glyph: Theme.icons.chevronDown
                            variant: "ghost"
                            buttonSize: 22
                            iconSize: 12
                            enabled: audioEffectCard.index < root.selectedAudioEffects.length - 1
                            tooltip: qsTr("Move audio effect down")
                            onClicked: EditorState.moveAudioEffect(
                                           EditorState.selectedTrack, EditorState.selectedClip,
                                           audioEffectCard.index, audioEffectCard.index + 1)
                        }
                        IconButton {
                            glyph: audioEffectCard.effectEnabled ? Theme.icons.eye : Theme.icons.eyeOff
                            variant: "ghost"
                            buttonSize: 22
                            iconSize: 12
                            tooltip: audioEffectCard.effectEnabled
                                     ? qsTr("Disable audio effect") : qsTr("Enable audio effect")
                            onClicked: EditorState.setAudioEffectEnabled(
                                           EditorState.selectedTrack, EditorState.selectedClip,
                                           audioEffectCard.index, !audioEffectCard.effectEnabled)
                        }
                        IconButton {
                            glyph: Theme.icons.copy
                            variant: "ghost"
                            buttonSize: 22
                            iconSize: 12
                            tooltip: qsTr("Copy this audio effect")
                            onClicked: EditorState.copyAudioEffectToClipboard(
                                           EditorState.selectedTrack, EditorState.selectedClip,
                                           audioEffectCard.index)
                        }
                        IconButton {
                            glyph: Theme.icons.x
                            variant: "ghost"
                            buttonSize: 22
                            iconSize: 12
                            tooltip: qsTr("Remove audio effect")
                            onClicked: EditorState.removeAudioEffect(
                                           EditorState.selectedTrack, EditorState.selectedClip,
                                           audioEffectCard.index)
                        }
                    }
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

                            Row {
                                width: parent.width
                                spacing: 8
                                Text {
                                    width: parent.width - 48
                                    elide: Text.ElideRight
                                    text: audioParamRow.paramData.label
                                    color: Theme.mutedForeground
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeXs
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Text {
                                    width: 40
                                    horizontalAlignment: Text.AlignRight
                                    text: audioParamRow.paramData.isBoolean
                                          ? (audioParamRow.paramData.value ? qsTr("On") : qsTr("Off"))
                                          : Number(audioParamSlider.value).toFixed(
                                                Math.abs(audioParamRow.paramData.max - audioParamRow.paramData.min) >= 10 ? 1 : 2)
                                    color: Theme.panelForeground
                                    font.family: Theme.monoFontFamily
                                    font.pixelSize: Theme.fontSizeXs
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }

                            ThemedSwitch {
                                visible: !!audioParamRow.paramData.isBoolean
                                checked: !!audioParamRow.paramData.value
                                onToggled: EditorState.previewSetAudioEffectParam(
                                               EditorState.selectedTrack, EditorState.selectedClip,
                                               audioEffectCard.index, audioParamRow.paramData.key,
                                               checked ? 1 : 0)
                            }

                            ThemedSlider {
                                id: audioParamSlider
                                label: audioParamRow.paramData.label
                                visible: !audioParamRow.paramData.isBoolean
                                width: parent.width
                                from: audioParamRow.paramData.min
                                to: audioParamRow.paramData.max
                                // Same pattern as PreviewPanel scrub: keep the model binding
                                // off while pressed so preview ticks cannot fight the drag.
                                Binding on value {
                                    when: !audioParamSlider.pressed
                                    value: audioParamRow.paramData.value
                                }
                                onMoved: EditorState.previewSetAudioEffectParam(
                                             EditorState.selectedTrack, EditorState.selectedClip,
                                             audioEffectCard.index, audioParamRow.paramData.key, value)
                                onPressedChanged: {
                                    if (pressed)
                                        EditorState.beginPreviewDrag(qsTr("Edit audio effect"))
                                    else
                                        EditorState.commitPreviewDrag()
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
