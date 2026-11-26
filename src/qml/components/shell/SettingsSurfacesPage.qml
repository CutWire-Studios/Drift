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
        text: qsTr("OSC")
        available: SurfaceHub.hasOsc
        query: page.query
    }

    SettingsRow {
        title: qsTr("OSC control")
        searchId: "surfaces-osc"
        description: qsTr("Listen for OSC messages over UDP, for TouchOSC, Open Stage Control and scripts. Anything else a sender transmits shows up as a control to bind.")
        keywords: "osc open sound control touchosc udp network"
        available: SurfaceHub.hasOsc
        query: page.query

        ThemedSwitch {
            checked: SurfaceHub.osc !== null && SurfaceHub.osc.enabled
            onToggled: SurfaceHub.osc.enabled = checked
        }
    }

    SettingsRow {
        title: qsTr("Listen port")
        description: qsTr("The UDP port Drift receives on.")
        keywords: "osc port udp listen"
        available: SurfaceHub.hasOsc
        query: page.query

        ThemedNumberField {
            width: Theme.c.settingsRow.controlWidth
            from: 1
            to: 65535
            value: SurfaceHub.osc !== null ? SurfaceHub.osc.port : 8000
            onEdited: v => SurfaceHub.osc.port = v
        }
    }

    SettingsRow {
        title: qsTr("Feedback port")
        description: qsTr("The UDP port on the sender's machine that Drift answers to.")
        keywords: "osc port udp feedback reply"
        available: SurfaceHub.hasOsc
        query: page.query

        ThemedNumberField {
            width: Theme.c.settingsRow.controlWidth
            from: 1
            to: 65535
            value: SurfaceHub.osc !== null ? SurfaceHub.osc.feedbackPort : 9000
            onEdited: v => SurfaceHub.osc.feedbackPort = v
        }
    }

    SettingsRow {
        title: qsTr("Allowed hosts")
        searchId: "surfaces-osc-hosts"
        description: qsTr("One address or range per line, such as 192.168.1.40 or 10.0.0.0/24. Empty allows the local network only.")
        keywords: "osc allowed hosts firewall allowlist ip address network"
        stacked: true
        available: SurfaceHub.hasOsc
        query: page.query

        ThemedTextArea {
            width: parent.width
            text: SurfaceHub.osc !== null ? SurfaceHub.osc.allowedHosts : ""
            onActiveFocusChanged: if (!activeFocus) SurfaceHub.osc.allowedHosts = text
        }
    }

    SettingsRow {
        title: qsTr("OSC problem")
        description: SurfaceHub.osc !== null ? SurfaceHub.osc.error : ""
        keywords: "osc error port in use"
        available: SurfaceHub.hasOsc && SurfaceHub.osc !== null && SurfaceHub.osc.error !== ""
        query: page.query
    }

    SettingsGroupHeader {
        text: qsTr("Phone remote")
        available: SurfaceHub.hasRemoteServer || SurfaceHub.hasRemoteClient
        query: page.query
    }

    SettingsRow {
        title: qsTr("Phone remote")
        searchId: "surfaces-remote"
        description: qsTr("Let a phone running Drift act as a jog wheel, fader bank and colour trackballs for this editor over your local network.")
        keywords: "phone remote android pair qr code wifi jog wheel trackball faders gyro"
        available: SurfaceHub.hasRemoteServer
        query: page.query

        ThemedSwitch {
            checked: SurfaceHub.remoteServer !== null && SurfaceHub.remoteServer.enabled
            onToggled: SurfaceHub.remoteServer.enabled = checked
        }
    }

    SettingsRow {
        title: qsTr("Pair a phone")
        description: qsTr("Scan the code from the Drift app on your phone, or type the code into its Remote page. It works for a few minutes.")
        keywords: "pair phone qr code token connect"
        stacked: true
        available: SurfaceHub.hasRemoteServer && SurfaceHub.remoteServer.enabled
        query: page.query

        Loader {
            width: parent.width
            active: SurfaceHub.hasRemoteServer
            sourceComponent: Component {
                Column {
                    width: parent.width
                    spacing: Theme.spacingMd

                    ThemedButton {
                        text: SurfaceHub.remoteServer.pairing ? qsTr("Stop pairing") : qsTr("Pair phone")
                        onClicked: SurfaceHub.remoteServer.pairing ? SurfaceHub.remoteServer.stopPairing()
                                                                   : SurfaceHub.remoteServer.startPairing()
                    }

                    Canvas {
                        id: qr
                        readonly property var rows: SurfaceHub.remoteServer.pairingQr
                        visible: rows.length > 0
                        width: Theme.c.phonePairing.qrSize
                        height: width
                        onRowsChanged: requestPaint()
                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.fillStyle = Theme.c.phonePairing.qrBackground
                            ctx.fillRect(0, 0, width, height)
                            const n = rows.length
                            if (n === 0)
                                return
                            const quiet = 2
                            const cell = width / (n + quiet * 2)
                            ctx.fillStyle = Theme.c.phonePairing.qrModule
                            for (let y = 0; y < n; ++y)
                                for (let x = 0; x < n; ++x)
                                    if (rows[y][x] === "1")
                                        ctx.fillRect((x + quiet) * cell, (y + quiet) * cell, Math.ceil(cell), Math.ceil(cell))
                        }
                    }

                    ThemedLabel {
                        width: parent.width
                        visible: SurfaceHub.remoteServer.pairing
                        text: SurfaceHub.remoteServer.pairingCode
                        tone: "muted"
                        size: "sm"
                        wrapMode: Text.WrapAnywhere
                    }

                    ThemedLabel {
                        width: parent.width
                        visible: text.length > 0
                        text: SurfaceHub.remoteServer.error
                        tone: "muted"
                        size: "sm"
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }

    Repeater {
        model: SurfaceHub.remoteServer !== null ? SurfaceHub.remoteServer.phones : []

        delegate: SettingsRow {
            id: phoneRow

            required property var modelData

            title: modelData.name
            description: modelData.connected ? qsTr("Connected") : qsTr("Paired, not connected")
            keywords: "phone remote paired forget"
            query: page.query

            ThemedButton {
                text: qsTr("Forget")
                onClicked: SurfaceHub.remoteServer.forget(phoneRow.modelData.id)
            }
        }
    }

    SettingsRow {
        title: qsTr("Use this phone as a remote")
        searchId: "surfaces-remote-phone"
        description: qsTr("Pair with Drift on your computer to jog, fly the 3D view and set colours from here.")
        keywords: "phone remote pair desktop control"
        available: SurfaceHub.hasRemoteClient
        query: page.query

        ThemedButton {
            text: qsTr("Open remote")
            onClicked: SurfaceHub.openRemote()
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
