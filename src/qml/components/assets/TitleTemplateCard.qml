import QtQuick
import Drift
// One title / lower-third template: a miniature of its elements, click to add them at the playhead.
Column {
    id: root
    required property var modelData
    required property real cardSize
    signal added()
    width: cardSize
    spacing: Theme.spacingSm

    scale: tap.pressed ? 0.97 : (hover.hovered ? 1.02 : 1.0)
    Behavior on scale {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }

    Rectangle {
        id: stage
        width: parent.width
        height: Math.round(width * 9 / 16)
        radius: Theme.c.titleTemplateCard.radius
        color: Theme.c.titleTemplateCard.background
        border.width: Theme.c.titleTemplateCard.borderWidth
        border.color: hover.hovered ? Theme.c.titleTemplateCard.borderHover : Theme.c.titleTemplateCard.border
        clip: true

        Behavior on border.color {
            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }

        Repeater {
            model: root.modelData.elements

            delegate: Item {
                id: element
                required property var modelData
                readonly property bool isText: modelData.type === "text"
                readonly property var back: modelData.background
                x: modelData.box[0] * stage.width
                y: modelData.box[1] * stage.height
                width: modelData.box[2] * stage.width
                height: modelData.box[3] * stage.height

                Rectangle {
                    visible: !element.isText
                    anchors.fill: parent
                    color: element.modelData.fill || "white"
                    opacity: element.modelData.opacity === undefined ? 1 : element.modelData.opacity
                    radius: Math.min(width, height) * (element.modelData.shape === "ellipse" || element.modelData.shape === "pill"
                                                       ? 0.5 : (element.modelData.radius || 0))
                }

                Rectangle {
                    visible: element.isText && !!element.back
                    color: element.back ? element.back.color : "transparent"
                    radius: element.back ? (element.back.radius || 0) * label.font.pixelSize : 0
                    width: label.contentWidth + 2 * (element.back ? element.back.padding || 0 : 0) * label.font.pixelSize
                    height: label.contentHeight + 2 * (element.back ? element.back.padding || 0 : 0) * label.font.pixelSize
                    x: label.x + label.contentX - (element.back ? element.back.padding || 0 : 0) * label.font.pixelSize
                    y: label.y + (label.height - label.contentHeight) / 2 - (element.back ? element.back.padding || 0 : 0) * label.font.pixelSize
                }

                Text {
                    id: label
                    readonly property real contentX: horizontalAlignment === Text.AlignRight ? width - contentWidth
                                                     : horizontalAlignment === Text.AlignHCenter ? (width - contentWidth) / 2 : 0
                    visible: element.isText
                    anchors.fill: parent
                    text: element.isText ? element.modelData.text : ""
                    color: element.modelData.color || "white"
                    font.family: element.modelData.font || Theme.fontFamily
                    font.weight: element.modelData.weight || 700
                    font.italic: !!element.modelData.italic
                    font.pixelSize: Math.max(3, (element.modelData.size || 0.06) * stage.height)
                    font.capitalization: element.modelData.case === "upper" ? Font.AllUppercase : Font.MixedCase
                    font.letterSpacing: (element.modelData.letterSpacing || 0) * font.pixelSize
                    wrapMode: element.modelData.wrap === false ? Text.NoWrap : Text.WordWrap
                    horizontalAlignment: element.modelData.align === "left" ? Text.AlignLeft
                                         : element.modelData.align === "right" ? Text.AlignRight : Text.AlignHCenter
                    verticalAlignment: element.modelData.valign === "top" ? Text.AlignTop
                                       : element.modelData.valign === "bottom" ? Text.AlignBottom : Text.AlignVCenter
                    elide: Text.ElideNone
                }
            }
        }

        HoverHandler {
            id: hover
            cursorShape: Qt.PointingHandCursor
        }

        TapHandler {
            id: tap
            onTapped: {
                EditorState.addTitleTemplate(root.modelData.id)
                root.added()
            }
        }

        ThemedToolTip {
            text: qsTr("%1 — click to add at the playhead, then type over the text.").arg(root.modelData.label)
            visible: hover.hovered
        }
    }

    Text {
        width: parent.width
        text: root.modelData.label
        elide: Text.ElideRight
        horizontalAlignment: Text.AlignHCenter
        color: hover.hovered ? Theme.c.titleTemplateCard.foregroundHover : Theme.c.titleTemplateCard.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs

        Behavior on color {
            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }
    }
}
