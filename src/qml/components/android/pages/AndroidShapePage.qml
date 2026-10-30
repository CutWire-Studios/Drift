import QtQuick
import Drift
// Picking a shape adds it at the playhead; picking another replaces it.
AndroidAddPage {
    id: page

    propertiesComponent: Component {
        ShapeInspector { }
    }

    ShapesTab {
        anchors.fill: parent
        onAdded: page.adopt()
    }
}
