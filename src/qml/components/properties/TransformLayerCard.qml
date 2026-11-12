import QtQuick
import QtQuick.Controls.Basic
import Drift
// Top of the Transform tab for a transform clip: what the layer covers, and the clips it is
// moving right now.
Column {
    id: root

    property var clipData: ({})
    property int revision: 0

    readonly property int layerTrack: EditorState.selectedTrack
    readonly property var tracks: EditorState.tracks
    readonly property var options: {
        void revision
        void tracks
        return layerTrack >= 0 ? EditorState.transformSpanOptions(layerTrack) : []
    }
    readonly property var liveChildren: {
        void revision
        void EditorState.playheadSeconds
        const out = []
        const covered = layerTrack >= 0 ? EditorState.transformLayerCoveredClips(layerTrack, EditorState.selectedClip) : []
        const t = EditorState.playheadSeconds
        for (let i = 0; i < covered.length; ++i) {
            const clip = (tracks[covered[i].track].clips || [])[covered[i].clip]
            if (clip && t >= clip.start && t < clip.start + clip.duration)
                out.push({ "track": covered[i].track, "clip": covered[i].clip, "name": clip.name })
        }
        return out
    }
    readonly property int shownChildren: 6

    spacing: Theme.spacingMd

    function trackTypeLabel(track) {
        if (track.isTransformLayer) return qsTr("Transform")
        switch (track.type) {
        case "audio": return qsTr("Audio")
        case "text": return qsTr("Text")
        case "subtitle": return qsTr("Subtitle")
        case "shape": return qsTr("Graphic")
        case "adjustment": return qsTr("Adjustment")
        }
        return qsTr("Video")
    }

    // Same fallback the track headers show: a custom name, else type and the track's number.
    function trackName(index) {
        const track = tracks[index]
        if (!track)
            return ""
        if (track.name && track.name.length > 0)
            return track.name
        if (track.number > 0)
            return trackTypeLabel(track) + " " + track.number
        let ordinal = 0
        for (let i = 0; i <= index; ++i) {
            if (tracks[i].type === track.type && !!tracks[i].isTransformLayer === !!track.isTransformLayer)
                ordinal++
        }
        return trackTypeLabel(track) + " " + Math.max(1, ordinal)
    }

    function optionLabel(option) {
        if (option.kind === "all")
            return qsTr("Everything below")
        if (option.kind === "only")
            return qsTr("%1 only").arg(trackName(option.endIndex))
        return qsTr("%1 to %2").arg(trackName(option.firstIndex)).arg(trackName(option.endIndex))
    }

    Connections {
        target: EditorState
        function onTracksChanged() { root.revision++ }
        function onSelectionChanged() { root.revision++ }
    }

    Text {
        width: parent.width
        wrapMode: Text.WordWrap
        text: qsTr("Moves, scales, turns and fades every track under it as one. Each clip keeps its own transform inside the group.")
        color: Theme.c.transformLayerCard.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    Text {
        text: qsTr("Covers")
        color: Theme.c.transformLayerCard.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
        font.weight: Font.Medium
    }

    ThemedComboBox {
        width: parent.width
        enabled: root.options.length > 0
        model: root.options.map((o) => root.optionLabel(o))
        currentIndex: {
            for (let i = 0; i < root.options.length; ++i) {
                if (root.options[i].current)
                    return i
            }
            return -1
        }
        displayText: currentIndex >= 0 ? currentText : qsTr("Nothing")
        onActivated: (index) => EditorState.setTransformSpan(root.layerTrack, root.options[index].endId)
    }

    Text {
        text: qsTr("At the playhead")
        color: Theme.c.transformLayerCard.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
        font.weight: Font.Medium
    }

    Text {
        visible: root.liveChildren.length === 0
        width: parent.width
        wrapMode: Text.WordWrap
        text: qsTr("No covered clip plays here.")
        color: Theme.c.transformLayerCard.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    Flow {
        width: parent.width
        spacing: 6
        visible: root.liveChildren.length > 0

        Repeater {
            model: root.liveChildren.slice(0, root.shownChildren)
            delegate: ThemedChip {
                required property var modelData
                text: modelData.name || qsTr("Clip")
                onClicked: EditorState.selectClip(modelData.track, modelData.clip)
            }
        }
        ThemedChip {
            visible: root.liveChildren.length > root.shownChildren
            text: qsTr("+%n more", "", root.liveChildren.length - root.shownChildren)
            onClicked: EditorState.selectTransformChildren(root.layerTrack, EditorState.selectedClip)
        }
    }
}
