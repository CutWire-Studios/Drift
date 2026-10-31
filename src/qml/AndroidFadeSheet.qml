import QtQuick
import QtQuick.Controls.Basic
import Drift
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

    Flickable {
        id: scroll
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: fadeColumn.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar { }

        Column {
            id: fadeColumn
            x: Theme.pagePadding + root.safeLeft
            width: scroll.width - x - Theme.pagePadding - root.safeRight
            topPadding: Theme.spacingLg
            bottomPadding: Theme.spacingLg
            spacing: Theme.spacingXl

            Repeater {
                model: [
                    { which: "in", label: qsTr("Fade in") },
                    { which: "out", label: qsTr("Fade out") }
                ]

                delegate: AndroidFadeSliderRow {
                    id: fadeRow
                    readonly property bool isIn: modelData.which === "in"
                    width: parent.width
                    current: isIn ? root.fadeIn : root.fadeOut
                    maxFade: root.maxFade
                    formatSeconds: function(v) { return root.formatSeconds(v) }
                    onPreviewBegan: EditorState.beginPreviewDrag(fadeRow.isIn ? qsTr("Fade in") : qsTr("Fade out"))
                    onPreviewEnded: EditorState.commitPreviewDrag()
                    onMoved: (value) => {
                        if (fadeRow.isIn)
                            root.setFades(value, root.fadeOut)
                        else
                            root.setFades(root.fadeIn, value)
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
}
