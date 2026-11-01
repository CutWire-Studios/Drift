import QtQuick
import QtQuick.Controls.Basic
import Drift

ThemedDialog {
    id: root

    title: qsTr("Help")
    preferredWidth: 460
    showFooter: true
    showReject: false
    acceptText: qsTr("Close")

    readonly property var links: [
        { glyph: Theme.icons.bug, title: qsTr("Open an issue on GitHub"),
          url: "https://github.com/CutWire-Studios/Drift/issues",
          label: "github.com/CutWire-Studios/Drift/issues" },
        { glyph: Theme.icons.messageCircle, title: qsTr("Join our Discord server"),
          url: "https://cutwire.org/discord", label: "cutwire.org/discord" },
        { glyph: Theme.icons.bookOpen, title: qsTr("Read the docs"),
          url: "https://docs.cutwire.org/drift", label: "docs.cutwire.org/drift" },
        { glyph: Theme.icons.mail, title: qsTr("Contact us via email"),
          url: "mailto:contact@cutwire.org", label: "contact@cutwire.org" }
    ]

    contentItem: Column {
        spacing: Theme.spacingXl
        width: parent ? parent.width : 460

        Repeater {
            model: root.links

            delegate: Item {
                required property var modelData
                width: parent.width
                height: Math.max(linkText.implicitHeight, openButton.implicitHeight)

                IconGlyph {
                    id: linkIcon
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: modelData.glyph
                    iconSize: Theme.iconSizeMd
                }

                Column {
                    id: linkText
                    anchors.left: linkIcon.right
                    anchors.right: openButton.left
                    anchors.leftMargin: Theme.spacingLg
                    anchors.rightMargin: Theme.spacingLg
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.spacingXs

                    ThemedLabel {
                        width: parent.width
                        text: modelData.title
                        size: "sm"
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                    }

                    ThemedLabel {
                        width: parent.width
                        text: modelData.label
                        size: "xs"
                        tone: "muted"
                        elide: Text.ElideRight
                    }
                }

                ThemedButton {
                    id: openButton
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    variant: "secondary"
                    text: qsTr("Open")
                    onClicked: Qt.openUrlExternally(modelData.url)
                }
            }
        }
    }
}
