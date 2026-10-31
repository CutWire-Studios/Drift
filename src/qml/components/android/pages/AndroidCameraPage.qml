import QtQuick
import QtQuick.Controls.Basic
import Drift
// Camera tab of the properties sheet.
Item {
    id: root

    property var scaffold: null
    property int trackIndex: -1
    property int clipIndex: -1

    Flickable {
        id: scroll
        anchors.fill: parent
        contentWidth: width
        contentHeight: inspector.height + Theme.pagePadding * 2
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar { }

        CameraInspector {
            id: inspector
            x: Theme.pagePadding
            y: Theme.pagePadding
            width: scroll.width - Theme.pagePadding * 2
        }
    }
}
