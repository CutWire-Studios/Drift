import QtQuick
import Drift

QuickSetupPage {
    id: root

    title: qsTr("Interface size")
    subtitle: qsTr("Make buttons, text and icons larger or smaller.")
    glyph: Theme.icons.zoomIn
    contentMaxWidth: 760

    readonly property var scales: [1.0, 1.25, 1.5, 1.75, 2.0]
    property real recommended: EditorState.preferences.uiScale
    readonly property real applied: EditorState.preferences.appliedUiScale > 0
                                    ? EditorState.preferences.appliedUiScale : 1.0
    readonly property real ratio: EditorState.preferences.uiScale / applied

    Component.onCompleted: recommended = EditorState.preferences.uiScale

    Flow {
        width: parent.width
        spacing: Theme.spacingMd

        Repeater {
            model: root.scales

            QuickSetupCard {
                id: card

                required property real modelData
                readonly property bool isRecommended: Math.abs(modelData - root.recommended) < 0.001

                width: 128
                height: 76
                text: Math.round(modelData * 100) + "%"
                selected: Math.abs(modelData - EditorState.preferences.uiScale) < 0.001
                onClicked: EditorState.preferences.uiScale = modelData

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: card.isRecommended ? Theme.spacingMd : (parent.height - height) / 2
                    text: card.text
                    color: Theme.c.quickSetupCard.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeBase * 1.3
                    font.weight: Font.Medium
                }

                Rectangle {
                    visible: card.isRecommended
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: Theme.spacingMd
                    width: badge.implicitWidth + Theme.spacingLg * 2
                    height: badge.implicitHeight + Theme.spacingXs * 2
                    radius: height / 2
                    color: Theme.c.quickSetupCard.badgeBackground

                    Text {
                        id: badge

                        anchors.centerIn: parent
                        text: qsTr("Recommended")
                        color: Theme.c.quickSetupCard.badgeForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeTiny
                        font.weight: Font.Medium
                    }
                }
            }
        }
    }

    Rectangle {
        width: parent.width
        height: 300
        radius: Theme.c.quickSetup.radius
        color: Theme.c.quickSetup.panelBackground
        border.width: Theme.c.quickSetup.borderWidth
        border.color: Theme.c.quickSetup.panelBorder
        clip: true

        QuickSetupMockEditor {
            anchors.centerIn: parent
            width: 480
            height: 300
            scale: root.ratio
            transformOrigin: Item.Center
            antialiasing: true

            Behavior on scale {
                NumberAnimation { duration: Theme.durationSlow; easing.type: Theme.easing }
            }
        }
    }

    ThemedLabel {
        width: parent.width
        horizontalAlignment: Text.AlignHCenter
        visible: EditorState.preferences.uiScaleNeedsRestart
        size: "sm"
        text: qsTr("Drift will restart when you finish setup to apply this size.")
    }
}
