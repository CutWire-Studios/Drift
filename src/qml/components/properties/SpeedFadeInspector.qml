import QtQuick
import QtQuick.Controls.Basic
import Drift
Item {
    id: root

    property int clipDataRevision: 0
    readonly property var clipData: {
        void clipDataRevision
        return EditorState.selectedClipData
    }
    readonly property bool hasSelection: !!clipData && Object.keys(clipData).length > 0
    readonly property string clipKind: hasSelection ? (clipData.kind || "") : ""
    // Composites (and audio separated from one) read a nested timeline, which the Speed ramp
    // preview and the reverse render cannot decode.
    readonly property bool nestedSource: hasSelection && !!clipData.sequenceId

    // A host that must close its own edit group before the ramp window opens sets rampHandled
    // and does the opening itself on rampRequested.
    property bool rampHandled: false
    signal rampRequested()

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
        spacing: Theme.phone ? Theme.spacingMd : Theme.spacingXl

        Text {
            visible: !Theme.phone
            text: qsTr("Playback speed")
            SearchAnchor {
                searchId: "speed-playback"
                title: qsTr("Playback speed")
                keywords: "fast slow motion"
            }
            color: Theme.touchInput ? Theme.c.speedFadeInspector.foreground : Theme.c.speedFadeInspector.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.touchInput ? Theme.fontSizeSm : Theme.fontSizeXs
            font.weight: Theme.touchInput ? Font.Medium : Font.Normal
        }

        // A ramp supersedes the constant rate outright, so the controls below
        // are meaningless while one is attached.
        property bool hasSpeedCurve: {
            void root.clipDataRevision
            return !!root.clipData.hasSpeedCurve
        }

        Flow {
            id: rampRow
            width: parent.width
            spacing: 6

            SearchAnchor {
                searchId: "speed-custom"
                title: qsTr("Speed ramp…")
                keywords: "ramp curve"
                target: speedCustomButton
            }
            ThemedButton {
                id: speedCustomButton
                visible: !root.nestedSource
                text: qsTr("Speed ramp…")
                variant: "secondary"
                onClicked: root.rampHandled
                           ? root.rampRequested()
                           : ShellBridge.main.openSpeedCurve(EditorState.selectedTrack, EditorState.selectedClip)
            }

            ThemedChip {
                anchors.verticalCenter: parent.verticalCenter
                visible: speedColumn.hasSpeedCurve
                text: qsTr("Speed ramp active — remove")
                selected: true
                onClicked: EditorState.clearClipSpeedCurve(
                               EditorState.selectedTrack, EditorState.selectedClip)
            }
        }

            Row {
                width: parent.width
                spacing: 6
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
                        width: Theme.touchInput ? (parent.width - parent.spacing * 4) / 5 : implicitWidth
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

        InlineTip {
            tipId: "speed.split"
            readonly property var keys: EditorState.shortcutFor("split")
            text: keys.length > 0 && !Theme.touchInput ? qsTr("Split (%1) to speed up only part of a clip").arg(Theme.shortcutDisplay(keys))
                                  : qsTr("Split the clip to speed up only part of it")
        }

        AndroidSliderRow {
            id: speedRow
            visible: Theme.touchInput
            enabled: !speedColumn.hasSpeedCurve && !EditorState.playing
            label: qsTr("Speed")
            from: Math.log(0.25)
            to: Math.log(100)
            stepSize: 0.01
            value: Math.log(root.clipData.speed || 1.0)
            valueText: speedColumn.hasSpeedCurve ? qsTr("Ramp") : ""
            valueFormatter: function (v) { return Math.exp(v).toFixed(2) + "×" }
            onMoved: (v) => EditorState.previewSetClipSpeed(
                         EditorState.selectedTrack, EditorState.selectedClip, Math.exp(v))
            onDragBegan: EditorState.beginPreviewDrag(qsTr("Speed changed"))
            onDragEnded: EditorState.commitPreviewDrag()
        }

        ThemedSlider {
            id: speedSlider
            visible: !Theme.touchInput
            label: qsTr("Speed")
            enabled: !speedColumn.hasSpeedCurve && !EditorState.playing
            width: parent.width
            from: Math.log(0.25)
            to: Math.log(100)
            stepSize: 0.01
            valueFormatter: function (v) { return Math.exp(v).toFixed(2) }
            Binding on value {
                when: !speedSlider.pressed
                value: Math.log(root.clipData.speed || 1.0)
            }
            onMoved: EditorState.previewSetClipSpeed(
                         EditorState.selectedTrack, EditorState.selectedClip, Math.exp(value))
            onPressedChanged: {
                if (pressed)
                    EditorState.beginPreviewDrag(qsTr("Speed changed"))
                else
                    EditorState.commitPreviewDrag()
            }
        }

        Text {
            visible: !Theme.touchInput
            text: (speedColumn.hasSpeedCurve
                   ? qsTr("Speed ramp")
                   : (root.clipData.speed || 1).toFixed(2) + "×")
                    + (root.clipData.reverse ? qsTr(" (reversed)") : "")
            color: Theme.c.speedFadeInspector.foregroundMuted
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.fontSizeSm
        }

        SearchAnchor {
            searchId: "speed-reverse"
            title: qsTr("Reverse")
            keywords: "backwards rewind"
            target: speedReverseChip
        }
        ThemedChip {
            id: speedReverseChip
            parent: Theme.phone ? rampRow : speedColumn
            visible: !root.nestedSource
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

        AndroidInfoButton {
            Component.onCompleted: parent = rampRow
            hint: qsTr("Split the clip to speed up only part of it")
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
