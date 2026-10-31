import QtQuick
import QtQuick.Layouts
import Drift

QuickSetupPage {
    id: root

    readonly property string discordUrl: "https://cutwire.org/discord"
    readonly property string email: "contact@cutwire.org"

    title: qsTr("You're all set")
    subtitle: qsTr("Need help or have an idea? We'd love to hear from you.")
    glyph: "heart"
    nextLabel: qsTr("Finish")
    contentMaxWidth: 560

    property real phase: 0
    property bool copied: false

    NumberAnimation on phase {
        from: 0
        to: 1
        duration: 5600
        loops: Animation.Infinite
        running: root.visible
    }

    Timer {
        id: copiedReset

        interval: 1800
        onTriggered: root.copied = false
    }

    TextInput {
        id: clipboardProxy

        visible: false
        text: root.email
    }

    Item {
        anchors.horizontalCenter: parent.horizontalCenter
        width: 260
        height: 120

        Repeater {
            model: [
                { x: 0, y: 8, w: 150, mine: false, offset: 0 },
                { x: 100, y: 52, w: 150, mine: true, offset: 0.33 },
                { x: 28, y: 92, w: 96, mine: false, offset: 0.66 }
            ]

            Rectangle {
                id: bubble

                required property var modelData
                readonly property real t: (root.phase + 1 - modelData.offset) % 1
                readonly property real appear: Math.min(1, t / 0.12)

                x: modelData.x
                y: modelData.y + (1 - appear) * 8
                width: modelData.w
                height: 28
                radius: 14
                opacity: appear * (1 - Math.max(0, (t - 0.9) / 0.1))
                color: modelData.mine ? Theme.c.quickSetup.dotActive : Theme.c.quickSetup.iconBackground

                Row {
                    anchors.centerIn: parent
                    spacing: 5

                    Repeater {
                        model: 3

                        Rectangle {
                            required property int index

                            width: 6
                            height: 6
                            radius: 3
                            color: bubble.modelData.mine ? Theme.c.quickSetupMockEditor.primaryForeground
                                                         : Theme.c.quickSetup.iconForeground
                            opacity: 0.45 + 0.55 * Math.max(0, Math.sin((root.phase * 6 - index * 0.3) * Math.PI))
                        }
                    }
                }
            }
        }
    }

    ThemedButton {
        anchors.horizontalCenter: parent.horizontalCenter
        variant: "primary"
        glyph: "users"
        text: qsTr("Join our Discord")
        topPadding: root.mobile ? Theme.spacingXl : Theme.spacingMd
        bottomPadding: topPadding
        leftPadding: Theme.spacing2xl
        rightPadding: leftPadding
        onClicked: Qt.openUrlExternally(root.discordUrl)
    }

    Rectangle {
        width: parent.width
        height: emailRow.implicitHeight + Theme.spacingLg * 2
        radius: Theme.c.quickSetup.radius
        color: Theme.c.quickSetup.panelBackground
        border.width: Theme.c.quickSetup.borderWidth
        border.color: Theme.c.quickSetup.panelBorder

        RowLayout {
            id: emailRow

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.margins: Theme.spacingLg
            spacing: Theme.spacingMd

            IconGlyph {
                glyph: "mail"
                iconSize: 18
                iconColor: Theme.c.quickSetup.iconForeground
            }

            Text {
                Layout.fillWidth: true
                text: root.email
                elide: Text.ElideRight
                color: addressArea.containsMouse ? Theme.c.quickSetup.dotActive : Theme.c.quickSetup.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeBase
                font.underline: addressArea.containsMouse

                Accessible.role: Accessible.Link
                Accessible.name: root.email

                MouseArea {
                    id: addressArea

                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: Qt.openUrlExternally("mailto:" + root.email)
                }
            }

            IconButton {
                glyph: root.copied ? Theme.icons.check : Theme.icons.copy
                tooltip: root.copied ? qsTr("Copied") : qsTr("Copy email address")
                onClicked: {
                    clipboardProxy.selectAll()
                    clipboardProxy.copy()
                    root.copied = true
                    copiedReset.restart()
                }
            }
        }
    }
}
