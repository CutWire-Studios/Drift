import QtQuick
import Drift

// Tree row for one nested adjustment lane, level with that lane's strip below the parent's clips.
Item {
    id: laneRow

    required property var row
    required property var modelData
    required property int index
    required property int count

    readonly property var column: row.column
    readonly property string kind: {
        const dep = column.tracks
        return EditorState.adjustmentLaneKind(modelData)
    }
    readonly property bool laneHidden: !!column.tracks[modelData] && column.tracks[modelData].hidden === true
    readonly property int clipCount: {
        const t = column.tracks[modelData]
        return t && t.clips ? t.clips.length : 0
    }
    readonly property bool last: index === count - 1
    readonly property color kindColor: kind === "audioEffects" ? Theme.c.trackHeaderLaneRow.audio
                                       : kind === "mask" ? Theme.c.trackHeaderLaneRow.mask
                                       : Theme.c.trackHeaderLaneRow.video
    readonly property string kindLabel: kind === "audioEffects" ? qsTr("Audio FX")
                                        : kind === "mask" ? qsTr("Mask") : qsTr("Effects")
    readonly property string kindGlyph: kind === "audioEffects" ? Theme.icons.audioLines
                                        : kind === "mask" ? Theme.icons.mask : Theme.icons.sparkles

    readonly property real indent: 26

    width: row.width
    height: Theme.adjustmentLaneHeight

    Rectangle {
        x: laneRow.indent
        width: 1
        height: laneRow.last ? parent.height / 2 : parent.height
        color: Theme.c.trackHeaderLaneRow.connector
    }
    Rectangle {
        x: laneRow.indent
        y: Math.floor(parent.height / 2)
        width: 9
        height: 1
        color: Theme.c.trackHeaderLaneRow.connector
    }

    IconGlyph {
        x: laneRow.indent + 13
        anchors.verticalCenter: parent.verticalCenter
        glyph: laneRow.kindGlyph
        iconSize: 12
        iconColor: laneRow.kindColor
        opacity: laneRow.laneHidden ? 0.45 : 1
    }

    Text {
        x: laneRow.indent + 30
        anchors.verticalCenter: parent.verticalCenter
        width: Math.max(0, eye.x - x - Theme.spacingSm)
        text: laneRow.clipCount > 1 ? laneRow.kindLabel + " (" + laneRow.clipCount + ")" : laneRow.kindLabel
        color: Theme.c.trackHeaderLaneRow.foreground
        opacity: laneRow.laneHidden ? 0.45 : 1
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
        elide: Text.ElideRight
    }

    TrackHeaderLaneEye {
        id: eye
        x: laneRow.width - 24
        column: laneRow.column
        modelData: laneRow.modelData
    }
}
