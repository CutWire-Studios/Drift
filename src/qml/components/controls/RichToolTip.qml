pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Drift

// ThemedToolTip with an optional shortcut chip, one-line body, state pill and, after an
// extended hover, a preview image or illustration. With only `text` set it renders like a
// plain tooltip.
ThemedToolTip {
    id: root

    property string actionId: ""
    property string title: ""
    property string body: ""
    property string shortcut: ""
    property string state: ""
    property url preview: ""
    // A preview that is one row of equal square frames: plays through them while shown.
    property int previewFrames: 1
    property Component illustration: null
    property int extendedDelay: 1200

    readonly property string _title: title.length > 0 ? title : text
    readonly property string _shortcut: {
        if (shortcut.length > 0)
            return shortcut
        if (actionId.length === 0)
            return ""
        return Theme.shortcutDisplay(EditorState.shortcutFor(actionId))
    }
    readonly property bool _hasExtra: preview.toString().length > 0 || illustration !== null
    property bool _extended: false

    Timer {
        interval: Math.max(0, root.extendedDelay - root.delay)
        running: root.visible && root._hasExtra
        onTriggered: root._extended = true
    }
    onVisibleChanged: {
        if (!visible)
            _extended = false
    }

    contentItem: Column {
        spacing: Theme.spacingSm
        width: Math.min(implicitWidth, 320 - root.leftPadding - root.rightPadding)

        Row {
            spacing: Theme.spacingLg

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root._title
                color: Theme.c.themedToolTip.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                font.weight: root.body.length > 0 ? Font.DemiBold : Font.Normal
                wrapMode: Text.WordWrap
                width: Math.min(implicitWidth, 260)
            }

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                visible: root._shortcut.length > 0
                implicitWidth: chipText.implicitWidth + Theme.spacingMd * 2
                implicitHeight: chipText.implicitHeight + Theme.spacingXs * 2
                width: implicitWidth
                height: implicitHeight
                radius: Theme.c.themedToolTip.radius
                color: Theme.c.themedToolTip.chipBackground

                Text {
                    id: chipText
                    anchors.centerIn: parent
                    text: root._shortcut
                    color: Theme.c.themedToolTip.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
            }
        }

        Text {
            visible: root.body.length > 0
            text: root.body
            color: Theme.c.themedToolTip.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            wrapMode: Text.WordWrap
            width: Math.min(implicitWidth, 320 - root.leftPadding - root.rightPadding)
        }

        Rectangle {
            visible: root.state.length > 0
            implicitWidth: pillText.implicitWidth + Theme.spacingLg * 2
            implicitHeight: pillText.implicitHeight + Theme.spacingXs * 2
            width: implicitWidth
            height: implicitHeight
            radius: Theme.radiusPill
            color: Theme.c.themedToolTip.pillBackground

            Text {
                id: pillText
                anchors.centerIn: parent
                text: root.state
                color: Theme.c.themedToolTip.pillForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
        }

        Item {
            id: stripView
            visible: root._extended && root.previewFrames > 1 && root.preview.toString().length > 0
            width: 160
            height: visible ? width : 0
            clip: true
            property real scrub: 0
            readonly property int frame: Math.max(0, Math.min(root.previewFrames - 1,
                                                              Math.round(scrub * (root.previewFrames - 1))))

            NumberAnimation on scrub {
                running: stripView.visible
                from: 0
                to: 1
                duration: 1400
                loops: Animation.Infinite
            }

            Image {
                source: stripView.visible ? root.preview : ""
                height: stripView.height
                width: stripView.height * root.previewFrames
                x: -stripView.frame * stripView.height
                fillMode: Image.Stretch
                asynchronous: true
            }
        }

        Image {
            visible: root._extended && root.previewFrames <= 1 && root.preview.toString().length > 0
            source: visible ? root.preview : ""
            width: visible ? 320 - root.leftPadding - root.rightPadding : 0
            height: visible ? width * 0.5625 : 0
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
        }

        Loader {
            active: root._extended && root.illustration !== null
            visible: active
            sourceComponent: root.illustration
        }
    }
}
