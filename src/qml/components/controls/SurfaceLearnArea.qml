import QtQuick
import Drift

// Over a parameter while control-surface learn mode is on: a click arms the parameter, and the
// next control moved on a connected device is bound to it. Give it a `targetId` for a fixed
// parameter, or a `propKey` with its range for a clip property or effect parameter row.
MouseArea {
    id: root

    property string targetId: ""
    property string propKey: ""
    property string label: ""
    property real from: 0
    property real to: 1
    readonly property var surfaces: EditorState.surfaces
    readonly property bool armed: surfaces.learnTarget !== "" && surfaces.learnLabel === label && label !== ""

    anchors.fill: parent
    z: 1000
    visible: surfaces.learnMode
    cursorShape: Qt.PointingHandCursor
    onClicked: {
        if (targetId !== "")
            surfaces.armLearn(targetId, label)
        else
            surfaces.armLearnProperty(propKey, label, from, to)
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.c.surfaceLearn.radius
        color: root.armed ? Theme.c.surfaceLearn.armedFill : Theme.c.surfaceLearn.fill
        border.width: Theme.c.surfaceLearn.borderWidth
        border.color: Theme.c.surfaceLearn.border
    }
}
