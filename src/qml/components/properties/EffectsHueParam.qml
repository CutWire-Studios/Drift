import QtQuick
import Drift

Row {
    property var row
    property int effectIndex: -1
    property var inspector
    // Sibling params of the same effect. Chroma key's u_keyMode/u_keyLuma let a grey pick
    // switch to a brightness key instead of being dropped (#605).
    property var effectParams: []
    id: hueRow
    visible: row.paramData.type === "float"
             && row.paramData.hue === true
    width: parent.width
    spacing: 8

    function currentHue() {
        const deg = sliderValue(row.paramData)
        return isNaN(deg) ? 0 : deg
    }

    function sibling(key) {
        return (effectParams || []).find(p => p.key === key)
    }

    function sliderValue(data) {
        const keys = (data.keyframes && data.keyframes.points) || []
        return keys.length === 0
            ? Number(data.value)
            : EditorState.propertyValueAt(
                  EditorState.selectedTrack, EditorState.selectedClip,
                  data.prop, EditorState.playheadSeconds, data.value)
    }

    function swatchHex() {
        const mode = sibling("u_keyMode")
        const luma = sibling("u_keyLuma")
        if (mode && luma && Math.round(Number(mode.value)) === 1) {
            const v = sliderValue(luma)
            return Qt.rgba(v, v, v, 1).toString()
        }
        return inspector.hueToHex(currentHue())
    }

    Text {
        width: parent.width - 148
        elide: Text.ElideRight
        text: qsTr("Pick %1").arg(row.paramData.label)
        color: Theme.c.effectsHueParam.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
        anchors.verticalCenter: parent.verticalCenter
    }
    ColorSwatchField {
        anchors.verticalCenter: parent.verticalCenter
        hex: hueRow.swatchHex()
        tooltip: qsTr("Choose %1").arg(row.paramData.label)
        onEyedropperStarted: inspector.bypassForEyedropper(effectIndex)
        onEyedropperEnded: EditorState.cancelPreviewDrag()
        onEdited: value => {
            // The hex field re-emits its own value on focus-out, which must not become an undo step.
            if (value.toLowerCase() === hueRow.swatchHex().toLowerCase())
                return
            const deg = inspector.hexToHue(value)
            const mode = hueRow.sibling("u_keyMode")
            const luma = hueRow.sibling("u_keyLuma")
            const toBrightness = isNaN(deg)
            if (toBrightness && !(mode && luma))
                return
            if (!toBrightness && Math.abs(deg - hueRow.currentHue()) < 0.01
                    && !(mode && Math.round(Number(mode.value)) === 1))
                return
            const t = EditorState.playheadSeconds
            EditorState.beginPreviewDrag(
                qsTr("Edit %1").arg(row.paramData.label))
            if (mode)
                EditorState.previewSetClipKeyframe(
                    EditorState.selectedTrack, EditorState.selectedClip,
                    mode.prop, t, toBrightness ? 1 : 0)
            if (toBrightness) {
                const c = Qt.lighter(value, 1)
                EditorState.previewSetClipKeyframe(
                    EditorState.selectedTrack, EditorState.selectedClip,
                    luma.prop, t, 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b)
            } else {
                EditorState.previewSetClipKeyframe(
                    EditorState.selectedTrack, EditorState.selectedClip,
                    row.paramData.prop, t, deg)
            }
            EditorState.commitPreviewDrag()
        }
    }
}
