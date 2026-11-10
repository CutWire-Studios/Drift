import QtQuick
import QtQuick.Controls.Basic
import Drift

// Scrolling script shared by the panel and the preview overlay. EditorState.teleprompter.position
// is in lines so both views stay in step whatever their font scale.
Item {
    id: root

    property real fontScale: 1
    property color textColor: Theme.c.teleprompterView.foreground
    property color markerColor: Theme.c.teleprompterView.marker
    // Where the line being read sits, as a fraction of the view height.
    property real readLineRatio: 0.3

    readonly property real lineHeight: Math.round(EditorState.teleprompter.fontSize * fontScale * 1.4)
    readonly property real maxY: Math.max(0, flick.contentHeight - flick.height)
    readonly property real readLineY: height * readLineRatio

    function pushPosition() {
        EditorState.teleprompter.position = Math.max(0, flick.contentY) / root.lineHeight
    }

    Flickable {
        id: flick
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: scriptText.height + root.readLineY + Math.max(0, height - root.readLineY - root.lineHeight)
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar { policy: ScrollBar.AsNeeded }

        Binding {
            target: flick
            property: "contentY"
            value: Math.min(root.maxY, EditorState.teleprompter.position * root.lineHeight)
            when: !flick.moving
        }

        onContentYChanged: {
            if (moving || wheel.active)
                root.pushPosition()
        }

        Text {
            id: scriptText
            x: Theme.spacing2xl
            y: root.readLineY
            width: flick.width - Theme.spacing2xl * 2
            text: EditorState.teleprompter.script
            wrapMode: Text.Wrap
            font.family: Theme.fontFamily
            font.pixelSize: EditorState.teleprompter.fontSize * root.fontScale
            lineHeightMode: Text.FixedHeight
            lineHeight: root.lineHeight
            color: root.textColor
        }

        WheelHandler {
            id: wheel
            property bool active: false
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: (event) => {
                active = true
                const dy = event.pixelDelta.y !== 0 ? event.pixelDelta.y : event.angleDelta.y / 120 * root.lineHeight * 2
                flick.contentY = Math.max(0, Math.min(root.maxY, flick.contentY - dy))
                active = false
            }
        }
    }

    Rectangle {
        y: root.readLineY
        width: parent.width
        height: 1
        color: root.markerColor
        opacity: 0.5
    }

    Connections {
        target: EditorState.teleprompter
        function onPositionChanged() {
            if (EditorState.teleprompter.playing && root.height > 0 && root.maxY > 0
                    && EditorState.teleprompter.position * root.lineHeight >= root.maxY) {
                EditorState.teleprompter.playing = false
                EditorState.teleprompter.position = root.maxY / root.lineHeight
            }
        }
    }
}
