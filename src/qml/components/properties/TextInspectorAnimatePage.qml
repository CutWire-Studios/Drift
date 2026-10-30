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
    TextInspectorAnimControls {
        inspector: root.inspector
    }
    TextInspectorAnimAdvanced {
        inspector: root.inspector
    }
}
