import QtQuick
import Drift

Item {
    id: bar
    property var host
    width: parent.width
    height: 20
    clip: true

    readonly property real buttonSize: 20
    readonly property real gap: 4
    readonly property bool showLabel: width >= 160
    readonly property string collapseTip: host.curveEditing
        ? qsTr("Collapse curve editor to compact view (88px)")
        : qsTr("Expand curve editor to view and edit Bezier handles (180px)")
    readonly property string beatTip: EditorState.beatAnalysisRunning
        ? qsTr("Analyzing…")
        : (EditorState.beatGridVisible ? qsTr("Hide the beat markers")
                                       : qsTr("Find the beat and show markers"))
    readonly property string onsetTip: EditorState.beatAnalysisRunning
        ? qsTr("Analyzing…")
        : (EditorState.onsetsVisible ? qsTr("Hide hits")
                                     : qsTr("Find beats and hits in the audio under this clip"))
    readonly property string handlesTip: host.showHandles
        ? qsTr("Hide Bezier handles")
        : qsTr("Show Bezier handles. Without them, drag a curve to bend it")

    // Buttons drop out lowest-priority first; the overflow button's menu has all of them.
    readonly property var fit: {
        const wants = [true, host.curveEditing, true, true]
        let total = 0
        for (const w of wants)
            total += w ? 1 : 0
        const avail = width - (showLabel ? 60 : 0)
        const cap = Math.max(0, Math.floor((avail + gap) / (buttonSize + gap)))
        const overflow = total > cap
        let room = overflow ? Math.max(0, cap - 1) : cap
        const out = { overflow: overflow }
        const keys = ["toggle", "handles", "beat", "onsets"]
        for (let i = 0; i < wants.length; ++i) {
            out[keys[i]] = wants[i] && room > 0
            if (out[keys[i]])
                --room
        }
        return out
    }

    function toggleBeat() {
        if (EditorState.beatGridVisible)
            EditorState.beatGridVisible = false
        else
            host.showBeatLayer(true)
    }
    function toggleOnsets() {
        if (EditorState.onsetsVisible)
            EditorState.onsetsVisible = false
        else
            host.showBeatLayer(false)
    }

    Text {
        id: label
        visible: bar.showLabel
        anchors.left: parent.left
        anchors.right: buttons.left
        anchors.rightMargin: bar.gap
        anchors.verticalCenter: parent.verticalCenter
        text: qsTr("Keyframes")
        elide: Text.ElideRight
        color: Theme.c.keyframeGraphHeaderBar.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
        font.weight: Font.Medium
    }

    Row {
        id: buttons
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        spacing: bar.gap

        IconButton {
            visible: bar.fit.toggle
            glyph: host.curveEditing ? Theme.icons.minimize : Theme.icons.maximize
            variant: "text"
            buttonSize: bar.buttonSize
            iconSize: 14
            tooltip: bar.collapseTip
            onClicked: host.toggleExpanded()
        }

        IconButton {
            visible: bar.fit.handles
            glyph: Theme.icons.spline
            variant: "text"
            buttonSize: bar.buttonSize
            iconSize: 14
            active: host.showHandles
            tooltip: bar.handlesTip
            onClicked: host.showHandles = !host.showHandles
        }

        IconButton {
            visible: bar.fit.beat
            glyph: Theme.icons.music
            variant: "text"
            buttonSize: bar.buttonSize
            iconSize: 14
            active: EditorState.beatGridVisible
            enabled: !EditorState.beatAnalysisRunning
            tooltip: bar.beatTip
            onClicked: bar.toggleBeat()
        }

        IconButton {
            visible: bar.fit.onsets
            glyph: Theme.icons.audioLines
            variant: "text"
            buttonSize: bar.buttonSize
            iconSize: 14
            active: EditorState.onsetsVisible
            enabled: !EditorState.beatAnalysisRunning
            tooltip: bar.onsetTip
            onClicked: bar.toggleOnsets()
        }

        IconButton {
            id: overflowButton
            visible: bar.fit.overflow
            glyph: Theme.icons.ellipsis
            variant: "text"
            buttonSize: bar.buttonSize
            iconSize: 14
            tooltip: qsTr("More keyframe tools")
            onClicked: overflowMenu.popup(overflowButton, 0, overflowButton.height)
        }
    }

    ThemedContextMenu {
        id: overflowMenu

        ThemedMenuItem {
            text: bar.collapseTip
            icon.name: host.curveEditing ? Theme.icons.minimize : Theme.icons.maximize
            onTriggered: host.toggleExpanded()
        }
        ThemedMenuItem {
            visible: host.curveEditing
            text: bar.handlesTip
            icon.name: host.showHandles ? Theme.icons.check : Theme.icons.spline
            onTriggered: host.showHandles = !host.showHandles
        }
        ThemedMenuItem {
            enabled: !EditorState.beatAnalysisRunning
            text: bar.beatTip
            icon.name: EditorState.beatGridVisible ? Theme.icons.check : Theme.icons.music
            onTriggered: bar.toggleBeat()
        }
        ThemedMenuItem {
            enabled: !EditorState.beatAnalysisRunning
            text: bar.onsetTip
            icon.name: EditorState.onsetsVisible ? Theme.icons.check : Theme.icons.audioLines
            onTriggered: bar.toggleOnsets()
        }
    }
}
