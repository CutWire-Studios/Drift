import QtQuick
import Drift
// Text editing: font, style, effects, animation and layout as tabs of one page, so the whole
// session is one edit group. `tab` picks the one it opens on.
Item {
    id: root

    property var scaffold: null
    property int trackIndex: -1
    property int clipIndex: -1
    property string tab: "font"

    AndroidTextTabs {
        anchors.fill: parent
        tab: root.tab
        onTabChanged: root.tab = tab
    }
}
