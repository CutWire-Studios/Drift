import QtQuick
import Drift

// Grade on the left, output size on the right. Orange under the threshold, green once it clears it.
Rectangle {
    id: pill

    required property bool suggestUpscale
    required property int shortSide
    required property int outputWidth
    required property int outputHeight

    readonly property color tint: pill.suggestUpscale ? Theme.c.mediaPreviewResolutionPill.warning : Theme.c.mediaPreviewResolutionPill.constructive
    readonly property color ink: pill.suggestUpscale ? Theme.c.mediaPreviewResolutionPill.warningForeground : Theme.c.mediaPreviewResolutionPill.constructiveForeground

    width: pillRow.width + 2
    height: 38
    radius: Theme.radiusPill
    color: Qt.alpha(tint, Theme.c.mediaPreviewResolutionPill.tintOpacity)
    border.width: Theme.c.mediaPreviewResolutionPill.borderWidth
    border.color: Qt.alpha(tint, Theme.c.mediaPreviewResolutionPill.tintOpacityBorder)

    Row {
        id: pillRow
        anchors.centerIn: parent

        MediaPreviewGradeChip {
            tint: pill.tint
            grade: qsTr("%1p").arg(pill.shortSide)
            height: pill.height - 2
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            leftPadding: 14
            rightPadding: 18
            text: pill.outputWidth + " × " + pill.outputHeight
            font.family: Theme.monoFontFamily
            font.pixelSize: 18
            font.weight: Font.Medium
            color: pill.ink
        }
    }
}
