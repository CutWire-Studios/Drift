import QtQuick
import Drift

Item {
    implicitWidth: 960
    implicitHeight: 640

    readonly property bool previewReady: mock.ready

    PreviewMockData {
        id: mock
    }

    readonly property var toolWindow: speedCurveWindow

    SpeedCurveWindow {
        id: speedCurveWindow
    }
}
