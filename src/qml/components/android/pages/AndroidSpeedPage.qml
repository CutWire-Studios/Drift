import QtQuick
import QtQuick.Controls.Basic
import Drift
// Speed tab of the properties sheet, plus the speed-curve window. The curve window opens over the
// preview, so the page applies its edit group first and the curve is a separate step.
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

        SpeedFadeInspector {
            id: inspector
            x: Theme.pagePadding
            y: Theme.pagePadding
            width: scroll.width - Theme.pagePadding * 2
            rampHandled: true
            onRampRequested: {
                const track = root.trackIndex
                const clip = root.clipIndex
                root.scaffold.apply()
                ShellBridge.main.openSpeedCurve(track, clip)
            }
        }
    }
}
