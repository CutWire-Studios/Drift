import QtQuick
import QtQuick.Controls.Basic
import Drift

ThemedDialog {
    id: root

    title: qsTr("About Drift")
    preferredWidth: 420
    showFooter: true
    showReject: false
    acceptText: qsTr("Close")

    contentItem: Column {
        spacing: Theme.spacingXl
        width: parent ? parent.width : 420

        Row {
            spacing: Theme.spacingXl
            anchors.horizontalCenter: parent.horizontalCenter

            Image {
                source: "qrc:/app/drift.png"
                width: 64
                height: 64
                fillMode: Image.PreserveAspectFit
                mipmap: true
            }

            Column {
                spacing: Theme.spacingXs
                anchors.verticalCenter: parent.verticalCenter

                ThemedLabel {
                    text: qsTr("Drift")
                    size: "xl"
                    font.weight: Font.DemiBold
                }

                ThemedLabel {
                    text: qsTr("Version %1").arg(Updates.currentVersion)
                    tone: "muted"
                    size: "sm"
                }
            }
        }

        ThemedLabel {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            size: "sm"
            text: qsTr("Open-source video editor by CutWire Studios.")
        }

        ThemedLabel {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            size: "xs"
            tone: "muted"
            text: qsTr("Licensed under GPLv3. Copyright © CutWire Studios.")
        }
    }
}
