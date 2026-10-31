import QtQuick
import Drift

Item {
    implicitWidth: 960
    implicitHeight: 640

    Component.onCompleted: EditorState.selectClip(0, 0)

    TextInspector {
        anchors.fill: parent
    }
}
