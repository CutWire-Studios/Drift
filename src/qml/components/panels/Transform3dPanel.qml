import QtQuick
import QtQuick.Controls.Basic
import Drift
// The 3D Transform panel: the 3D placement of the selected layer or model, the same inspector the
// phone's Transform page shows below the flat transform.
PanelFrame {
    id: root
    SearchAnchor {
        searchId: "transform3d-panel"
        title: qsTr("3D Transform")
        keywords: "3d position anchor rotation scale depth world local orientation"
    }

    property bool shown: false

    AssetsPanelHeader {
        id: header
        width: parent.width
        height: Theme.panelHeaderHeight
        title: qsTr("3D Transform")
    }

    Flickable {
        id: flick
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        clip: true
        contentWidth: width
        contentHeight: inspector.implicitHeight + Theme.spacingLg * 2
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar {}

        Transform3dInspector {
            id: inspector
            x: Theme.spacingLg
            y: Theme.spacingLg
            width: flick.width - Theme.spacingLg * 2
        }
    }
}
