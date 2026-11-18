import QtQuick
import Drift

Column {
    id: root
    property var inspector
    width: parent ? parent.width : implicitWidth
    spacing: Theme.spacingMd
    visible: inspector.showAnimate && inspector.hasTextStyle

    TextInspectorAnimSlotBar {
        inspector: root.inspector
    }
    TextInspectorAnimateHint {
        inspector: root.inspector
    }
    TextInspectorAnimCategoryStrip {
        inspector: root.inspector
    }
    TextInspectorAnimPresetGrid {
        inspector: root.inspector
    }
    Flow {
        width: parent.width
        spacing: Theme.spacingSm

        ThemedButton {
            text: qsTr("Import…")
            glyph: Theme.icons.download
            variant: "secondary"
            tooltip: qsTr("Import an animation from a Lottie or exported preset file")
            onClicked: root.inspector.importAnimationPreset()
        }
        ThemedButton {
            text: qsTr("Rename…")
            glyph: Theme.icons.pencil
            variant: "secondary"
            visible: root.inspector.selectedPresetIsUser
            onClicked: root.inspector.requestRenameAnimation()
        }
        ThemedButton {
            text: qsTr("Export…")
            glyph: Theme.icons.upload
            variant: "secondary"
            visible: root.inspector.selectedPresetIsUser
            onClicked: root.inspector.exportAnimationPreset()
        }
        ThemedButton {
            text: qsTr("Delete")
            glyph: Theme.icons.trash
            variant: "secondary"
            visible: root.inspector.selectedPresetIsUser
            onClicked: root.inspector.requestDeleteAnimation()
        }
    }
    TextInspectorAnimControls {
        inspector: root.inspector
    }
    TextInspectorAnimAdvanced {
        inspector: root.inspector
    }
}
