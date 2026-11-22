import QtQuick
import Drift

// Text tabs with an underline on the active one, for switching between the sections of one phone
// page. Fills the width with equal tabs when they fit, scrolls sideways when they do not.
Item {
    id: root

    // [{ id, label }]
    property var tabs: []
    property string currentId: ""
    signal tabSelected(string tabId)

    readonly property real minTabWidth: Theme.androidMinTouchTarget * 1.4
    readonly property real naturalWidth: {
        let sum = 0
        for (let i = 0; i < metrics.count; ++i)
            sum += Math.max(root.minTabWidth, metrics.itemAt(i).implicitWidth + Theme.spacingLg * 2)
        return sum
    }
    readonly property real extraPerTab: tabs.length > 0 ? Math.max(0, (width - naturalWidth) / tabs.length) : 0

    implicitHeight: Theme.controlHeight + Theme.spacingXs
    height: implicitHeight

    Repeater {
        id: metrics
        model: root.tabs
        Text {
            visible: false
            text: modelData.label
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeBase
            font.weight: Font.Medium
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: row.width
        contentHeight: height
        clip: true
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds

        Row {
            id: row

            Repeater {
                model: root.tabs

                Item {
                    id: tab
                    required property var modelData
                    required property int index
                    readonly property bool active: root.currentId === modelData.id
                    width: Math.max(root.minTabWidth, label.implicitWidth + Theme.spacingLg * 2) + root.extraPerTab
                    height: root.height

                    onActiveChanged: if (active) root.reveal(tab)

                    Text {
                        id: label
                        anchors.centerIn: parent
                        text: tab.modelData.label
                        color: tab.active ? Theme.c.androidTabStrip.foregroundActive : Theme.c.androidTabStrip.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeBase
                        font.weight: Font.Medium
                    }

                    Rectangle {
                        anchors.bottom: parent.bottom
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: label.implicitWidth
                        height: Theme.c.androidTabStrip.indicatorHeight
                        radius: height / 2
                        color: Theme.c.androidTabStrip.indicator
                        visible: tab.active
                    }

                    Accessible.role: Accessible.PageTab
                    Accessible.name: tab.modelData.label
                    Accessible.selected: tab.active

                    TapHandler {
                        onTapped: {
                            Haptics.select()
                            root.tabSelected(tab.modelData.id)
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: Theme.c.androidTabStrip.borderWidth
        color: Theme.c.androidTabStrip.border
        z: -1
    }

    function reveal(tab) {
        const x = tab.mapToItem(row, 0, 0).x
        if (x < flick.contentX)
            flick.contentX = x
        else if (x + tab.width > flick.contentX + flick.width)
            flick.contentX = Math.min(x + tab.width - flick.width, Math.max(0, flick.contentWidth - flick.width))
    }
}
