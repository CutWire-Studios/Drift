import QtQuick
import QtQuick.Layouts
import Drift

// The phone as a control surface for a desktop Drift: transport and jog, three colour trackballs,
// an eight-strip fader bank and a look pad with optional gyro steering for the 3D fly mode. Every
// control is a device control on the desktop, so it can be rebound there.
AndroidSubPage {
    id: page

    readonly property var client: SurfaceHub.remoteClient
    readonly property bool connected: client !== null && client.state === "connected"
    readonly property var values: client ? client.values : ({})

    title: qsTr("Remote")

    function value(control, fallback) {
        const v = page.values[control]
        return v === undefined ? fallback : v
    }

    Flickable {
        anchors.fill: parent
        anchors.leftMargin: SafeArea.margins.left + Theme.pagePadding
        anchors.rightMargin: SafeArea.margins.right + Theme.pagePadding
        contentHeight: column.height + Theme.spacing3xl
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        interactive: !jog.pressed

        Column {
            id: column
            width: parent.width
            topPadding: Theme.spacingLg
            spacing: Theme.spacingXl

            Column {
                width: parent.width
                spacing: Theme.spacingMd
                visible: !page.connected

                ThemedLabel {
                    width: parent.width
                    text: page.client && page.client.paired
                          ? qsTr("Reconnect to your desktop, or pair again with a new code.")
                          : qsTr("On your desktop open Settings, Control surfaces, then Pair phone. Type the code shown there, in the form 192.168.1.20:47890/ABCD2345.")
                    tone: "muted"
                    size: "sm"
                    wrapMode: Text.WordWrap
                }
                ThemedTextField {
                    id: codeField
                    width: parent.width
                    placeholderText: qsTr("Pairing code")
                    inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhPreferUppercase
                }
                ThemedLabel {
                    width: parent.width
                    visible: text.length > 0
                    text: page.client ? page.client.error : ""
                    tone: "destructive"
                    size: "sm"
                    wrapMode: Text.WordWrap
                }
                Row {
                    spacing: Theme.spacingMd

                    ThemedButton {
                        variant: "primary"
                        text: page.client && page.client.state === "connecting" ? qsTr("Connecting…") : qsTr("Connect")
                        enabled: page.client !== null && codeField.text.length > 0
                        onClicked: if (!page.client.pair(codeField.text)) Toasts.error(qsTr("That is not a pairing code."))
                    }
                    ThemedButton {
                        text: qsTr("Reconnect")
                        visible: page.client !== null && page.client.paired
                        onClicked: page.client.reconnect()
                    }
                    ThemedButton {
                        variant: "ghost"
                        text: qsTr("Forget desktop")
                        visible: page.client !== null && page.client.paired
                        onClicked: page.client.forget()
                    }
                }
            }

            Column {
                width: parent.width
                spacing: Theme.spacingXl
                visible: page.connected

                RowLayout {
                    width: parent.width

                    ThemedLabel {
                        Layout.fillWidth: true
                        text: qsTr("Connected to %1").arg(page.client ? page.client.hostName : "")
                        tone: "muted"
                        size: "sm"
                        elide: Text.ElideRight
                    }
                    ThemedButton {
                        variant: "ghost"
                        text: qsTr("Disconnect")
                        onClicked: page.client.disconnectFromDesktop()
                    }
                }

                Row {
                    spacing: Theme.spacingMd
                    anchors.horizontalCenter: parent.horizontalCenter

                    ThemedButton {
                        text: qsTr("Back")
                        onPressed: page.client.button("stepBack", true)
                        onReleased: page.client.button("stepBack", false)
                    }
                    ThemedButton {
                        variant: "primary"
                        text: page.client && page.client.playing ? qsTr("Pause") : qsTr("Play")
                        onPressed: page.client.button("play", true)
                        onReleased: page.client.button("play", false)
                    }
                    ThemedButton {
                        text: qsTr("Stop")
                        onPressed: page.client.button("stop", true)
                        onReleased: page.client.button("stop", false)
                    }
                    ThemedButton {
                        text: qsTr("Next")
                        onPressed: page.client.button("stepForward", true)
                        onReleased: page.client.button("stepForward", false)
                    }
                }

                RemoteJogWheel {
                    id: jog
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Math.min(parent.width, 240)
                    height: width
                    readonly property bool pressed: false
                    onTurned: ticks => page.client.relative("jog", ticks)
                }

                Flow {
                    width: parent.width
                    spacing: Theme.spacingMd

                    Repeater {
                        model: [
                            { key: "lift", label: qsTr("Shadows") },
                            { key: "gamma", label: qsTr("Midtones") },
                            { key: "gain", label: qsTr("Highlights") }
                        ]

                        delegate: ColorWheel {
                            id: ball

                            required property var modelData

                            size: Math.floor((column.width - Theme.spacingMd * 2) / 3) - 8
                            label: modelData.label
                            offset: Qt.point(page.value(modelData.key + ".x", 0.5) * 2 - 1,
                                             page.value(modelData.key + ".y", 0.5) * 2 - 1)
                            luma: page.value(modelData.key + ".luma", 0.5) * 2 - 1
                            onOffsetMoved: (x, y) => {
                                page.client.absolute(ball.modelData.key + ".x", (x + 1) / 2)
                                page.client.absolute(ball.modelData.key + ".y", (y + 1) / 2)
                            }
                            onLumaMoved: v => page.client.absolute(ball.modelData.key + ".luma", (v + 1) / 2)
                        }
                    }
                }

                Row {
                    id: faders
                    width: parent.width
                    height: 180
                    spacing: Theme.spacingSm

                    Repeater {
                        model: 8

                        delegate: Column {
                            id: strip

                            required property int index

                            width: (faders.width - faders.spacing * 7) / 8
                            height: faders.height
                            spacing: Theme.spacingXs

                            ThemedLabel {
                                width: parent.width
                                horizontalAlignment: Text.AlignHCenter
                                text: page.client && page.client.labels[strip.index] ? page.client.labels[strip.index]
                                                                                     : String(strip.index + 1)
                                tone: "muted"
                                size: "xs"
                                elide: Text.ElideRight
                            }
                            ThemedSlider {
                                width: parent.width
                                height: parent.height - y
                                orientation: Qt.Vertical
                                from: 0
                                to: 1
                                Binding on value {
                                    when: !strip.children[1].pressed
                                    value: page.value("fader" + (strip.index + 1), 0.75)
                                }
                                onMoved: page.client.absolute("fader" + (strip.index + 1), value)
                            }
                        }
                    }
                }

                Rectangle {
                    id: pad
                    width: parent.width
                    height: 160
                    radius: Theme.c.phoneRemote.padRadius
                    color: Theme.c.phoneRemote.padBackground
                    border.width: Theme.c.phoneRemote.borderWidth
                    border.color: Theme.c.phoneRemote.padBorder

                    ThemedLabel {
                        anchors.centerIn: parent
                        text: qsTr("Look pad: drag to turn the 3D view")
                        tone: "muted"
                        size: "sm"
                    }
                    MouseArea {
                        property real lastX: 0
                        property real lastY: 0

                        anchors.fill: parent
                        preventStealing: true
                        onPressed: mouse => { lastX = mouse.x; lastY = mouse.y }
                        onPositionChanged: mouse => {
                            page.client.relative("look.x", (mouse.x - lastX) * 0.5)
                            page.client.relative("look.y", (mouse.y - lastY) * 0.5)
                            lastX = mouse.x
                            lastY = mouse.y
                        }
                    }
                }

                RowLayout {
                    width: parent.width
                    visible: page.client && page.client.gyroAvailable

                    ThemedLabel {
                        Layout.fillWidth: true
                        text: qsTr("Steer with the gyroscope")
                    }
                    ThemedSwitch {
                        checked: page.client && page.client.gyro
                        onToggled: page.client.gyro = checked
                    }
                }
            }
        }
    }
}
