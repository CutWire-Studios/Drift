import QtQuick
import QtQuick.Shapes
import Drift

// Where the timeline has been baked, along the bottom of the seek strip. A stale bake still
// plays; the hatch is the cue that it no longer matches the edit.
Item {
    id: strip

    property var panel

    signal manageRequested()

    height: Theme.timelineBakeStripHeight

    function stateLabel(bake) {
        return bake.state === "baking" ? qsTr("Baking %1%").arg(Math.round(bake.progress * 100))
               : bake.state === "stale" ? qsTr("Stale") : qsTr("Fresh")
    }

    Repeater {
        model: EditorState.bakes.bakes
        delegate: Item {
            id: seg
            required property var modelData
            readonly property bool baking: modelData.state === "baking"
            readonly property bool stale: modelData.state === "stale"

            x: modelData.start * strip.panel.pxPerSecond
            width: Math.max(2, (modelData.end - modelData.start) * strip.panel.pxPerSecond)
            height: strip.height

            Rectangle {
                anchors.fill: parent
                radius: Theme.c.timelineBakeStrip.radius
                color: seg.baking ? Theme.c.timelineBakeStrip.track
                       : seg.stale ? Theme.c.timelineBakeStrip.stale
                                   : Theme.c.timelineBakeStrip.fresh
            }

            Rectangle {
                visible: seg.baking
                height: parent.height
                width: parent.width * seg.modelData.progress
                radius: Theme.c.timelineBakeStrip.radius
                color: Theme.c.timelineBakeStrip.progress
            }

            Shape {
                visible: seg.stale
                anchors.fill: parent
                ShapePath {
                    strokeColor: Theme.c.timelineBakeStrip.staleHatch
                    strokeWidth: seg.height
                    fillColor: "transparent"
                    capStyle: ShapePath.FlatCap
                    dashPattern: [0.5, 1]
                    startX: 0
                    startY: seg.height / 2
                    PathLine { x: seg.width; y: seg.height / 2 }
                }
            }

            MouseArea {
                id: segMouse
                anchors.fill: parent
                anchors.topMargin: -2
                hoverEnabled: true
                acceptedButtons: Qt.RightButton
                onClicked: {
                    bakeMenu.bakeId = seg.modelData.id
                    bakeMenu.bakeState = seg.modelData.state
                    bakeMenu.popup()
                }
            }

            ThemedToolTip {
                visible: segMouse.containsMouse
                text: qsTr("%1 – %2 · %3 · %4%5")
                      .arg(strip.panel.formatTime(seg.modelData.start))
                      .arg(strip.panel.formatTime(seg.modelData.end))
                      .arg(qsTr("%1 · %2×%3").arg(seg.modelData.qualityLabel)
                           .arg(seg.modelData.width).arg(seg.modelData.height))
                      .arg(strip.stateLabel(seg.modelData))
                      .arg(seg.modelData.bytes > 0 ? " · " + DownloadFormat.formatBytes(seg.modelData.bytes) : "")
            }
        }
    }

    TimelineBakeContextMenu {
        id: bakeMenu
        onManageRequested: strip.manageRequested()
    }
}
