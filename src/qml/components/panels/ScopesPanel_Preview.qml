import QtQuick
import Drift

Item {
    implicitWidth: 960
    implicitHeight: 640

    readonly property bool previewReady: mock.ready

    PreviewMockData {
        id: mock
    }

    ScopesPanel {
        anchors.fill: parent
        shown: true
    }
}
