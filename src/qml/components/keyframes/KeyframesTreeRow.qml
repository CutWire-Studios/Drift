pragma ComponentBehavior: Bound
import QtQuick
import Drift
import "KeyframesModel.js" as Model

Item {
    id: root

    required property var row
    required property int track
    required property int clip
    required property real playhead
    required property real displayPlayhead
    required property bool curvesMode
    required property bool curveShown
    required property bool focused

    readonly property real rowHeight: Theme.controlHeightSm + Theme.spacingMd
    readonly property bool isGroup: row.type === "group"
    readonly property bool empty: row.type === undefined
    readonly property color accent: Theme.keyframeCurveColor(isGroup || empty ? "" : row.keyProp)
    readonly property real halfFrame: {
        void EditorState.tracksRevision
        return 0.5 / Math.max(1, EditorState.projectFile.projectFps())
    }
    readonly property bool hasKeys: !isGroup && !!row.keys && row.keys.length > 0
    readonly property var keysHere: isGroup ? [] : Model.refsForRowAt(row, displayPlayhead, halfFrame)
    readonly property bool keyHere: keysHere.length > 0
    readonly property bool hasPrev: hasKeys && Model.prevKeyTime([row], displayPlayhead) !== null
    readonly property bool hasNext: hasKeys && Model.nextKeyTime([row], displayPlayhead) !== null

    signal focusRequested()
    signal groupToggled()
    signal curveShownToggled(bool shown)

    function toggleKeyHere() {
        if (!isGroup && Model.refsForRowAt(row, playhead, halfFrame).length > 0)
            EditorState.keyframes.removeKeysAt(track, clip, row.tracks, playhead)
        else
            EditorState.keyframes.addKeysAt(track, clip, row.tracks, playhead)
    }

    function toggleAnimation() {
        const target = !row.enabled
        for (let i = 0; i < row.tracks.length; ++i) {
            if (EditorState.clipPropertyKeyframesEnabled(track, clip, row.tracks[i]) !== target)
                EditorState.toggleClipPropertyKeyframesEnabled(track, clip, row.tracks[i])
        }
    }

    height: rowHeight
    visible: !empty

    Rectangle {
        anchors.fill: parent
        color: root.isGroup ? Theme.c.keyframesTreeRow.groupBackground
                            : (root.focused ? Theme.c.keyframesTreeRow.backgroundFocused
                                            : (hover.hovered ? Theme.c.keyframesTreeRow.backgroundHover
                                                             : "transparent"))
    }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: Theme.c.panelFrame.borderWidth
        color: Theme.c.keyframesTreeRow.border
    }

    HoverHandler { id: hover }

    TapHandler {
        onTapped: root.isGroup ? root.groupToggled() : root.focusRequested()
    }

    Row {
        visible: root.isGroup
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingMd
        anchors.rightMargin: Theme.spacingMd
        spacing: Theme.spacingSm

        Item {
            width: Theme.iconSizeSm
            height: parent.height

            IconGlyph {
                anchors.centerIn: parent
                glyph: root.isGroup && root.row.collapsed ? Theme.icons.chevronRight : Theme.icons.chevronDown
                iconSize: 12
                iconColor: Theme.c.keyframesTreeRow.groupForeground
            }
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, parent.width - Theme.iconSizeSm - countText.width - parent.spacing * 2)
            text: root.isGroup ? root.row.label : ""
            elide: Text.ElideRight
            color: Theme.c.keyframesTreeRow.groupForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            font.weight: Font.Medium
        }

        Text {
            id: countText
            anchors.verticalCenter: parent.verticalCenter
            visible: root.isGroup && root.row.count > 0
            text: root.isGroup ? String(root.row.count) : ""
            color: Theme.c.keyframesTreeRow.foregroundMuted
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }

    Row {
        visible: !root.isGroup
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingXl
        anchors.rightMargin: Theme.spacingMd
        spacing: Theme.spacingSm

        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            buttonSize: 20
            iconSize: 14
            glyph: Theme.icons.clock
            active: !root.empty && !!root.row.animated && !!root.row.enabled
            enabled: root.hasKeys
            tooltip: !root.hasKeys
                     ? qsTr("%1 has no keyframes yet").arg(root.row.label ?? "")
                     : (root.row.enabled
                        ? qsTr("Stop animating %1 (keys are kept)").arg(root.row.label)
                        : qsTr("Animate %1").arg(root.row.label))
            onClicked: root.toggleAnimation()
        }

        Text {
            id: label
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(24, parent.width - scrubber.width - nav.width - curveCheck.width
                                - 20 - parent.spacing * 4)
            text: root.isGroup ? "" : (root.row.label ?? "")
            elide: Text.ElideRight
            color: root.hasKeys && root.row.enabled ? Theme.c.keyframesTreeRow.foreground
                                                    : Theme.c.keyframesTreeRow.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            font.weight: root.hasKeys && root.row.enabled ? Font.Medium : Font.Normal
        }

        KeyframesValueScrubber {
            id: scrubber
            anchors.verticalCenter: parent.verticalCenter
            visible: !root.isGroup
            row: root.row
            track: root.track
            clip: root.clip
            playhead: root.displayPlayhead
            onActiveFocusChanged: if (activeFocus) root.focusRequested()
        }

        Row {
            id: nav
            anchors.verticalCenter: parent.verticalCenter
            spacing: 0

            IconButton {
                buttonSize: 18
                iconSize: 12
                glyph: Theme.icons.chevronLeft
                tooltip: qsTr("Previous keyframe")
                enabled: root.hasPrev
                onClicked: {
                    root.focusRequested()
                    EditorState.playheadSeconds = Model.prevKeyTime([root.row], root.playhead)
                }
            }

            IconButton {
                buttonSize: 20
                iconSize: 14
                glyph: root.keyHere ? Theme.icons.diamondMinus : Theme.icons.diamondPlus
                tooltip: root.keyHere
                         ? qsTr("Remove %1's keyframe at the playhead").arg(root.isGroup ? "" : root.row.label)
                         : qsTr("Add a keyframe for %1 at the playhead").arg(root.isGroup ? "" : root.row.label)
                onClicked: {
                    root.focusRequested()
                    root.toggleKeyHere()
                }
            }

            IconButton {
                buttonSize: 18
                iconSize: 12
                glyph: Theme.icons.chevronRight
                tooltip: qsTr("Next keyframe")
                enabled: root.hasNext
                onClicked: {
                    root.focusRequested()
                    EditorState.playheadSeconds = Model.nextKeyTime([root.row], root.playhead)
                }
            }
        }

        ThemedCheckBox {
            id: curveCheck
            anchors.verticalCenter: parent.verticalCenter
            visible: root.curvesMode
            width: visible ? implicitWidth : 0
            enabled: root.hasKeys
            checked: root.curveShown
            tooltip: qsTr("Show this curve")
            onToggled: root.curveShownToggled(checked)
        }
    }
}
