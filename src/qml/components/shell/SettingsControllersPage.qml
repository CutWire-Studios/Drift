import QtQuick
import Drift

SettingsPage {
    id: page

    pageId: "controllers"
    title: qsTr("Controllers")

    SettingsGroupHeader {
        text: qsTr("Devices")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Use game controllers")
        searchId: "controllers-enabled"
        description: Gamepad.available ? qsTr("Fly the 3D view and drive the timeline from a gamepad.")
                                       : qsTr("This build has no game controller support.")
        keywords: "gamepad joystick xbox playstation dualsense switch pro input"
        available: Gamepad.available
        query: page.query

        ThemedSwitch {
            checked: Gamepad.enabled
            onToggled: Gamepad.enabled = checked
        }
    }

    SettingsRow {
        title: qsTr("Connected controllers")
        searchId: "controllers-devices"
        description: Gamepad.connected ? "" : qsTr("None found. Plug one in or pair it over Bluetooth.")
        keywords: "gamepad joystick connected gyro rumble"
        stacked: true
        available: Gamepad.available && Gamepad.enabled
        query: page.query

        Column {
            width: parent.width
            spacing: Theme.spacingSm

            Repeater {
                model: Gamepad.devices

                Text {
                    required property var modelData
                    width: parent.width
                    text: modelData.name + (modelData.gyro ? qsTr(" · gyro") : "") + (modelData.rumble ? qsTr(" · rumble") : "")
                    color: Theme.c.settingsRow.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeBase
                    elide: Text.ElideRight
                }
            }
        }
    }

    SettingsRow {
        title: qsTr("Test")
        searchId: "controllers-test"
        description: qsTr("Move the sticks and press buttons to see what the editor receives.")
        keywords: "gamepad calibrate sticks triggers buttons"
        stacked: true
        available: Gamepad.available && Gamepad.enabled && Gamepad.connected
        query: page.query

        ControllerTestView {
            width: parent.width
        }
    }

    SettingsGroupHeader {
        text: qsTr("Sticks")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Dead zone")
        searchId: "controllers-dead-zone"
        description: qsTr("How far a stick must move before it counts. Raise it if the view drifts on its own.")
        keywords: "gamepad stick drift threshold"
        available: Gamepad.available
        query: page.query

        ThemedSlider {
            width: Theme.c.settingsRow.controlWidth
            label: qsTr("Dead zone")
            from: 0
            to: 0.6
            value: Gamepad.deadZone
            valueFormatter: (v) => Math.round(v * 100) + "%"
            onMoved: Gamepad.deadZone = value
        }
    }

    SettingsRow {
        title: qsTr("Response curve")
        searchId: "controllers-curve"
        description: qsTr("1 is linear. Higher values give finer control near the centre.")
        keywords: "gamepad stick exponent sensitivity acceleration"
        available: Gamepad.available
        query: page.query

        ThemedSlider {
            width: Theme.c.settingsRow.controlWidth
            label: qsTr("Response curve")
            from: 1
            to: 3
            value: Gamepad.curve
            valueFormatter: (v) => Number(v).toFixed(1)
            onMoved: Gamepad.curve = value
        }
    }

    SettingsRow {
        title: qsTr("Look speed")
        searchId: "controllers-look-speed"
        description: qsTr("Degrees per second at full right-stick deflection in 3D fly mode.")
        keywords: "gamepad right stick sensitivity turn rate fly"
        available: Gamepad.available
        query: page.query

        ThemedSlider {
            width: Theme.c.settingsRow.controlWidth
            label: qsTr("Look speed")
            from: 30
            to: 360
            value: Gamepad.lookSpeed
            valueFormatter: (v) => Math.round(v) + "°/s"
            onMoved: Gamepad.lookSpeed = value
        }
    }

    SettingsGroupHeader {
        text: qsTr("Motion and feedback")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Gyro aiming")
        searchId: "controllers-gyro"
        description: qsTr("Tilt the controller to aim the camera in 3D fly mode.")
        keywords: "gamepad motion sensor gyroscope aim dualsense switch pro"
        available: Gamepad.available && Gamepad.hasGyro
        query: page.query

        ThemedSwitch {
            checked: Gamepad.gyroEnabled
            onToggled: Gamepad.gyroEnabled = checked
        }
    }

    SettingsRow {
        title: qsTr("Gyro sensitivity")
        searchId: "controllers-gyro-sensitivity"
        description: qsTr("Scales how far the view turns for a given tilt.")
        keywords: "gamepad motion gyroscope aim"
        available: Gamepad.available && Gamepad.hasGyro && Gamepad.gyroEnabled
        query: page.query

        ThemedSlider {
            width: Theme.c.settingsRow.controlWidth
            label: qsTr("Gyro sensitivity")
            from: 0.1
            to: 5
            value: Gamepad.gyroSensitivity
            valueFormatter: (v) => Number(v).toFixed(1) + "×"
            onMoved: Gamepad.gyroSensitivity = value
        }
    }

    SettingsRow {
        title: qsTr("Rumble feedback")
        searchId: "controllers-haptics"
        description: qsTr("A short pulse when scrubbing crosses a cut or marker and when a gizmo snaps.")
        keywords: "gamepad vibration haptics rumble snap marker"
        available: Gamepad.available && Gamepad.hasRumble
        query: page.query

        ThemedSwitch {
            checked: Gamepad.hapticsEnabled
            onToggled: {
                Gamepad.hapticsEnabled = checked
                if (checked)
                    Gamepad.rumble(0.0, 0.5, 60)
            }
        }
    }

    SettingsRow {
        title: qsTr("Button bindings")
        searchId: "controllers-bindings"
        description: qsTr("Gamepad buttons are bound like keys, in the Keyboard shortcuts page: add a binding and press a controller button.")
        keywords: "gamepad buttons keymap rebind shoulder dpad"
        available: Gamepad.available
        query: page.query

        ThemedButton {
            text: qsTr("Open shortcuts")
            onClicked: ShellBridge.main.openSettings("keymap")
        }
    }
}
