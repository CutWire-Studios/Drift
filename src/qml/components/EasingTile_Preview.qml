import QtQuick
import Drift

Item {
    implicitWidth: 360
    implicitHeight: 480

    Flow {
        anchors.fill: parent
        padding: Theme.spacingMd
        spacing: Theme.spacingXs

        Repeater {
            model: EditorState.easingPresets()

            EasingTile {
                required property var modelData
                required property int index

                width: 80
                preset: modelData
                selected: index === 1
            }
        }
    }
}
