import QtQuick
import QtQuick.Controls.Basic
import Drift

// Dialog allowing the user to select which attributes from the copied clip
// (transform/motion, speed, volume, video/audio effects, transitions)
// are pasted onto the selected clips in a single undo step.
ThemedDialog {
    id: root

    title: qsTr("Paste Attributes")
    preferredWidth: Theme.dialogWidthMd
    acceptText: qsTr("Paste")
    rejectText: qsTr("Cancel")

    property var clipAttrs: ({})
    property bool pasteTransform: true
    property bool pasteSpeed: false
    property bool pasteVolume: false
    property bool pasteEffects: true
    property bool pasteAudioEffects: false
    property bool pasteTransitions: false
    property bool replaceEffects: false

    readonly property bool hasAnySelected: pasteTransform || pasteSpeed || pasteVolume
                                           || pasteEffects || pasteAudioEffects || pasteTransitions

    showAccept: hasAnySelected

    function openDialog() {
        clipAttrs = EditorState.clipboardAttributes()
        if (!clipAttrs || !clipAttrs.hasClip)
            return

        pasteTransform = (clipAttrs.hasTransform !== false)
        pasteSpeed = (clipAttrs.hasSpeed === true)
        pasteVolume = false
        pasteEffects = (clipAttrs.hasEffects === true)
        pasteAudioEffects = (clipAttrs.hasAudioEffects === true)
        pasteTransitions = (clipAttrs.hasTransitions === true)
        replaceEffects = false

        open()
    }

    function selectAll() {
        if (clipAttrs.hasTransform !== false)
            pasteTransform = true
        if (clipAttrs.hasSpeed !== false)
            pasteSpeed = true
        if (clipAttrs.isAudioOrVideo !== false)
            pasteVolume = true
        if (clipAttrs.hasEffects === true)
            pasteEffects = true
        if (clipAttrs.hasAudioEffects === true)
            pasteAudioEffects = true
        if (clipAttrs.hasTransitions === true)
            pasteTransitions = true
    }

    function selectNone() {
        pasteTransform = false
        pasteSpeed = false
        pasteVolume = false
        pasteEffects = false
        pasteAudioEffects = false
        pasteTransitions = false
    }

    onAccepted: {
        EditorState.pasteAttributes({
            "transform": root.pasteTransform,
            "speed": root.pasteSpeed,
            "volume": root.pasteVolume,
            "effects": root.pasteEffects,
            "audioEffects": root.pasteAudioEffects,
            "transitions": root.pasteTransitions,
            "replaceEffects": root.replaceEffects
        })
    }

    contentItem: Column {
        width: parent ? parent.width : Theme.dialogWidthMd
        spacing: Theme.spacingLg

        // Context header
        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
            text: {
                const name = (root.clipAttrs && root.clipAttrs.clipName) ? root.clipAttrs.clipName : qsTr("Clip")
                const targets = (root.clipAttrs && root.clipAttrs.targetClipCount) ? root.clipAttrs.targetClipCount : 1
                return qsTr("Pasting from “%1” onto %n selected clip(s):", "", targets).arg(name)
            }
        }

        // Video Attributes Section
        Column {
            width: parent.width
            spacing: Theme.spacingSm

            ThemedLabel {
                text: qsTr("Video Attributes")
                size: "xs"
                color: Theme.primary
                font.weight: Font.DemiBold
            }

            ThemedCheckBox {
                width: parent.width
                text: qsTr("Transform (motion, position, scale, opacity)")
                checked: root.pasteTransform
                enabled: root.clipAttrs.hasTransform !== false
                onToggled: root.pasteTransform = checked
            }

            ThemedCheckBox {
                width: parent.width
                text: {
                    if (root.clipAttrs.hasSpeed) {
                        const spd = (root.clipAttrs.speed || 1.0).toFixed(2)
                        const rev = root.clipAttrs.reverse ? qsTr(", reverse") : ""
                        const curve = root.clipAttrs.hasSpeedCurve ? qsTr(", speed curve") : ""
                        return qsTr("Speed / Retime (%1x%2%3)").arg(spd).arg(rev).arg(curve)
                    }
                    return qsTr("Speed / Retime")
                }
                checked: root.pasteSpeed
                enabled: root.clipAttrs.isAudioOrVideo !== false
                onToggled: root.pasteSpeed = checked
            }

            ThemedCheckBox {
                id: videoEffectsCheck
                width: parent.width
                text: {
                    const count = root.clipAttrs.effectCount || 0
                    return count > 0
                        ? qsTr("Video Effects (%n effect(s))", "", count)
                        : qsTr("Video Effects (none)")
                }
                checked: root.pasteEffects
                enabled: (root.clipAttrs.effectCount || 0) > 0
                onToggled: root.pasteEffects = checked
            }
        }

        // Audio Attributes Section
        Column {
            width: parent.width
            spacing: Theme.spacingSm

            ThemedLabel {
                text: qsTr("Audio Attributes")
                size: "xs"
                color: Theme.primary
                font.weight: Font.DemiBold
            }

            ThemedCheckBox {
                width: parent.width
                text: qsTr("Volume & Fades (volume keyframes, in/out ramps)")
                checked: root.pasteVolume
                enabled: root.clipAttrs.isAudioOrVideo !== false
                onToggled: root.pasteVolume = checked
            }

            ThemedCheckBox {
                id: audioEffectsCheck
                width: parent.width
                text: {
                    const count = root.clipAttrs.audioEffectCount || 0
                    return count > 0
                        ? qsTr("Audio Effects (%n effect(s))", "", count)
                        : qsTr("Audio Effects (none)")
                }
                checked: root.pasteAudioEffects
                enabled: (root.clipAttrs.audioEffectCount || 0) > 0
                onToggled: root.pasteAudioEffects = checked
            }
        }

        // Transitions Section
        Column {
            width: parent.width
            spacing: Theme.spacingSm
            visible: (root.clipAttrs.transitionCount || 0) > 0

            ThemedLabel {
                text: qsTr("Transitions")
                size: "xs"
                color: Theme.primary
                font.weight: Font.DemiBold
            }

            ThemedCheckBox {
                width: parent.width
                text: qsTr("Transitions (%n transition(s))", "", root.clipAttrs.transitionCount || 0)
                checked: root.pasteTransitions
                onToggled: root.pasteTransitions = checked
            }
        }

        // Options separator
        Rectangle {
            width: parent.width
            height: Theme.borderWidth
            color: Theme.panelBorder
        }

        // Effect Options
        ThemedCheckBox {
            width: parent.width
            text: qsTr("Replace existing effects (instead of appending)")
            checked: root.replaceEffects
            enabled: root.pasteEffects || root.pasteAudioEffects
            onToggled: root.replaceEffects = checked
        }

        // Quick selection buttons
        Row {
            spacing: Theme.spacingMd
            anchors.right: parent.right

            ThemedButton {
                variant: "ghost"
                text: qsTr("Select All")
                onClicked: root.selectAll()
            }

            ThemedButton {
                variant: "ghost"
                text: qsTr("Select None")
                onClicked: root.selectNone()
            }
        }
    }
}
