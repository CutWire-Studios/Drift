import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

// The marketplace opt-in. Shown by the Market tab and by the shared-link sheet, which are the
// two ways into a store download — one component so the wording cannot drift apart between them.
//
// The tick is deliberate friction. Nothing here is a normal in-app download: the daily cap, the
// third-party sources and the failure modes are all things a user has to have been told about
// before the first attempt, not after it fails.
Item {
    id: root

    property real sideMargin: Theme.pagePadding
    signal accepted()

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: column.implicitHeight + Theme.spacing3xl * 2
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar { }

        Column {
            id: column
            x: root.sideMargin
            y: Theme.spacing3xl
            width: Math.min(parent.width - root.sideMargin * 2, 420)
            spacing: Theme.spacingXl

            Rectangle {
                width: 48
                height: 48
                radius: Theme.radiusMd
                color: Theme.panelAccent

                IconGlyph {
                    anchors.centerIn: parent
                    glyph: Theme.icons.warning
                    iconSize: Theme.iconSizeXl
                    iconColor: Theme.warning
                }
            }

            ThemedLabel {
                width: parent.width
                tone: "default"
                size: "base"
                text: qsTr("The marketplace is experimental")
            }

            ThemedLabel {
                width: parent.width
                text: qsTr("This feature is still being built and can change or stop working at any time. Before you use it, please read what it can and cannot do.")
            }

            Column {
                width: parent.width
                spacing: Theme.spacingLg

                Repeater {
                    model: [
                        qsTr("You get a limited number of downloads per day. The limit is small, may change without notice, and once it is used up you have to wait."),
                        qsTr("We cannot guarantee that any source stays available. Sources can be removed, rate-limited or broken by the sites they pull from, at any time and without warning."),
                        qsTr("We cannot guarantee that a download will succeed, finish, or give you the quality you picked. Some items will simply fail."),
                        qsTr("Everything here comes from third parties. Drift does not host, own or vet it — you are responsible for making sure you have the right to use whatever you download.")
                    ]

                    delegate: Row {
                        required property string modelData
                        width: column.width
                        spacing: Theme.spacingLg

                        Rectangle {
                            width: Theme.spacingMd
                            height: width
                            radius: width / 2
                            y: Theme.spacingLg
                            color: Theme.mutedForeground
                        }

                        ThemedLabel {
                            width: parent.width - Theme.spacingMd - Theme.spacingLg
                            text: parent.modelData
                        }
                    }
                }
            }

            Item { width: 1; height: Theme.spacingSm }

            ThemedCheckBox {
                id: understood
                text: qsTr("I understand")
            }

            ThemedButton {
                variant: "primary"
                text: qsTr("Continue to the marketplace")
                enabled: understood.checked
                onClicked: {
                    Market.acceptTerms()
                    root.accepted()
                }
            }
        }
    }
}
