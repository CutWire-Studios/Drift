import QtQuick
import Drift

// Add/remove-a-key-at-the-playhead diamond for params that keyframe as several channel tracks
// (colour, vec2) or that have no value slider of their own (bool). Solid: a key sits on the
// playhead, so a click removes it. Ghosted: the param animates but no key is here. Grey: no keys.
Item {
    id: root

    property var keyframeList: []
    property string label: ""
    readonly property bool animated: keyframeList && keyframeList.length > 0
    readonly property bool keyHere: {
        if (!animated)
            return false
        const t = EditorState.inspectorPlayheadSeconds
        for (let i = 0; i < keyframeList.length; ++i) {
            if (Math.abs(keyframeList[i].seconds - t) <= 1 / 30)
                return true
        }
        return false
    }

    signal addRequested()
    signal removeRequested()

    width: 16
    height: 16

    Rectangle {
        anchors.centerIn: parent
        width: 9
        height: 9
        rotation: 45
        radius: 2
        color: root.keyHere ? Theme.primary : "transparent"
        border.width: 1.5
        border.color: root.animated ? Theme.primary : Theme.mutedForeground
        opacity: root.animated && !root.keyHere ? 0.6 : 1
    }

    ThemedToolTip {
        visible: hover.hovered
        text: root.keyHere ? qsTr("Remove %1 keyframe at the playhead").arg(root.label)
                           : qsTr("Add %1 keyframe at the playhead").arg(root.label)
    }

    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        onTapped: root.keyHere ? root.removeRequested() : root.addRequested()
    }
}
