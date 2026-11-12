import QtQuick
import Drift

// Desktop header content: name and the main toggles on the first line, secondary toggles
// (mic, channel lanes) on a second line when the row is tall enough.
// Toggles sit in fixed-width slots, right-aligned, so each kind lines up down the column.
Item {
    id: ctl

    required property var row
    signal overflowRequested(Item anchor)

    readonly property real slotWidth: 20
    readonly property real padRight: 6
    readonly property real nameLeft: row.laneCount > 0 ? 24 + laneToggle.width + 2 : 24
    readonly property real nameMin: 28

    readonly property var fit: {
        const wants = [true, row.wantSolo, row.wantMute, row.wantHide]
        const secondary = [row.wantMic, row.wantLanes]
        const cap = Math.max(0, Math.floor((row.width - nameLeft - padRight - nameMin) / slotWidth))
        const secondaryCols = row.twoLine
            ? Math.min(2, Math.max(0, Math.floor((row.width - nameLeft - padRight) / slotWidth))) : 0
        let overflow = !row.twoLine && secondary.some(w => w)
        for (let i = 0; i < 2; ++i) {
            if (row.twoLine && secondary[i] && i < 2 - secondaryCols)
                overflow = true
        }
        let cols = Math.min(4, Math.max(0, cap - (overflow ? 1 : 0)))
        for (let i = 0; i < 4; ++i) {
            if (wants[i] && i < 4 - cols)
                overflow = true
        }
        if (overflow)
            cols = Math.min(4, Math.max(0, cap - 1))
        return { overflow: overflow, cols: cols, secondaryCols: secondaryCols }
    }

    TrackHeaderLaneToggle {
        id: laneToggle
        visible: ctl.row.laneCount > 0
        x: 24
        y: ctl.row.blockTop
        height: ctl.row.line1Height
        row: ctl.row
    }

    TrackHeaderName {
        x: ctl.nameLeft
        y: ctl.row.blockTop
        height: ctl.row.line1Height
        width: Math.max(0, line1.x - x - Theme.spacingSm)
        label: ctl.row.trackDisplayName
        trackIndex: ctl.row.index
        customName: ctl.row.column.tracks[ctl.row.index].name || ""
        excludedFromExport: ctl.row.trackExcludedFromExport
        dimmed: ctl.row.trackHidden
        opacity: ctl.row.trackHidden ? 0.55 : 1
    }

    Row {
        id: line1
        anchors.right: parent.right
        anchors.rightMargin: ctl.padRight
        y: ctl.row.blockTop
        height: ctl.row.line1Height

        Item {
            visible: ctl.fit.overflow
            width: ctl.slotWidth
            height: line1.height
            TrackHeaderOverflowButton {
                id: overflowButton
                anchors.horizontalCenter: parent.horizontalCenter
                shown: true
                onActivated: ctl.overflowRequested(overflowButton)
            }
        }
        Item {
            visible: ctl.fit.cols >= 4
            width: ctl.slotWidth
            height: line1.height
            TrackHeaderLockButton {
                trackIndex: ctl.row.index
                trackLocked: ctl.row.trackLocked
            }
        }
        Item {
            visible: ctl.fit.cols >= 3
            width: ctl.slotWidth
            height: line1.height
            TrackHeaderSoloButton {
                shown: ctl.row.wantSolo
                trackIndex: ctl.row.index
                trackSolo: ctl.row.trackSolo
            }
        }
        Item {
            visible: ctl.fit.cols >= 2
            width: ctl.slotWidth
            height: line1.height
            TrackHeaderMuteButton {
                anchors.horizontalCenter: parent.horizontalCenter
                shown: ctl.row.wantMute
                trackIndex: ctl.row.index
                trackMuted: ctl.row.trackMuted
            }
        }
        Item {
            visible: ctl.fit.cols >= 1
            width: ctl.slotWidth
            height: line1.height
            TrackHeaderHideButton {
                anchors.horizontalCenter: parent.horizontalCenter
                shown: ctl.row.wantHide
                trackIndex: ctl.row.index
                trackHidden: ctl.row.trackHidden
                hideLabel: ctl.row.hideLabel
            }
        }
    }

    Row {
        id: line2
        visible: ctl.row.twoLine
        anchors.right: parent.right
        anchors.rightMargin: ctl.padRight
        y: ctl.row.blockTop + ctl.row.line1Height
        height: ctl.row.line2Height

        Item {
            visible: ctl.fit.secondaryCols >= 2
            width: ctl.slotWidth
            height: line2.height
            TrackHeaderMicButton {
                anchors.horizontalCenter: parent.horizontalCenter
                trackIndex: ctl.row.index
                shown: ctl.row.wantMic
            }
        }
        Item {
            visible: ctl.fit.secondaryCols >= 1
            width: ctl.slotWidth
            height: line2.height
            TrackHeaderChannelLanesButton {
                anchors.horizontalCenter: parent.horizontalCenter
                column: ctl.row.column
                trackIndex: ctl.row.index
                shown: ctl.row.wantLanes
            }
        }
    }
}
