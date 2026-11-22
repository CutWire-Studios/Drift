import QtQuick
import QtQuick.Controls.Basic
import Drift
// Motion (vector) tab of the properties sheet.
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

        EmptyState {
            x: Theme.pagePadding
            y: Theme.pagePadding
            width: scroll.width - Theme.pagePadding * 2
            visible: !inspector.hasVector
            glyph: Theme.icons.layers
            title: qsTr("Not an animation")
            hint: qsTr("Motion settings apply to Lottie and SVG clips.")
        }

        VectorInspector {
            id: inspector
            visible: hasVector
            x: Theme.pagePadding
            y: Theme.pagePadding
            width: scroll.width - Theme.pagePadding * 2
        }
    }
}
