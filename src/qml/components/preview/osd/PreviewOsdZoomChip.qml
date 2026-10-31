import QtQuick
import Drift
// Shown once the view has moved, or while a masking tool is up (zooming in to paint or place
// points precisely); the % in the middle is the way back to 100%.
Item {
    id: chip

    property var viewport

    visible: !!chip.viewport && (chip.viewport.viewMoved || EditorState.preview.maskingTool !== "")
    width: visible ? zoomOut.width + zoomLabel.width + zoomIn.width : 0
    height: Theme.iconButtonSize

    IconButton {
        id: zoomOut
        anchors.left: parent.left
        glyph: Theme.icons.zoomOut
        variant: "text"
        tooltip: qsTr("Zoom out")
        onClicked: chip.viewport.zoomAt(chip.viewport.width / 2, chip.viewport.height / 2, 1 / 1.25)
    }

    Item {
        id: zoomLabel
        anchors.left: zoomOut.right
        width: zoomText.implicitWidth + Theme.spacingMd * 2
        height: parent.height

        Text {
            id: zoomText
            anchors.centerIn: parent
            text: chip.viewport ? Math.round(chip.viewport.userZoom * 100) + "%" : ""
            color: zoomMouse.containsMouse ? Theme.c.previewOsdZoomChip.foregroundHover : Theme.c.previewOsdZoomChip.foreground
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        MouseArea {
            id: zoomMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: if (chip.viewport) chip.viewport.resetView()
        }

        ThemedToolTip {
            text: Theme.platformShortcutText(
                      qsTr("Preview zoom — Ctrl+scroll over the preview to zoom, "
                         + "middle-drag to pan. Click to reset to 100%."))
            visible: zoomMouse.containsMouse
        }
    }

    IconButton {
        id: zoomIn
        anchors.left: zoomLabel.right
        glyph: Theme.icons.zoomIn
        variant: "text"
        tooltip: qsTr("Zoom in")
        onClicked: chip.viewport.zoomAt(chip.viewport.width / 2, chip.viewport.height / 2, 1.25)
    }
}
