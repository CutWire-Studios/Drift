import QtQuick
import Drift

PanelFrame {
    id: root

    property bool shown: false

    EmptyState {
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.spacing3xl, 260)
        compact: true
        title: qsTr("Tracking")
    }
}
