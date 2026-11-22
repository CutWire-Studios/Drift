import QtQuick
import Drift

// Toggle stacked per-channel waveforms on a track that has more than one channel.
IconGlyph {
    id: channelLanesToggle

    property var column
    property int trackIndex: -1
    property bool shown: false
    readonly property bool lanesOn: column.tracks[trackIndex].showChannelWaveforms === true
    // trackMaxChannelCount has no notify of its own and the count only lands
    // once a block has decoded, so depend on `tracks` to re-evaluate then.
    readonly property int laneChannels: {
        const dep = column.tracks.length
        return EditorState.trackMaxChannelCount(trackIndex)
    }

    visible: shown && laneChannels > 1
    glyph: Theme.icons.split
    iconSize: 16
    iconColor: lanesOn ? Theme.c.trackHeaderChannelLanesButton.accent : Theme.c.trackHeaderChannelLanesButton.foregroundMuted
    anchors.verticalCenter: parent.verticalCenter

    Behavior on iconColor {
        ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }

    ThemedToolTip {
        visible: channelLanesMouse.containsMouse
        text: channelLanesToggle.lanesOn
              ? qsTr("Show one combined waveform")
              : qsTr("Show each channel separately (%1)").arg(channelLanesToggle.laneChannels)
    }

    MouseArea {
        id: channelLanesMouse
        anchors.fill: parent
        anchors.leftMargin: Theme.touchInput ? -5 : -4
        anchors.rightMargin: Theme.touchInput ? -5 : -4
        anchors.topMargin: Theme.touchInput ? -14 : -4
        anchors.bottomMargin: Theme.touchInput ? -14 : -4
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            const on = !channelLanesToggle.lanesOn
            Haptics.toggle(on)
            EditorState.setTrackShowChannelWaveforms(channelLanesToggle.trackIndex, on)
            if (on)
                channelLanesToggle.column.growRowForChannelLanes(
                    channelLanesToggle.trackIndex, channelLanesToggle.laneChannels)
        }
    }
}
