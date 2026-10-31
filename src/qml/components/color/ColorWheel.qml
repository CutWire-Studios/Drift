import QtQuick
import Drift

Item {
    id: root

    property string label: ""
    property point offset: Qt.point(0, 0)
    property real luma: 0
    property real size: 140
    property bool animated: false

    signal began()
    signal offsetMoved(real x, real y)
    signal lumaMoved(real v)
    signal ended()

    readonly property real puckSize: Theme.touchInput ? 18 : 12
    readonly property real chromeHeight: implicitHeight - size
    readonly property bool modified: offset.x !== 0 || offset.y !== 0 || luma !== 0

    function resetOffset() {
        began()
        offsetMoved(0, 0)
        ended()
    }

    function resetAll() {
        began()
        offsetMoved(0, 0)
        lumaMoved(0)
        ended()
    }

    implicitWidth: size
    width: implicitWidth
    implicitHeight: header.height + size + lumaSlider.height + Theme.spacingMd * 2

    Item {
        id: header
        width: parent.width
        height: Theme.controlHeightSm

        Text {
            anchors.left: parent.left
            anchors.right: keyMark.visible ? keyMark.left : resetButton.left
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
            text: root.label
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        KeyframeDiamond {
            id: keyMark
            anchors.right: resetButton.left
            anchors.verticalCenter: parent.verticalCenter
            visible: root.animated
            animated: true
            tooltip: qsTr("Animated. Editing sets keyframes (needs Auto keyframes on).")
        }

        IconButton {
            id: resetButton
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            glyph: Theme.icons.reset
            tooltip: qsTr("Reset %1").arg(root.label)
            buttonSize: Theme.controlHeightSm
            enabled: root.modified
            onClicked: root.resetAll()
        }
    }

    Item {
        id: disc
        y: header.height + Theme.spacingMd
        anchors.horizontalCenter: parent.horizontalCenter
        width: root.size
        height: root.size

        ColorWheelPad {
            id: pad
            anchors.fill: parent
            offset: root.offset
            neutralColor: Theme.c.colorWheel.discNeutral
            rimStrength: Theme.c.colorWheel.rimStrength
            ringColor: Theme.c.colorWheel.ring
            ringActiveColor: Theme.c.colorWheel.ringActive
            focusRingColor: Theme.focusRing
            ringWidth: Theme.c.colorWheel.ringWidth
            crosshairColor: Theme.c.colorWheel.crosshair
            crosshairOpacity: Theme.c.colorWheel.crosshairOpacity
            crosshairWidth: Theme.c.vec2ParamPad.crosshairWidth
            puckColor: Theme.c.colorWheel.puck
            puckBorderColor: Theme.c.colorWheel.puckBorder
            puckBorderWidth: Theme.c.colorWheel.puckBorderWidth
            puckSize: root.puckSize
            vectorColor: Theme.c.colorWheel.vector
            vectorOpacity: Theme.c.colorWheel.vectorOpacity
            onBegan: root.began()
            onOffsetMoved: (x, y) => root.offsetMoved(x, y)
            onEnded: root.ended()
            onResetRequested: root.resetOffset()

            HoverHandler {
                id: discHover
            }
        }

        ThemedToolTip {
            visible: discHover.hovered && !pad.pressed
            text: qsTr("Drag to tint. Shift for fine control. Double-click to reset the color.")
        }
    }

    GradeSlider {
        id: lumaSlider
        y: disc.y + root.size + Theme.spacingMd
        width: parent.width
        label: qsTr("Brightness")
        from: -1
        to: 1
        defaultValue: 0
        value: root.luma
        gradient: Theme.c.colorGradeTracks.luma
        onBegan: root.began()
        onMoved: v => root.lumaMoved(v)
        onEnded: root.ended()
    }
}
