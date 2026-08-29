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

    height: speedColumn.height
    implicitHeight: speedColumn.height

    function refreshFields() {}

    Connections {
        target: EditorState
        function onSelectionChanged() { root.clipDataRevision++ }
        function onSelectedClipDataChanged() { root.clipDataRevision++ }
        function onTracksChanged() { root.clipDataRevision++ }
    }

    Column {
        id: speedColumn
        width: root.width
        spacing: Theme.spacingXl

        EmptyState {
            visible: root.clipKind !== "video" && root.clipKind !== "audio"
            width: parent.width
            compact: true
            glyph: Theme.icons.gauge
            title: qsTr("Not available")
            hint: qsTr("Speed applies to video and audio clips.")
        }

        Text {
            visible: root.clipKind === "video" || root.clipKind === "audio"
            text: qsTr("Playback speed")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        // A ramp supersedes the constant rate outright, so the controls below
        // are meaningless while one is attached.
        property bool hasSpeedCurve: {
            void root.clipDataRevision
            return !!root.clipData.hasSpeedCurve
        }

        Row {
            width: parent.width
            spacing: 6
            visible: root.clipKind === "video" || root.clipKind === "audio"

            ThemedButton {
                text: qsTr("Custom speed…")
                variant: "secondary"
                onClicked: root.Window.window.openSpeedCurve(
                               EditorState.selectedTrack, EditorState.selectedClip)
            }

            ThemedChip {
                anchors.verticalCenter: parent.verticalCenter
                visible: speedColumn.hasSpeedCurve
                text: qsTr("Custom speed active — remove")
                selected: true
                onClicked: EditorState.clearClipSpeedCurve(
                               EditorState.selectedTrack, EditorState.selectedClip)
            }
        }

            Row {
                width: parent.width
                spacing: 6
                visible: root.clipKind === "video" || root.clipKind === "audio"
                Repeater {
                    model: [
                        { label: "0.25×", value: 0.25 },
                        { label: "0.5×", value: 0.5 },
                        { label: "1×", value: 1.0 },
                        { label: "2×", value: 2.0 },
                        { label: "4×", value: 4.0 }
                    ]
                    delegate: ThemedChip {
                        required property var modelData
                        text: modelData.label
                        enabled: !speedColumn.hasSpeedCurve
                        selected: !speedColumn.hasSpeedCurve
                                  && Math.abs((root.clipData.speed || 1) - modelData.value) < 0.01
                        onClicked: EditorState.setClipSpeed(
                                       EditorState.selectedTrack, EditorState.selectedClip,
                                       modelData.value)
                    }
                }
            }

        ThemedSlider {
            id: speedSlider
            label: qsTr("Speed")
            visible: root.clipKind === "video" || root.clipKind === "audio"
            enabled: !speedColumn.hasSpeedCurve
            width: parent.width
            from: 0.25
            to: 4.0
            stepSize: 0.05
            Binding on value {
                when: !speedSlider.pressed
                value: root.clipData.speed || 1.0
            }
            onMoved: EditorState.previewSetClipSpeed(
                         EditorState.selectedTrack, EditorState.selectedClip, value)
            onPressedChanged: {
                if (pressed)
                    EditorState.beginPreviewDrag(qsTr("Speed changed"))
                else
                    EditorState.commitPreviewDrag()
            }
        }

        Text {
            visible: root.clipKind === "video" || root.clipKind === "audio"
            text: (speedColumn.hasSpeedCurve
                   ? qsTr("Custom speed")
                   : (root.clipData.speed || 1).toFixed(2) + "×")
                    + (root.clipData.reverse ? qsTr(" (reversed)") : "")
            color: Theme.mutedForeground
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.fontSizeSm
        }

        ThemedChip {
            visible: root.clipKind === "video" || root.clipKind === "audio"
            text: qsTr("Reverse")
            selected: {
                void root.clipDataRevision
                return !!root.clipData.reverse
            }
            // Turning reverse off is free. Turning it on may need a rendered copy first, which
            // requestClipReverse decides and confirms through ReverseProgressDialog.
            onClicked: {
                if (root.clipData.reverse)
                    EditorState.setClipReverse(EditorState.selectedTrack,
                                               EditorState.selectedClip, false)
                else
                    EditorState.requestClipReverse(EditorState.selectedTrack,
                                                   EditorState.selectedClip)
            }
        }

        // Shown when a reversed video clip has no rendered copy covering it — after a cancelled
        // render, or after a trim pushed the clip past the range that was rendered.
        Row {
            width: parent.width
            spacing: Theme.spacingLg
            visible: {
                void root.clipDataRevision
                void EditorState.reverseRendering
                return root.clipKind === "video" && !!root.clipData.reverse
                       && !EditorState.clipHasReverseProxy(EditorState.selectedTrack,
                                                           EditorState.selectedClip)
            }

            ThemedLabel {
                width: parent.width - renderReversedButton.width - Theme.spacingLg
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Not rendered — playback may stutter")
            }

            ThemedButton {
                id: renderReversedButton
                text: qsTr("Render")
                onClicked: EditorState.requestClipReverse(EditorState.selectedTrack,
                                                          EditorState.selectedClip)
            }
        }
    }
}
