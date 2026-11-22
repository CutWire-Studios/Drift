import QtQuick
import Drift
// Picking a text style adds a styled text clip at the playhead; picking another replaces it.
AndroidAddPage {
    id: page

    propertiesComponent: Component {
        AndroidTextTabs {
            embedded: true
        }
    }

    TextAssetsTab {
        anchors.fill: parent
        pickerMode: true
        onAdded: page.adopt()
    }
}
