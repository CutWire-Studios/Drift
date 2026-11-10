import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
Item {
    id: root

    property int clipDataRevision: 0
    property int transitionDataRevision: 0

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
            transitionDurationField.value = root.activeTransition.duration || EditorState.preferences.transitionDurationSeconds
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
                  ? qsTr("Hover the cut between two clips and click + to add a transition.")
                  : qsTr("Transitions work between two clips on a video, shape, or text track.")
            color: Theme.c.transitionInspector.foregroundMuted
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
                color: Theme.c.transitionInspector.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
            }

            ThemedButton {
                id: addTransitionButton
                text: qsTr("Add transition…")
                variant: "primary"
                onClicked: {
                    const track = EditorState.selectedTrack
                    const left = EditorState.selectedClip
                    const page = ShellBridge.main ? ShellBridge.main.editorPage : null
                    if (page && page.openTransitionPicker)
                        page.openTransitionPicker(track, left, "", "")
                    else
                        addPicker.openFor(track, left, "", "", addTransitionButton, 0,
                                          addTransitionButton.height + Theme.spacingXs)
                }

                TransitionPickerPopup {
                    id: addPicker
                }
            }
        }

        Column {
            width: parent.width
            spacing: 12
            visible: root.hasActiveTransition

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: {
                    const t = root.activeTransition
                    let line = t.overlapping
                        ? qsTr("Overlap transition. Drag another kind from Transitions to replace it.")
                        : qsTr("Transition to the next clip. Move across the cut to preview it.")
                    const h = t.handles || {}
                    if (!t.overlapping && (h.out === false || h.in === false))
                        line += " " + qsTr("One side has no media past the cut, so its sound fades through silence instead of crossfading.")
                    return line
                }
                color: Theme.c.transitionInspector.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            Column {
                width: parent.width
                spacing: 4
                Text {
                    text: qsTr("Type")
                    color: Theme.c.transitionInspector.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
                // A thumbnail picker instead of a name list: many kinds are only told apart by how they look.
                Rectangle {
                    id: kindButton
                    readonly property var kindInfo: {
                        const kinds = EditorState.transitionKindsForTrack(root.transitionEditTrack)
                        const active = root.hasActiveTransition ? (root.activeTransition.kind || "crossfade") : ""
                        for (let i = 0; i < kinds.length; ++i) {
                            if (kinds[i].kind === active)
                                return kinds[i]
                        }
                        return ({ kind: active, label: active })
                    }
                    readonly property string strip: kindInfo.previewStripPath || ""
                    readonly property int frameCount: Math.max(1, kindInfo.previewFrames || 1)
                    width: parent.width
                    height: Theme.controlHeight * 1.6
                    radius: Theme.c.themedComboBox.radius
                    color: Theme.c.themedComboBox.background
                    border.width: kindMouse.containsMouse || kindButton.activeFocus
                                  ? Theme.c.themedComboBox.borderWidthFocus
                                  : Theme.c.themedComboBox.borderWidth
                    border.color: kindMouse.containsMouse || kindButton.activeFocus
                                  ? Theme.c.themedComboBox.borderFocus
                                  : Theme.c.themedComboBox.border
                    activeFocusOnTab: true
                    Keys.onSpacePressed: kindButton.openPicker()
                    Keys.onReturnPressed: kindButton.openPicker()

                    function openPicker() {
                        if (!root.hasActiveTransition)
                            return
                        const track = root.transitionEditTrack
                        const left = root.transitionEditLeftClip
                        const id = root.activeTransition.id
                        const kind = root.activeTransition.kind || "crossfade"
                        const page = ShellBridge.main ? ShellBridge.main.editorPage : null
                        if (page && page.openTransitionPicker)
                            page.openTransitionPicker(track, left, id, kind)
                        else
                            kindPicker.openFor(track, left, id, kind, kindButton, 0, kindButton.height + Theme.spacingXs)
                    }

                    Row {
                        anchors.fill: parent
                        anchors.margins: Theme.spacingXs
                        spacing: Theme.spacingSm

                        Item {
                            id: kindThumb
                            width: height
                            height: parent.height
                            clip: true

                            Image {
                                visible: kindButton.strip.length > 0 && status === Image.Ready
                                source: kindButton.strip.length > 0 ? EditorState.imageUrl(kindButton.strip) : ""
                                height: parent.height
                                width: parent.height * kindButton.frameCount
                                x: -Math.round(0.45 * (kindButton.frameCount - 1)) * parent.height
                                sourceSize.height: Math.ceil(parent.height * Screen.devicePixelRatio)
                                fillMode: Image.Stretch
                                asynchronous: true
                                smooth: true
                            }

                            IconGlyph {
                                anchors.centerIn: parent
                                visible: kindButton.strip.length === 0
                                glyph: Theme.icons.chevronsRight
                                iconColor: Theme.c.themedComboBox.icon
                            }
                        }

                        Text {
                            width: parent.width - kindThumb.width - kindChevron.width - parent.spacing * 2
                            anchors.verticalCenter: parent.verticalCenter
                            text: kindButton.kindInfo.label || kindButton.kindInfo.kind || ""
                            elide: Text.ElideRight
                            color: Theme.c.themedComboBox.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeSm
                        }

                        IconGlyph {
                            id: kindChevron
                            anchors.verticalCenter: parent.verticalCenter
                            glyph: Theme.icons.chevronDown
                            iconColor: Theme.c.themedComboBox.icon
                        }
                    }

                    MouseArea {
                        id: kindMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: kindButton.openPicker()
                    }

                    TransitionPickerPopup {
                        id: kindPicker
                    }
                }
            }

            Column {
                width: parent.width
                spacing: 4
                Text {
                    text: qsTr("Duration")
                    SearchAnchor {
                        searchId: "transition-duration"
                        title: qsTr("Transition duration")
                        keywords: "length"
                    }
                    color: Theme.c.transitionInspector.foregroundMuted
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

            // Remaps progress across the transition window. "Custom" hands off to the same
            // curve editor the clip fades use, scoped to this transition.
            Column {
                width: parent.width
                spacing: 4
                Text {
                    text: qsTr("Curve")
                    SearchAnchor {
                        searchId: "transition-curve"
                        title: qsTr("Transition curve")
                        keywords: "easing"
                    }
                    color: Theme.c.transitionInspector.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
                CurveField {
                    id: transitionCurveBox
                    width: parent.width
                    domain: "fade"
                    curve: root.activeTransition.easingCurve || "linear"
                    handles: root.activeTransition.easingHandles || []
                    onCurveChosen: (curve, handles) => {
                        if (!root.hasActiveTransition)
                            return
                        EditorState.setTransitionEasing(
                                    root.transitionEditTrack, root.activeTransition.id, curve, handles)
                        if (curve === "custom")
                            ShellBridge.main.openTransitionCurve(
                                        root.transitionEditTrack, root.activeTransition.id)
                    }
                }
            }

            // Shader parameters declared by the active transition package.
            // Integer model: preview ticks replace params as a new list; a count
            // model keeps the pressed slider alive across those updates.
            // An audio track only hears the transition, so its picture parameters don't apply.
            Repeater {
                model: root.activeTransition.audioOnly ? 0 : (root.activeTransition.params || []).length
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
                            color: Theme.c.transitionInspector.foregroundMuted
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
                            color: Theme.c.transitionInspector.foreground
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
                        lockWhilePlaying: true
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
