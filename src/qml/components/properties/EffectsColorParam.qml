import QtQuick
import Drift

Column {
    property var row
    property int effectIndex: -1
    property var inspector
    property bool procedural: false
    spacing: 4
    width: parent ? parent.width : implicitWidth
    visible: row && row.paramData.type === "color"

    // A shade is picked, not dialled, so colours get the swatch and stay
    // off the keyframe strip — the track type is double all the way down.
    Row {
        id: colorRow
        visible: row.paramData.type === "color"
        width: parent.width
        spacing: 8
        readonly property var colorKeys: (row.paramData.keyframes
                                          && row.paramData.keyframes.points) || []
        // Stored as #rrggbb or #rrggbbaa; Qt reads 8 digits as
        // #aarrggbb, so the colour is rebuilt from channels.
        function channelAt(suffix, fallback) {
            if (colorKeys.length === 0)
                return fallback
            return EditorState.propertyValueAt(
                EditorState.selectedTrack, EditorState.selectedClip,
                row.paramData.prop + suffix,
                EditorState.inspectorPlayheadSeconds, fallback)
        }
        // This row is built for every param type, so the value is often a number
        // or a bool; only a "#..." string is a colour.
        readonly property string staticHex: {
            const v = row.paramData.value
            return typeof v === "string" && v.charAt(0) === "#" ? v : "#ffffff"
        }
        readonly property color staticColor: Qt.color(staticHex.substring(0, 7))
        readonly property real staticAlpha: staticHex.length === 9
            ? parseInt(staticHex.substring(7), 16) / 255 : 1
        readonly property color shownColor: Qt.rgba(
            channelAt(".r", staticColor.r), channelAt(".g", staticColor.g),
            channelAt(".b", staticColor.b), 1)
        readonly property real shownAlpha: channelAt(".a", staticAlpha)
        function hexOf(c) {
            const h = v => ("0" + Math.round(v * 255).toString(16)).slice(-2)
            return "#" + h(c.r) + h(c.g) + h(c.b)
        }
        ChannelKeyButton {
            anchors.verticalCenter: parent.verticalCenter
            keyframeList: colorRow.colorKeys
            label: row.paramData.label
            onAddRequested: EditorState.setClipColorKeyframe(
                                EditorState.selectedTrack, EditorState.selectedClip,
                                row.paramData.prop, EditorState.playheadSeconds,
                                Qt.rgba(colorRow.shownColor.r, colorRow.shownColor.g,
                                        colorRow.shownColor.b, colorRow.shownAlpha))
            onRemoveRequested: EditorState.removeClipChannelKeyframes(
                                   EditorState.selectedTrack, EditorState.selectedClip,
                                   row.paramData.prop, EditorState.playheadSeconds,
                                   [".r", ".g", ".b", ".a"])
        }
        Text {
            width: parent.width - 148 - 24
            elide: Text.ElideRight
            text: row.paramData.label
            color: Theme.c.effectsColorParam.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            anchors.verticalCenter: parent.verticalCenter
        }
        ColorSwatchField {
            anchors.verticalCenter: parent.verticalCenter
            hex: colorRow.hexOf(colorRow.shownColor)
            tooltip: qsTr("Choose %1").arg(row.paramData.label)
            onEyedropperStarted: if (!procedural) inspector.bypassForEyedropper(effectIndex)
            onEyedropperEnded: if (!procedural) EditorState.cancelPreviewDrag()
            onEdited: value => {
                // Animated colours key the playhead, like every other
                // animated param; static ones set the value.
                if (colorRow.colorKeys.length > 0) {
                    EditorState.setClipColorKeyframe(
                        EditorState.selectedTrack, EditorState.selectedClip,
                        row.paramData.prop, EditorState.playheadSeconds,
                        Qt.rgba(Qt.color(value).r, Qt.color(value).g,
                                Qt.color(value).b, colorRow.shownAlpha))
                } else if (procedural) {
                    EditorState.setProceduralColorParam(
                        EditorState.selectedTrack, EditorState.selectedClip,
                        row.paramData.key, value)
                } else {
                    EditorState.setEffectColorParam(
                        EditorState.selectedTrack, EditorState.selectedClip,
                        effectIndex, row.paramData.key, value)
                }
            }
        }
    }

    // Opacity for colours that declare "alpha": the hex above carries
    // only the shade, so the fourth channel gets its own slider.
    ThemedSlider {
        visible: row.paramData.type === "color" && !!row.paramData.alpha
        width: parent.width
        label: qsTr("%1 opacity").arg(row.paramData.label)
        from: 0
        to: 1
        value: colorRow.shownAlpha
        // Animated: the drag previews the playhead key. Static: there is no
        // colour preview stream, so the value commits once on release.
        onPressedChanged: {
            const keyed = colorRow.colorKeys.length > 0
            if (pressed && keyed) {
                EditorState.beginPreviewDrag(
                    qsTr("Edit %1").arg(row.paramData.label))
            } else if (!pressed && keyed) {
                EditorState.commitPreviewDrag()
            } else if (!pressed) {
                const a = ("0" + Math.round(value * 255).toString(16)).slice(-2)
                const hex = colorRow.hexOf(colorRow.staticColor) + a
                if (procedural) {
                    EditorState.setProceduralColorParam(
                        EditorState.selectedTrack, EditorState.selectedClip, row.paramData.key, hex)
                } else {
                    EditorState.setEffectColorParam(
                        EditorState.selectedTrack, EditorState.selectedClip,
                        effectIndex, row.paramData.key, hex)
                }
            }
        }
        onMoved: {
            if (colorRow.colorKeys.length > 0) {
                EditorState.previewSetClipKeyframe(
                    EditorState.selectedTrack, EditorState.selectedClip,
                    row.paramData.prop + ".a", EditorState.playheadSeconds, value)
            }
        }
    }

    // Preset shades a package lists under "swatches" (Face Retouch's lip
    // colours). Picking one is the same edit as the picker above.
    Flow {
        visible: row.paramData.type === "color"
                 && (row.paramData.swatches || []).length > 0
        width: parent.width
        spacing: 6
        Repeater {
            model: row.paramData.type === "color"
                   ? (row.paramData.swatches || []) : []
            delegate: EffectsColorSwatchChip {
                                                current: String(row.paramData.value || "").toLowerCase() === modelData
                                                effectIndex: effectIndex
                                                procedural: procedural
                                                paramKey: row.paramData.key
                                            }
        }
    }
}
