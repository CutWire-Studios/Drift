import QtQuick
import Drift
// Picking a sticker adds it at the playhead; picking another replaces it.
AndroidAddPage {
    id: page

    propertiesComponent: Component {
        TransformInspector { }
    }

    StickersTab {
        anchors.fill: parent
        onAdded: page.adopt()
    }
}
