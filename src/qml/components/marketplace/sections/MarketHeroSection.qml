import QtQuick
import Drift
// A banner: image, headline, and an optional action when tapped.
Item {
    id: root

    required property var section
    required property var host

    readonly property bool actionable: !!root.section.action && !!root.section.action.type

    implicitHeight: root.host.compact ? 140 : 180

    Rectangle {
        x: root.host.sidePadding
        width: parent.width - root.host.sidePadding * 2
        height: parent.height
        radius: Theme.c.marketplace.heroRadius
        color: Theme.c.marketplace.heroBackground
        clip: true

        Image {
            anchors.fill: parent
            source: root.section.image || ""
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: heroText.implicitHeight + Theme.spacingXl * 2
            color: Theme.c.marketplace.heroScrim

            Column {
                id: heroText
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.margins: Theme.spacingXl
                spacing: Theme.spacingXs

                Text {
                    width: parent.width
                    text: root.section.title || ""
                    color: Theme.c.marketplace.heroForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeBase * 1.2
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    visible: text.length > 0
                    text: root.section.subtitle || ""
                    color: Theme.c.marketplace.heroForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm
                    elide: Text.ElideRight
                }
            }
        }

        HoverHandler { cursorShape: root.actionable ? Qt.PointingHandCursor : Qt.ArrowCursor }
        TapHandler {
            enabled: root.actionable
            onTapped: root.host.runAction(root.section.action)
        }
    }
}
