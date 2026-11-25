import QtQuick
import Drift

SettingsPage {
    id: page

    pageId: "surfaces"
    title: qsTr("Control surfaces")

    readonly property var surfaces: EditorState.surfaces

    SettingsGroupHeader {
        text: qsTr("Learn")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Learn mode")
        searchId: "surfaces-learn"
        description: qsTr("Click a parameter in the editor, such as a slider, a colour wheel or a mixer fader, then move a knob, fader or button on your device to bind it.")
        keywords: "control surface midi osc mackie faders knobs bind assign learn controller"
        query: page.query

        ThemedSwitch {
            checked: page.surfaces.learnMode
            onToggled: page.surfaces.learnMode = checked
        }
    }

    SettingsRow {
        title: page.surfaces.learnLabel
        description: qsTr("Waiting for a control. Move a fader or knob, or press a button, on a connected device.")
        keywords: "learn waiting"
        available: page.surfaces.learnTarget !== ""
        query: page.query

        ThemedButton {
            text: qsTr("Cancel")
            onClicked: page.surfaces.cancelLearn()
        }
    }

    SettingsGroupHeader {
        text: qsTr("MIDI")
        available: SurfaceHub.hasMidi
        query: page.query
    }

    SettingsRow {
        title: qsTr("MIDI controllers")
        searchId: "surfaces-midi"
        description: qsTr("Open every MIDI controller that is plugged in, including ones that appear later.")
        keywords: "midi usb bluetooth keyboard controller launchpad faders knobs enable"
        available: SurfaceHub.hasMidi
        query: page.query

        ThemedSwitch {
            checked: SurfaceHub.midi !== null && SurfaceHub.midi.enabled
            onToggled: SurfaceHub.midi.enabled = checked
        }
    }

    Repeater {
        model: SurfaceHub.midi !== null ? SurfaceHub.midi.ports : []

        delegate: SettingsRow {
            id: portRow

            required property var modelData

            title: modelData.name
            description: qsTr("Protocol: Mackie Control and HUI faders and buttons drive the mixer and transport, and follow the editor.")
            keywords: "midi port protocol mackie hui generic motorized faders"
            query: page.query

            ThemedComboBox {
                width: Theme.c.settingsRow.controlWidth
                textRole: "label"
                valueRole: "id"
                model: SurfaceHub.midi.protocols
                currentIndex: Math.max(0, indexOfValue(portRow.modelData.protocol))
                onActivated: SurfaceHub.midi.setProtocol(portRow.modelData.name, currentValue)
            }
        }
    }

    SettingsGroupHeader {
        text: qsTr("Devices")
        query: page.query
    }

    SettingsRow {
        title: qsTr("No control surfaces")
        description: qsTr("Plug in a MIDI controller, or enable OSC or the phone remote, and it appears here.")
        keywords: "control surface midi osc devices"
        available: page.surfaces.devices.length === 0
        query: page.query
    }

    Repeater {
        model: page.surfaces.devices

        delegate: SettingsRow {
            id: devRow
            required property var modelData

            title: modelData.name
            description: modelData.connected ? qsTr("Connected") : qsTr("Not connected")
            keywords: "control surface midi osc bindings " + modelData.id
            stacked: true
            query: page.query

            SurfaceDeviceBindings {
                device: devRow.modelData
            }
        }
    }
}
