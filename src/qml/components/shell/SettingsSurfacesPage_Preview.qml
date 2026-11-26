import QtQuick
import Drift

Item {
    implicitWidth: 760
    implicitHeight: 760

    Column {
        anchors.fill: parent
        spacing: 16

        SettingsSurfacesPage {
            width: parent.width
        }

        SurfaceDeviceBindings {
            device: ({ id: "preview", name: "Preview device", connected: true,
                       controls: [{ id: "fader1", label: "Fader 1", kind: "absolute" }] })
        }
    }
}
