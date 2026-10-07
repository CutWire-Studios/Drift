import QtQuick
import Drift
import ".."

// The preview's on-screen controls, kept off the picture until they are wanted: they fade in when
// the pointer moves over the preview and out again after a couple of idle seconds. They hold
// while hovered, while one of their popups is open, and while a tool is mid-drag.
Item {
    id: osd

    // The PreviewViewport whose zoom the chip reports and resets.
    property PreviewViewport viewport
    property bool interacting: false
    // Controls outside this item that fade with it (the fullscreen transport), and are in use.
    property bool held: false

    readonly property bool mode3d: EditorState.preview.mode === "3d"
    readonly property bool pinned: interacting || held || guidesPopover.opened || optionsMenu.busy
                                   || topHover.hovered || rightHover.hovered || toolsHover.hovered
    property bool awake: false
    readonly property bool shown: awake || pinned

    function wake() {
        awake = true
        idleTimer.restart()
    }

    // Passive, so the tools underneath still get their own hover and cursors.
    HoverHandler {
        id: pointerHover
        readonly property point position: point.position
        onPositionChanged: osd.wake()
        onHoveredChanged: if (hovered) osd.wake()
    }

    Timer {
        id: idleTimer
        interval: 2000
        onTriggered: osd.awake = false
    }

    Item {
        id: controls
        anchors.fill: parent
        opacity: osd.shown ? 1 : 0
        visible: opacity > 0

        Behavior on opacity {
            NumberAnimation {
                duration: osd.shown ? Theme.durationFast : Theme.durationSlow
                easing.type: Theme.easing
            }
        }

        PreviewModeSwitch {
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.topMargin: Theme.spacingLg

            HoverHandler { id: topHover }
        }

        Rectangle {
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: Theme.spacingLg
            width: rightRow.implicitWidth + Theme.spacingXs * 2
            height: rightRow.implicitHeight + Theme.spacingXs * 2
            radius: Theme.radiusMd
            color: Theme.panelBackground
            border.width: Theme.borderWidth
            border.color: Theme.panelBorder

            HoverHandler { id: rightHover }

            Row {
                id: rightRow
                anchors.centerIn: parent
                spacing: 0

                // Only once the view has moved, and the way back to 100%.
                Item {
                    visible: !!osd.viewport && osd.viewport.viewMoved
                    width: visible ? zoomText.implicitWidth + Theme.spacingMd * 2 : 0
                    height: Theme.iconButtonSize

                    Text {
                        id: zoomText
                        anchors.centerIn: parent
                        text: osd.viewport ? Math.round(osd.viewport.userZoom * 100) + "%" : ""
                        color: zoomMouse.containsMouse ? Theme.panelForeground : Theme.mutedForeground
                        font.family: Theme.monoFontFamily
                        font.pixelSize: Theme.fontSizeXs
                    }

                    MouseArea {
                        id: zoomMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: osd.viewport.resetView()
                    }

                    ThemedToolTip {
                        text: Theme.platformShortcutText(
                                  qsTr("Preview zoom — Ctrl+scroll over the preview to zoom, "
                                     + "middle-drag to pan. Click to reset to 100%."))
                        visible: zoomMouse.containsMouse
                    }
                }

                IconButton {
                    glyph: Theme.icons.grid
                    variant: "text"
                    tooltip: qsTr("Toggle guides")
                    active: EditorState.preview.guidesEnabled
                    onClicked: EditorState.preview.guidesEnabled = !EditorState.preview.guidesEnabled
                }

                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: Theme.icons.chevronDown
                    variant: "text"
                    tooltip: qsTr("Guide sets")
                    buttonSize: Theme.iconButtonSize * 0.6
                    iconSize: Theme.iconSizeBase * 0.75
                    active: guidesPopover.visible
                    onClicked: guidesPopover.opened ? guidesPopover.close() : guidesPopover.open()

                    GuidesPopover {
                        id: guidesPopover
                        x: parent.width - width
                        y: parent.height + Theme.spacingMd
                    }
                }

                IconButton {
                    glyph: Theme.icons.ellipsis
                    variant: "text"
                    tooltip: qsTr("Preview options")
                    active: optionsMenu.visible
                    onClicked: optionsMenu.opened ? optionsMenu.close() : optionsMenu.open()

                    PreviewOptionsMenu {
                        id: optionsMenu
                        x: parent.width - width
                        y: parent.height + Theme.spacingMd
                    }
                }
            }
        }

        Preview3DTools {
            visible: osd.mode3d
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: Theme.spacingLg

            HoverHandler { id: toolsHover }
        }
    }
}
