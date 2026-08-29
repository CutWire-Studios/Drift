import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

Item {
    id: root

    property int clipDataRevision: 0
    readonly property var clipData: {
        void clipDataRevision
        return EditorState.selectedClipData
    }
    readonly property bool hasSelection: !!clipData && Object.keys(clipData).length > 0
    readonly property string clipKind: hasSelection ? (clipData.kind || "") : ""
    readonly property bool stabilizing: !!(clipData && clipData.stabilizing)
    readonly property bool stabilized: !!(clipData && clipData.stabilized)
    readonly property bool stale: !!(clipData && clipData.stabilizeStale)
    readonly property string stabilizeMode: (clipData && clipData.stabilizeMode)
                                            ? clipData.stabilizeMode : "bake"
    readonly property bool keyframeMode: root.stabilizeMode === "keyframes"
    readonly property var modeIds: ["bake", "keyframes"]
    readonly property var modeLabels: [qsTr("Bake a new video"), qsTr("Animate with keyframes")]
    readonly property string actionLabel: {
        if (root.stabilized && root.stale)
            return qsTr("Update stabilization")
        if (root.keyframeMode) {
            if (root.stabilized)
                return qsTr("Re-apply keyframes")
            return qsTr("Stabilize with keyframes")
        }
        if (root.stabilized)
            return qsTr("Re-stabilize video")
        return qsTr("Stabilize video")
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

        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            text: root.keyframeMode
                  ? qsTr("Smooths camera shake by writing position keyframes. Linear pans stay as two keys far apart; only direction changes get extra keys. Changing smoothness or tripod does not update the preview until you apply.")
                  : qsTr("Smooths camera shake. Drift scans the clip once, then renders a new video. Changing smoothness or tripod does not update the preview until you apply.")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        Column {
            width: parent.width
            spacing: Theme.spacingSm

            Text {
                text: qsTr("Mode")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                font.weight: Font.Medium
            }

            ThemedComboBox {
                width: parent.width
                model: root.modeLabels
                enabled: !root.stabilizing
                currentIndex: Math.max(0, root.modeIds.indexOf(root.stabilizeMode))
                tooltip: qsTr("Bake a new file, or animate the clip with sparse transform keys")
                onActivated: (index) => {
                    EditorState.setClipStabilizeMode(
                                EditorState.selectedTrack, EditorState.selectedClip,
                                root.modeIds[index])
                }
            }
        }

        Column {
            width: parent.width
            spacing: Theme.spacingSm

            Text {
                text: qsTr("Smoothing")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                font.weight: Font.Medium
            }

            ThemedSlider {
                id: stabilizeSmoothingSlider
                label: qsTr("Smoothing")
                width: parent.width
                from: 2
                to: 60
                stepSize: 1
                enabled: !root.stabilizing
                valueFormatter: function (v) { return String(Math.round(v)) }
                Binding on value {
                    when: !stabilizeSmoothingSlider.pressed
                    value: (root.clipData && root.clipData.stabilizeSmoothing !== undefined)
                           ? root.clipData.stabilizeSmoothing : 15
                }
                onPressedChanged: {
                    if (!pressed && root.clipData) {
                        EditorState.setClipStabilizeSmoothing(
                             EditorState.selectedTrack, EditorState.selectedClip, value)
                    }
                }
            }

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("How many frames the smoother looks ahead and behind. Higher values hide more shake but crop the picture more.")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
        }

        Column {
            width: parent.width
            spacing: Theme.spacingSm

            ThemedSwitch {
                text: qsTr("Tripod mode")
                checked: root.clipData ? !!root.clipData.stabilizeTripod : false
                enabled: !root.stabilizing
                onToggled: EditorState.setClipStabilizeTripod(
                               EditorState.selectedTrack, EditorState.selectedClip, checked)
            }

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("Locks the framing as if the camera were on a tripod. Crops more aggressively than smoothing alone.")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
        }

        Row {
            width: parent.width
            spacing: Theme.spacingSm
            visible: root.stale && !root.stabilizing

            IconGlyph {
                glyph: Theme.icons.warning
                iconSize: Theme.iconSizeMd
                iconColor: Theme.warning
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                width: parent.width - Theme.iconSizeMd - parent.spacing
                wrapMode: Text.WordWrap
                text: root.keyframeMode
                      ? qsTr("Position keys still use the last run. Update to apply these settings.")
                      : qsTr("Preview still uses the last run. Update to apply these settings.")
                color: Theme.warning
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        Row {
            width: parent.width
            spacing: 8
            visible: !root.stabilizing

            ThemedButton {
                text: root.actionLabel
                variant: (root.stabilized && !root.stale) ? "secondary" : "primary"
                onClicked: EditorState.stabilizeClip(
                               EditorState.selectedTrack, EditorState.selectedClip)
            }

            ThemedButton {
                text: qsTr("Remove")
                variant: "ghost"
                visible: root.stabilized
                onClicked: EditorState.removeClipStabilization(
                               EditorState.selectedTrack, EditorState.selectedClip)
            }
        }

        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            visible: root.stabilizing
            text: (root.clipData && root.clipData.stabilizeStatus)
                  ? root.clipData.stabilizeStatus
                  : qsTr("Stabilizing…")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        ThemedProgressBar {
            visible: root.stabilizing
            width: parent.width
            value: (root.clipData && root.clipData.stabilizeProgress)
                   ? root.clipData.stabilizeProgress : 0
        }

        ThemedButton {
            visible: root.stabilizing
            width: parent.width
            text: qsTr("Cancel")
            variant: "ghost"
            onClicked: EditorState.cancelClipStabilization(
                           EditorState.selectedTrack, EditorState.selectedClip)
        }
    }
}
