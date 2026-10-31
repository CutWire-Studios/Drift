import QtQuick
import QtQuick.Controls.Basic
import Drift
// Effects tab of the properties sheet: the clip's applied effects, or the effect browser to add more.
Item {
    id: root

    property var scaffold: null
    property int trackIndex: -1
    property int clipIndex: -1
    property bool showBrowse: false

    Row {
        id: chips
        x: Theme.pagePadding
        height: Theme.controlHeight
        spacing: Theme.spacingSm

        ThemedChip {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Applied")
            variant: "outline"
            selected: !root.showBrowse
            onClicked: root.showBrowse = false
        }

        ThemedChip {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Browse")
            variant: "outline"
            selected: root.showBrowse
            onClicked: root.showBrowse = true
        }
    }

    Flickable {
        id: appliedFlick
        anchors.top: chips.bottom
        anchors.topMargin: Theme.spacingSm
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        visible: !root.showBrowse
        clip: true
        contentWidth: width
        contentHeight: effectsInspector.height + Theme.pagePadding * 2
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar { }

        EffectsInspector {
            id: effectsInspector
            x: Theme.pagePadding
            y: Theme.pagePadding
            width: appliedFlick.width - Theme.pagePadding * 2
            onBrowseEffectsRequested: root.showBrowse = true
        }
    }

    EffectBrowser {
        anchors.top: chips.bottom
        anchors.topMargin: Theme.spacingSm
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.pagePadding
        anchors.rightMargin: Theme.pagePadding
        visible: root.showBrowse
    }
}
