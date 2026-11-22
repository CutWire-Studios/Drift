import QtQuick
import QtQuick.Controls.Basic
import Drift

// One upscaler in the enhance window: a before/after thumbnail on the content it was made for,
// what it is good at, and how fast it runs relative to the other installed upscalers.
AbstractButton {
    id: root

    // An entry of RestoreWindow.upscaleModel: {name, value, scale, summary, thumbnail, content, spm, custom}.
    property var model: ({})
    // 1 (slowest) to 5 (fastest); 0 hides the meter.
    property int speedLevel: 0
    property string perFrameText: ""
    property bool selected: false
    // A chevron in the sidebar, where the card opens the picker rather than choosing.
    property string trailingGlyph: ""

    readonly property bool isNone: !model.value
    readonly property var contentLabels: ({
        anime: qsTr("Anime and drawings"),
        live: qsTr("Live action"),
        cg: qsTr("3D animation and games"),
        general: qsTr("General")
    })

    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    padding: Theme.spacingSm
    implicitHeight: body.implicitHeight + topPadding + bottomPadding
    opacity: enabled ? 1 : Theme.c.upscaleModelCard.disabledOpacity

    Accessible.role: Accessible.RadioButton
    Accessible.name: model.name || ""
    Accessible.checked: root.selected

    background: Rectangle {
        radius: Theme.c.upscaleModelCard.radius
        color: root.selected ? Qt.alpha(Theme.c.upscaleModelCard.accent, Theme.c.upscaleModelCard.accentOpacity)
                             : root.hovered ? Theme.c.upscaleModelCard.backgroundHover : Theme.c.upscaleModelCard.background
        border.width: root.selected || root.visualFocus ? Theme.c.upscaleModelCard.borderWidthFocus : Theme.c.upscaleModelCard.borderWidth
        border.color: root.selected || root.visualFocus ? Theme.c.upscaleModelCard.borderFocus : Theme.c.upscaleModelCard.border
    }

    contentItem: Column {
        id: body
        spacing: Theme.spacingSm

        Rectangle {
            id: frame
            width: parent.width
            height: Math.round(width * 9 / 16)
            radius: Theme.c.upscaleModelCard.frameRadius
            color: Theme.c.upscaleModelCard.frameBackground
            clip: true

            Image {
                anchors.fill: parent
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                smooth: true
                source: root.model.thumbnail || ""
                visible: status === Image.Ready
            }

            IconGlyph {
                anchors.centerIn: parent
                visible: !root.model.thumbnail
                glyph: root.isNone ? Theme.icons.x : Theme.icons.image
                iconSize: 22
                iconColor: Theme.c.upscaleModelCard.icon
            }

            // The thumbnails are split down the middle: bicubic on the left, the model on the right.
            Repeater {
                model: root.model.thumbnail ? [qsTr("Before"), qsTr("After")] : []
                delegate: Rectangle {
                    required property int index
                    required property string modelData
                    x: index === 0 ? 4 : frame.width - width - 4
                    y: frame.height - height - 4
                    width: tag.implicitWidth + 8
                    height: tag.implicitHeight + 2
                    radius: Theme.c.upscaleModelCard.badgeRadius
                    color: Theme.c.upscaleModelCard.badgeBackground

                    Text {
                        id: tag
                        anchors.centerIn: parent
                        text: parent.modelData
                        color: Theme.c.upscaleModelCard.badgeForeground
                        font.pixelSize: Theme.fontSizeTiny
                    }
                }
            }
        }

        Row {
            width: parent.width
            spacing: Theme.spacingSm

            ThemedLabel {
                id: nameLabel
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, parent.width - scaleBadge.width - chevron.width - parent.spacing * 2)
                elide: Text.ElideRight
                size: "sm"
                tone: "default"
                font.weight: Font.Medium
                text: root.model.name || ""
            }

            Rectangle {
                id: scaleBadge
                anchors.verticalCenter: parent.verticalCenter
                visible: !root.isNone
                width: visible ? scaleText.implicitWidth + 10 : 0
                height: scaleText.implicitHeight + 2
                radius: Theme.c.upscaleModelCard.scaleRadius
                color: Theme.c.upscaleModelCard.scaleBackground
                border.width: Theme.c.upscaleModelCard.scaleBorderWidth
                border.color: Theme.c.upscaleModelCard.scaleBorder

                Text {
                    id: scaleText
                    anchors.centerIn: parent
                    text: (root.model.scale || 1) + "×"
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.fontSizeXs
                    font.weight: Font.Medium
                    color: Theme.c.upscaleModelCard.foreground
                }
            }

            Item {
                height: 1
                width: Math.max(0, parent.width - nameLabel.width - scaleBadge.width - chevron.width - parent.spacing * 3)
            }

            IconGlyph {
                id: chevron
                anchors.verticalCenter: parent.verticalCenter
                visible: root.trailingGlyph.length > 0
                width: visible ? iconSize : 0
                glyph: root.trailingGlyph
                iconSize: 14
            }
        }

        ThemedLabel {
            width: parent.width
            visible: text.length > 0
            elide: Text.ElideRight
            text: (root.model.content || []).map(c => root.contentLabels[c] || c).join(", ")
                  || (root.model.custom ? qsTr("Custom model") : "")
        }

        ThemedLabel {
            width: parent.width
            visible: text.length > 0
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
            tone: "default"
            text: root.model.summary || ""
        }

        Row {
            visible: root.speedLevel > 0
            spacing: Theme.spacingSm

            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2

                Repeater {
                    model: 5
                    delegate: Rectangle {
                        required property int index
                        width: 10
                        height: 4
                        radius: Theme.c.upscaleModelCard.meterRadius
                        color: index < root.speedLevel ? Theme.c.upscaleModelCard.meterOn : Theme.c.upscaleModelCard.meterOff
                    }
                }
            }

            ThemedLabel {
                anchors.verticalCenter: parent.verticalCenter
                text: root.perFrameText
            }
        }
    }
}
