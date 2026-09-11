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
    readonly property var selectedEffects: {
        void clipDataRevision
        return EditorState.selectedClipEffects
    }
    readonly property bool hasChromaKey: root.chromaEffectIndex >= 0
    readonly property int chromaEffectIndex: {
        const effects = selectedEffects || []
        for (let i = 0; i < effects.length; i++) {
            if (effects[i].catalogId === "key.chroma")
                return i
        }
        return -1
    }

    property string selectedKeyColorHex: "#00FF00"

    height: contentCol.height
    implicitHeight: contentCol.height

    function hexToHue(hex) {
        const clean = ("" + hex).replace("#", "")
        const num = parseInt(clean.substring(0, 6), 16)
        if (isNaN(num))
            return 120
        const r = ((num >> 16) & 0xff) / 255
        const g = ((num >> 8) & 0xff) / 255
        const b = (num & 0xff) / 255
        const max = Math.max(r, g, b)
        const min = Math.min(r, g, b)
        const d = max - min
        if (d === 0)
            return 0
        let h
        if (max === r)
            h = ((g - b) / d + (g < b ? 6 : 0)) / 6
        else if (max === g)
            h = ((b - r) / d + 2) / 6
        else
            h = ((r - g) / d + 4) / 6
        return h * 360
    }

    function hueToHex(hue) {
        const h = (hue / 360) * 6
        const i = Math.floor(h) % 6
        const f = h - Math.floor(h)
        const p = 0
        const q = 1 - f
        const t = f
        let r, g, b
        switch (i) {
            case 0: r = 1; g = t; b = p; break
            case 1: r = q; g = 1; b = p; break
            case 2: r = p; g = 1; b = t; break
            case 3: r = p; g = q; b = 1; break
            case 4: r = t; g = p; b = 1; break
            default: r = 1; g = p; b = q; break
        }
        const toByte = function (v) {
            const n = Math.round(Math.max(0, Math.min(1, v)) * 255).toString(16)
            return n.length === 1 ? "0" + n : n
        }
        return "#" + toByte(r) + toByte(g) + toByte(b)
    }

    function findCurrentKeyHue() {
        if (root.chromaEffectIndex < 0)
            return 120
        const effect = (root.selectedEffects || [])[root.chromaEffectIndex]
        const params = (effect && effect.params) || []
        for (let i = 0; i < params.length; i++) {
            if (params[i].key === "u_keyHue")
                return Number(params[i].value) || 120
        }
        return 120
    }

    Component.onCompleted: {
        root.selectedKeyColorHex = root.hueToHex(root.findCurrentKeyHue())
    }

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
            text: qsTr("Pick the background colour to remove, then click Chroma to apply the key.")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        Row {
            width: parent.width
            spacing: 8
            visible: root.hasSelection

            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 108
                text: qsTr("Background colour")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                font.weight: Font.Medium
            }

            ColorSwatchField {
                id: colorSwatch
                anchors.verticalCenter: parent.verticalCenter
                hex: root.selectedKeyColorHex
                tooltip: qsTr("Pick the background colour")
                onEdited: value => {
                    root.selectedKeyColorHex = value
                    if (root.hasChromaKey) {
                        const hue = root.hexToHue(value)
                        EditorState.setEffectParam(
                            EditorState.selectedTrack, EditorState.selectedClip,
                            root.chromaEffectIndex, "u_keyHue", hue)
                    }
                }
            }
        }

        ThemedButton {
            width: parent.width
            visible: root.hasSelection
            enabled: root.hasSelection
            text: qsTr("Chroma")
            variant: root.hasChromaKey ? "secondary" : "primary"
            onClicked: {
                if (root.hasChromaKey)
                    return
                const newIndex = (EditorState.selectedClipEffects || []).length
                EditorState.addEffect(
                    EditorState.selectedTrack, EditorState.selectedClip, "key.chroma")
                const hue = root.hexToHue(root.selectedKeyColorHex)
                EditorState.setEffectParam(
                    EditorState.selectedTrack, EditorState.selectedClip,
                    newIndex, "u_keyHue", hue)
            }
        }

        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            visible: !root.hasSelection
            text: qsTr("Select a clip to use the chroma key.")
            color: Theme.warning
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }
}
