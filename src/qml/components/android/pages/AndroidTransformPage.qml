import QtQuick
import QtQuick.Controls.Basic
import Drift
// Transform tab of the properties sheet on the phone tool page. The on-preview gizmo edits the same values.
Item {
    id: root

    property var scaffold: null
    property int trackIndex: -1
    property int clipIndex: -1

    Flickable {
        id: scroll
        anchors.fill: parent
        contentWidth: width
        contentHeight: inspector.height + (inspector3d.visible ? inspector3d.implicitHeight + Theme.spacingXl : 0) + Theme.pagePadding * 2
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar { }

        TransformInspector {
            id: inspector
            x: Theme.pagePadding
            y: Theme.pagePadding
            width: scroll.width - Theme.pagePadding * 2
        }

        Transform3dInspector {
            id: inspector3d
            x: Theme.pagePadding
            y: Theme.pagePadding + inspector.height + Theme.spacingXl
            width: scroll.width - Theme.pagePadding * 2
            visible: is3d
        }
    }
}
