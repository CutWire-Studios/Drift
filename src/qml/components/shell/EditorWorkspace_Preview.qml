import QtQuick
import Drift

Item {
    implicitWidth: 1600
    implicitHeight: 900

    readonly property bool previewReady: mock.ready

    PreviewMockData {
        id: mock
    }

    EditorWorkspace {
        anchors.fill: parent
    }
}
