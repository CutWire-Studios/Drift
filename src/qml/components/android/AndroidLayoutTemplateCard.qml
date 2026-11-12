import QtQuick
import QtQuick.Controls.Basic
import Drift

// One canvas template: a ratio swatch, its name, and the detail line under it.
AbstractButton {
    id: card

    required property var modelData
    property bool selected: false
    // A free size has no catalog ratio; draw the one being typed.
    property int customWidth: 1
    property int customHeight: 1

    signal chosen(string templateId)

    width: Theme.androidLayoutCardWidth
    height: Theme.androidLayoutCardHeight
    hoverEnabled: true

    readonly property var ratio:
        modelData.aspect === "custom"
        ? { w: Math.max(1, card.customWidth),
            h: Math.max(1, card.customHeight) }
        : LayoutPresets.ratioFor(modelData.aspect)

    Accessible.role: Accessible.RadioButton
    Accessible.name: modelData.label
    Accessible.description: modelData.detail
    Accessible.checked: card.selected
    Accessible.onPressAction: card.clicked()

    scale: card.down ? Theme.pressScale : 1.0

    Behavior on scale {
        NumberAnimation {
            duration: Theme.durationPress
            easing.type: Theme.easing
        }
    }

    background: Rectangle {
        radius: Theme.c.androidLayoutTemplateCard.radius
        color: card.selected ? Theme.c.androidLayoutTemplateCard.backgroundSelected
                             : Theme.c.androidLayoutTemplateCard.background
        border.width: card.selected ? Theme.c.androidLayoutTemplateCard.borderWidthSelected
                                    : Theme.c.androidLayoutTemplateCard.borderWidth
        border.color: card.selected ? Theme.c.androidLayoutTemplateCard.borderSelected
                                    : Theme.c.androidLayoutTemplateCard.border

        Behavior on color {
            ColorAnimation {
                duration: Theme.durationFast
                easing.type: Theme.easing
            }
        }
    }

    contentItem: Item {
        AssetFavoriteButton {
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: Theme.spacingSm
            z: 1
            tabId: "layouts"
            itemId: card.modelData.id
        }

        Item {
            id: swatchBox
            anchors.top: parent.top
            anchors.topMargin: Theme.spacingLg
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width - Theme.spacingLg * 2
            height: 52

            readonly property real scaleToBox:
                Math.min(width / card.ratio.w, height / card.ratio.h)

            Rectangle {
                anchors.centerIn: parent
                width: Math.max(12, card.ratio.w * swatchBox.scaleToBox)
                height: Math.max(12, card.ratio.h * swatchBox.scaleToBox)
                radius: Theme.c.androidLayoutTemplateCard.swatchRadius
                color: Theme.c.androidLayoutTemplateCard.swatchBackground
                border.width: Theme.c.androidLayoutTemplateCard.swatchBorderWidth
                border.color: card.selected ? Theme.c.androidLayoutTemplateCard.swatchBorderSelected
                                            : Theme.c.androidLayoutTemplateCard.swatchBorder

                IconGlyph {
                    anchors.centerIn: parent
                    glyph: card.modelData.icon
                    iconSize: Theme.iconSizeMd
                    iconColor: card.selected ? Theme.c.androidLayoutTemplateCard.iconSelected
                                             : Theme.c.androidLayoutTemplateCard.icon
                }
            }
        }

        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: Theme.spacingSm
            anchors.rightMargin: Theme.spacingSm
            anchors.top: swatchBox.bottom
            anchors.topMargin: Theme.spacingMd
            spacing: 1

            Text {
                width: parent.width
                text: card.modelData.label
                color: card.selected ? Theme.c.androidLayoutTemplateCard.foregroundSelected
                                     : Theme.c.androidLayoutTemplateCard.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                font.weight: Font.Medium
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                text: card.modelData.detail
                color: Theme.c.androidLayoutTemplateCard.foregroundMuted
                font.family: Theme.monoFontFamily
                font.pixelSize: Theme.fontSizeTiny
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }
        }
    }

    onClicked: {
        Haptics.select()
        card.chosen(card.modelData.id)
    }
}
