import QtQuick
import Drift
import "components"

// The phone's home for a clip's edge fades. On the desktop they are dots on the clip; on touch
// those dots sat a finger's width from the trim handles and one kept being grabbed for the other,
// so here they are sliders instead, the way phone editors do it. Same values, same undo: a drag is
// one preview edit committed on release, exactly as dragging the dot was.
AndroidBottomSheet {
    id: root

    title: qsTr("Fade")
    doneText: qsTr("Done")
    // The point is watching the fade play out on the preview while setting it.
    blocking: false
    sheetHeightFraction: 0.36
    onDoneRequested: root.dismiss()

    property int clipDataRevision: 0
    readonly property var clipData: {
        void clipDataRevision
        return EditorState.selectedClipData
    }
    readonly property bool hasClip: !!clipData && clipData.duration !== undefined
    readonly property real clipDuration: hasClip ? clipData.duration : 0
    readonly property real fadeIn: hasClip ? (clipData.fadeIn || 0) : 0
    readonly property real fadeOut: hasClip ? (clipData.fadeOut || 0) : 0
    // Past a few seconds a fade is a different effect; the cap keeps the slider's travel useful.
    readonly property real maxFade: Math.max(0.1, Math.min(10, clipDuration))

    readonly property var curveIds: ["linear", "smooth", "equalPower", "custom"]
    readonly property var curveLabels: [qsTr("Linear"), qsTr("Smooth"), qsTr("Natural"), qsTr("Custom")]

    Connections {
        target: EditorState
        function onSelectedClipDataChanged() { root.clipDataRevision++ }
        function onClipPropertiesPreviewed() { root.clipDataRevision++ }
        function onSelectionChanged() {
            root.clipDataRevision++
            if (EditorState.selectedTrack < 0 || EditorState.selectedClip < 0)
                root.dismiss()
        }
    }

    function formatSeconds(v) {
        return qsTr("%1 s").arg(Number(v).toFixed(1))
    }

    // Fade in and fade out share the clip: neither may eat into the other.
    function setFades(fadeInSeconds, fadeOutSeconds) {
        const inClamped = Math.max(0, Math.min(fadeInSeconds, root.clipDuration - fadeOutSeconds))
        const outClamped = Math.max(0, Math.min(fadeOutSeconds, root.clipDuration - inClamped))
        EditorState.previewSetClipFade(EditorState.selectedTrack, EditorState.selectedClip,
                                       inClamped, outClamped)
    }

    Column {
        anchors.fill: parent
        anchors.leftMargin: Theme.pagePadding + root.safeLeft
        anchors.rightMargin: Theme.pagePadding + root.safeRight
        anchors.topMargin: Theme.spacingLg
        spacing: Theme.spacingXl

        Repeater {
            model: [
                { which: "in", label: qsTr("Fade in") },
                { which: "out", label: qsTr("Fade out") }
            ]

            delegate: Column {
                id: fadeRow
                required property var modelData
                readonly property bool isIn: modelData.which === "in"
                readonly property real current: isIn ? root.fadeIn : root.fadeOut
                width: parent.width
                spacing: Theme.spacingSm

                Item {
                    width: parent.width
                    height: Math.max(fadeLabel.implicitHeight, fadeValue.implicitHeight)

                    ThemedLabel {
                        id: fadeLabel
                        anchors.left: parent.left
                        text: fadeRow.modelData.label
                        size: "sm"
                    }
                    Text {
                        id: fadeValue
                        anchors.right: parent.right
                        text: root.formatSeconds(fadeSlider.pressed ? fadeSlider.value : fadeRow.current)
                        color: Theme.mutedForeground
                        font.family: Theme.monoFontFamily
                        font.pixelSize: Theme.fontSizeSm
                    }
                }

                ThemedSlider {
                    id: fadeSlider
                    width: parent.width
                    from: 0
                    to: root.maxFade
                    stepSize: 0.1
                    label: fadeRow.modelData.label
                    valueFormatter: root.formatSeconds
                    Binding on value {
                        when: !fadeSlider.pressed
                        value: fadeRow.current
                    }
                    onPressedChanged: {
                        if (pressed)
                            EditorState.beginPreviewDrag(fadeRow.isIn ? qsTr("Fade in") : qsTr("Fade out"))
                        else
                            EditorState.commitPreviewDrag()
                    }
                    onMoved: {
                        if (fadeRow.isIn)
                            root.setFades(value, root.fadeOut)
                        else
                            root.setFades(root.fadeIn, value)
                    }
                }
            }
        }

        Column {
            width: parent.width
            spacing: Theme.spacingSm

            ThemedLabel {
                text: qsTr("Curve")
                size: "sm"
            }

            Flow {
                width: parent.width
                spacing: Theme.spacingSm

                Repeater {
                    model: root.curveIds.length

                    delegate: ThemedChip {
                        required property int index
                        readonly property string curveId: root.curveIds[index]
                        text: root.curveLabels[index]
                        selected: (root.clipData.fadeCurve || "smooth") === curveId
                                  || (curveId === "custom" && root.clipData.fadeCurve === "bezier")
                        onClicked: {
                            Haptics.select()
                            if (curveId === "custom") {
                                root.hostWindow.openFadeCurve(EditorState.selectedTrack,
                                                              EditorState.selectedClip)
                                return
                            }
                            EditorState.setClipFadeCurve(EditorState.selectedTrack,
                                                         EditorState.selectedClip, curveId)
                        }
                    }
                }
            }
        }
    }
}
