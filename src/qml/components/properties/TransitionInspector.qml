import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

Item {
    id: root

    property int clipDataRevision: 0
    property int transitionDataRevision: 0
    property bool suppressTransitionKindUpdate: false

    readonly property var clipData: {
        void clipDataRevision
        return EditorState.selectedClipData
    }
    readonly property bool hasSelection: !!clipData && Object.keys(clipData).length > 0
    readonly property string clipKind: hasSelection ? (clipData.kind || "") : ""

    readonly property int transitionEditTrack: EditorState.selectedTransitionTrack >= 0
                                                 ? EditorState.selectedTransitionTrack
                                                 : EditorState.selectedTrack
    readonly property int transitionEditLeftClip: EditorState.selectedTransitionLeftClip >= 0
                                                    ? EditorState.selectedTransitionLeftClip
                                                    : EditorState.selectedClip
    readonly property var activeTransition: {
        void transitionDataRevision
        const selected = EditorState.selectedTransitionData
        if (selected && Object.keys(selected).length > 0)
            return selected
        return EditorState.transitionBetweenClips(
                   root.transitionEditTrack, root.transitionEditLeftClip)
    }
    readonly property bool hasActiveTransition: !!activeTransition && Object.keys(activeTransition).length > 0
    readonly property bool canAddOutgoingTransition: {
        if (!root.hasSelection)
            return false
        const tracks = EditorState.tracks
        const t = EditorState.selectedTrack
        const c = EditorState.selectedClip
        if (t < 0 || !tracks || t >= tracks.length)
            return false
        const track = tracks[t]
        if (track.type !== "video" && track.type !== "shape" && track.type !== "text")
            return false
        if (c < 0 || c >= track.clips.length)
            return false
        const left = track.clips[c]
        for (let i = 0; i < track.clips.length; i++) {
            if (i === c)
                continue
            const right = track.clips[i]
            if (right.start < left.start)
                continue
            const gap = right.start - (left.start + left.duration)
            if (gap <= 0.001)
                return true
        }
        return false
    }

    height: contentCol.height
    implicitHeight: contentCol.height

    function refreshFields() {
        if (!root.hasActiveTransition)
            return
        if (transitionDurationField && !transitionDurationField.activeFocus)
            transitionDurationField.value = root.activeTransition.duration || 0.5
        if (transitionKindBox) {
            const kinds = EditorState.transitionKinds()
            const active = root.activeTransition.kind || "crossfade"
            let idx = 0
            for (let i = 0; i < kinds.length; ++i) {
                if (kinds[i].kind === active) {
                    idx = i
                    break
                }
            }
            root.suppressTransitionKindUpdate = true
            transitionKindBox.currentIndex = idx
            root.suppressTransitionKindUpdate = false
        }
    }

    function commitEdits() {
        if (!root.hasActiveTransition)
            return
        const transitionId = root.activeTransition.id
        if (!transitionId)
            return
        if (transitionDurationField) {
            const v = transitionDurationField.value
            const current = Number(root.activeTransition.duration || 0.5)
            if (Math.abs(v - current) > 0.0001)
                EditorState.setTransitionDuration(
                    root.transitionEditTrack, transitionId, v)
        }
        if (transitionKindBox && transitionKindBox.currentIndex >= 0) {
            const kinds = EditorState.transitionKinds()
            const item = kinds[transitionKindBox.currentIndex]
            if (item && item.kind !== (root.activeTransition.kind || "crossfade"))
                EditorState.setTransitionKind(
                    root.transitionEditTrack, transitionId, item.kind)
        }
    }

    Connections {
        target: EditorState
        function onSelectionChanged() {
            root.clipDataRevision++
            root.transitionDataRevision++
            root.refreshFields()
        }
        function onSelectedTransitionDataChanged() {
            root.transitionDataRevision++
            root.refreshFields()
        }
        function onTracksChanged() {
            root.clipDataRevision++
            root.transitionDataRevision++
            root.refreshFields()
        }
    }

    Component.onCompleted: refreshFields()

    Column {
        id: contentCol
        width: root.width
        spacing: Theme.spacingXl

        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            visible: !root.hasActiveTransition && !root.canAddOutgoingTransition
            text: root.clipKind === "video" || root.clipKind === "shape" || root.clipKind === "text"
                  ? qsTr("Select where two clips overlap (shown in purple), or drag a clip so it overlaps the next one.")
                  : qsTr("Transitions work between two clips on a video, shape, or text track.")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
        }

        Column {
            width: parent.width
            spacing: 8
            visible: !root.hasActiveTransition && root.canAddOutgoingTransition

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("No transition after this clip. Add one at the cut to the next clip.")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
            }

            ThemedButton {
                text: qsTr("Add crossfade (0.5 s)")
                variant: "primary"
                onClicked: EditorState.addTransition(
                               EditorState.selectedTrack, EditorState.selectedClip,
                               "crossfade", 0.5)
            }
        }

        Column {
            width: parent.width
            spacing: 12
            visible: root.hasActiveTransition

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: root.activeTransition.overlapping
                      ? qsTr("Overlap transition. Drag another kind from Transitions to replace it.")
                      : qsTr("Transition to the next clip. Move across the cut to preview it.")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            Column {
                width: parent.width
                spacing: 4
                Text {
                    text: qsTr("Type")
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
                ThemedComboBox {
                    id: transitionKindBox
                    width: parent.width
                    textRole: "label"
                    valueRole: "kind"
                    model: EditorState.transitionKinds()
                    onActivated: transitionKindBox.commitTransitionKind()
                    onCurrentIndexChanged: {
                        if (root.suppressTransitionKindUpdate || !root.hasActiveTransition)
                            return
                        transitionKindBox.commitTransitionKind()
                    }

                    function commitTransitionKind() {
                        const item = model[currentIndex]
                        if (!item || !root.hasActiveTransition)
                            return
                        EditorState.setTransitionKind(
                            root.transitionEditTrack,
                            root.activeTransition.id, item.kind)
                    }
                }
            }

            Column {
                width: parent.width
                spacing: 4
                Text {
                    text: qsTr("Duration")
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
                ThemedNumberField {
                    id: transitionDurationField
                    to: 60
                    unit: "s"
                    width: parent.width
                    decimals: 2
                    step: 0.05
                    from: 0.05
                    onEdited: v => {
                        if (!root.hasActiveTransition)
                            return
                        EditorState.setTransitionDuration(
                            root.transitionEditTrack, root.activeTransition.id, v)
                    }
                }
            }

            // Shader parameters declared by the active transition package.
            // Integer model: preview ticks replace params as a new list; a count
            // model keeps the pressed slider alive across those updates.
            Repeater {
                model: (root.activeTransition.params || []).length
                delegate: Column {
                    id: trParamRow
                    required property int index
                    readonly property var paramData: (root.activeTransition.params || [])[index] || ({})
                    width: root.width
                    spacing: 4

                    Row {
                        width: parent.width
                        spacing: 8
                        Text {
                            width: parent.width - 48
                            elide: Text.ElideRight
                            text: trParamRow.paramData.label
                            color: Theme.mutedForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            width: 40
                            horizontalAlignment: Text.AlignRight
                            text: trParamRow.paramData.isBoolean
                                  ? (trParamRow.paramData.value ? qsTr("On") : qsTr("Off"))
                                  : Number(trParamSlider.value).toFixed(
                                        Math.abs(trParamRow.paramData.max - trParamRow.paramData.min) >= 10 ? 1 : 2)
                            color: Theme.panelForeground
                            font.family: Theme.monoFontFamily
                            font.pixelSize: Theme.fontSizeXs
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    ThemedSwitch {
                        visible: !!trParamRow.paramData.isBoolean
                        checked: !!trParamRow.paramData.value
                        onToggled: EditorState.setTransitionParam(
                                       root.transitionEditTrack, root.activeTransition.id,
                                       trParamRow.paramData.key, checked ? 1 : 0)
                    }

                    ThemedSlider {
                        id: trParamSlider
                        label: trParamRow.paramData.label
                        visible: !trParamRow.paramData.isBoolean
                        width: parent.width
                        from: trParamRow.paramData.min
                        to: trParamRow.paramData.max
                        Binding on value {
                            when: !trParamSlider.pressed
                            value: trParamRow.paramData.value
                        }
                        onMoved: EditorState.previewSetTransitionParam(
                                     root.transitionEditTrack, root.activeTransition.id,
                                     trParamRow.paramData.key, value)
                        onPressedChanged: {
                            if (pressed)
                                EditorState.beginPreviewDrag(qsTr("Edit transition"))
                            else
                                EditorState.commitPreviewDrag()
                        }
                    }
                }
            }

            ThemedButton {
                text: qsTr("Remove transition")
                variant: "destructive"
                onClicked: EditorState.removeTransition(
                               root.transitionEditTrack, root.activeTransition.id)
            }
        }
    }
}
