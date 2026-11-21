import QtQuick
import Drift
// Picking a shape adds it at the playhead; picking another replaces it.
AndroidAddPage {
    id: page

    propertiesComponent: Component {
        ShapeInspector { canEditPath: false }
    }

    ShapesTab {
        anchors.fill: parent
        pickerMode: true
        onAdded: page.adopt()
    }
}
