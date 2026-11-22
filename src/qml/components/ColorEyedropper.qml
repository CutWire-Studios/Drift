import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import QtQuick.Window
import Drift

// Full-window colour picker over the app's own window, with a magnifying loupe. Pointer: hover
// to aim, click to pick. Touch: drag to aim, lift to pick. Escape, Back, a right click or the
// hint's cancel button (touch only) cancel.
// Samples a periodic grab of the window rather than a single one, so a preview that re-renders
// after opening (an effect bypassed for the pick) is what gets sampled.
Popup {
    id: root

    signal picked(color value)

    readonly property int zoom: 10
    readonly property int lensSize: Theme.touchUi ? 120 : 104

    property var _grab: null
    property bool _grabbing: false
    property point _pos: Qt.point(-1, -1)
    property bool _aiming: false
    property color _color: "transparent"

    parent: Overlay.overlay
    x: 0
    y: 0
    width: parent ? parent.width : 0
    height: parent ? parent.height : 0
    padding: 0
    modal: true
    dim: false
    closePolicy: Popup.CloseOnEscape
    background: null

    function _refresh() {
        if (_grabbing || !parent || !parent.parent)
            return
        _grabbing = true
        const target = parent.parent
        target.grabToImage(function (result) {
            root._grabbing = false
            root._grab = result
            root._sample()
        }, Qt.size(Math.round(target.width * area.dpr), Math.round(target.height * area.dpr)))
    }

    function _sample() {
        if (_grab && _aiming)
            _color = AppController.imagePixel(_grab.image, Math.floor(_pos.x * area.dpr),
                                              Math.floor(_pos.y * area.dpr))
    }

    onAboutToShow: {
        _aiming = false
        _grab = null
        _refresh()
    }
    onClosed: _grab = null

    Timer {
        interval: 200
        repeat: true
        running: root.visible
        onTriggered: root._refresh()
    }

    contentItem: MouseArea {
        id: area

        readonly property real dpr: Screen.devicePixelRatio

        hoverEnabled: true
        preventStealing: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: Qt.CrossCursor

        function aim(mouse) {
            root._pos = Qt.point(Math.floor(mouse.x), Math.floor(mouse.y))
            root._aiming = true
            root._sample()
        }

        onPositionChanged: (mouse) => aim(mouse)
        onPressed: (mouse) => {
            if (mouse.button === Qt.RightButton) {
                root.close()
                return
            }
            aim(mouse)
        }
        onReleased: (mouse) => {
            if (mouse.button !== Qt.LeftButton || !root._grab)
                return
            aim(mouse)
            Haptics.confirm()
            root.picked(root._color)
            root.close()
        }
        onExited: if (!pressed) root._aiming = false

        Rectangle {
            id: hint
            anchors.horizontalCenter: parent.horizontalCenter
            y: Theme.spacing2xl + ((Overlay.overlay && Overlay.overlay.SafeArea)
                                   ? Overlay.overlay.SafeArea.margins.top : 0)
            width: hintText.implicitWidth + Theme.spacing2xl * 2
                   + (cancelButton.visible ? cancelButton.width : 0)
            height: Theme.controlHeight
            radius: Theme.radiusPill
            color: Theme.c.colorEyedropper.hintBackground
            border.width: Theme.c.colorEyedropper.hintBorderWidth
            border.color: Theme.c.colorEyedropper.hintBorder

            Text {
                id: hintText
                anchors.verticalCenter: parent.verticalCenter
                x: Theme.spacing2xl
                text: Theme.touchUi ? qsTr("Drag to a colour and lift to pick it")
                                    : qsTr("Click a colour to pick it. Esc cancels.")
                color: Theme.c.colorEyedropper.hintForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
            }

            // A finger has no right click and maybe no Back key (the desktop layout on a tablet).
            IconButton {
                id: cancelButton
                visible: Theme.touchUi
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                buttonSize: parent.height
                glyph: Theme.icons.x
                tooltip: qsTr("Cancel")
                onClicked: root.close()
            }
        }

        Item {
            id: loupe
            width: root.lensSize
            height: root.lensSize + Theme.spacingLg + hexPill.height
            visible: root._aiming && root._grab !== null
            // Beside the pointer, or well above a finger; flipped at the edges. Never over the
            // pixels it magnifies, which would show up in the next grab.
            readonly property real gap: Theme.touchUi ? 56 : 20
            x: root._pos.x + gap + width > area.width ? root._pos.x - gap - width : root._pos.x + gap
            y: Theme.touchUi
               ? (root._pos.y - gap - height < 0 ? root._pos.y + gap : root._pos.y - gap - height)
               : (root._pos.y + gap + height > area.height ? root._pos.y - gap - height : root._pos.y + gap)

            Item {
                id: lens
                width: root.lensSize
                height: root.lensSize
                visible: false
                layer.enabled: true

                Image {
                    source: root._grab ? root._grab.url : ""
                    smooth: false
                    cache: false
                    width: area.width * root.zoom
                    height: area.height * root.zoom
                    x: lens.width / 2 - (root._pos.x + 0.5) * root.zoom
                    y: lens.height / 2 - (root._pos.y + 0.5) * root.zoom
                }
            }

            Rectangle {
                id: lensMask
                width: root.lensSize
                height: root.lensSize
                radius: Theme.radiusPill
                visible: false
                layer.enabled: true
            }

            MultiEffect {
                width: root.lensSize
                height: root.lensSize
                source: lens
                maskEnabled: true
                maskSource: lensMask
                maskThresholdMin: 0.5
                maskSpreadAtMin: 1.0
            }

            // Target pixel.
            Rectangle {
                x: root.lensSize / 2 - width / 2
                y: root.lensSize / 2 - height / 2
                width: root.zoom + 2
                height: root.zoom + 2
                color: "transparent"
                border.width: Theme.c.colorEyedropper.crosshairBorderWidth
                border.color: Theme.c.colorEyedropper.crosshairBorder
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -1
                    color: "transparent"
                    border.width: Theme.c.colorEyedropper.crosshairShadowWidth
                    border.color: Theme.c.colorEyedropper.crosshairShadow
                }
            }

            // Ring in the sampled colour, with hairlines so it reads on any backdrop.
            Rectangle {
                width: root.lensSize
                height: root.lensSize
                radius: Theme.radiusPill
                color: "transparent"
                border.width: Theme.c.colorEyedropper.ringBorderWidth
                border.color: root._color
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -1
                    radius: Theme.radiusPill
                    color: "transparent"
                    border.width: Theme.c.colorEyedropper.ringShadowWidth
                    border.color: Theme.c.colorEyedropper.ringShadow
                }
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 4
                    radius: Theme.radiusPill
                    color: "transparent"
                    border.width: Theme.c.colorEyedropper.ringHighlightWidth
                    border.color: Theme.c.colorEyedropper.ringHighlight
                }
            }

            Rectangle {
                id: hexPill
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                width: hexText.implicitWidth + Theme.spacingXl * 2
                height: 24
                radius: Theme.radiusPill
                color: Theme.c.colorEyedropper.pillBackground
                border.width: Theme.c.colorEyedropper.pillBorderWidth
                border.color: Theme.c.colorEyedropper.pillBorder

                Text {
                    id: hexText
                    anchors.centerIn: parent
                    text: root._color.toString().toUpperCase()
                    color: Theme.c.colorEyedropper.pillForeground
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
            }
        }
    }
}
