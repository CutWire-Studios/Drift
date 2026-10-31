import QtQuick
import QtQuick.Controls.Basic
import Drift
// Audio tab of the properties sheet, with the clip's edge fades as sliders below it.
Item {
    id: root

    property var scaffold: null
    property int trackIndex: -1
    property int clipIndex: -1

    property int clipDataRevision: 0
    readonly property var clipData: {
        void clipDataRevision
        return EditorState.selectedClipData
    }
    readonly property bool hasClip: !!clipData && clipData.duration !== undefined
    readonly property real clipDuration: hasClip ? clipData.duration : 0
    readonly property real fadeIn: hasClip ? (clipData.fadeIn || 0) : 0
    readonly property real fadeOut: hasClip ? (clipData.fadeOut || 0) : 0
    readonly property real maxFade: Math.max(0.1, Math.min(10, clipDuration))

    Connections {
        target: EditorState
        function onSelectedClipDataChanged() { root.clipDataRevision++ }
        function onClipPropertiesPreviewed() { root.clipDataRevision++ }
        function onSelectionChanged() { root.clipDataRevision++ }
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
        contentWidth: width
        contentHeight: volumeColumn.height + Theme.pagePadding * 2
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar { }

        Column {
            id: volumeColumn
            x: Theme.pagePadding
            y: Theme.pagePadding
            width: scroll.width - Theme.pagePadding * 2
            spacing: Theme.spacing2xl

            AudioInspector {
                width: volumeColumn.width
            }

            Column {
                width: parent.width
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
            }
        }
    }
}
