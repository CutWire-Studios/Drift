import QtQuick
import QtQuick.Controls.Basic
import Drift

// The phone's home for a keyframe segment's easing. The desktop opens EasingPickerPopup beside
// the key; here the picker takes the sheet, so the curve can be watched on the preview while
// it is chosen, the same way the fade sheet works.
AndroidBottomSheet {
    id: root

    title: root.curveHost ? qsTr("Curve") : qsTr("Easing")
    doneText: qsTr("Done")
    blocking: false
    onDoneRequested: root.dismiss()

    property int trackIndex: -1
    property int clipIndex: -1
    property string propKey: ""
    property string propLabel: ""
    property real keySeconds: 0

    property var curveHost: null
    property var curveConfig: ({})

    function openForCurve(config, host) {
        curveConfig = config
        curveHost = host
        if (!opened)
            open()
    }

    function openFor(track, clip, prop, label, seconds) {
        curveHost = null
        trackIndex = track
        clipIndex = clip
        propKey = prop
        propLabel = label
        keySeconds = seconds
        if (!opened)
            open()
    }

    Connections {
        target: EditorState
        function onSelectionChanged() {
            if (root.curveHost)
                return
            if (EditorState.selectedTrack !== root.trackIndex || EditorState.selectedClip !== root.clipIndex)
                root.dismiss()
        }
    }

    Flickable {
        id: scroll
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: picker.y + picker.implicitHeight + Theme.spacingLg
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar { }

        EasingPicker {
            id: picker
            x: Theme.pagePadding + root.safeLeft
            y: Theme.spacingLg
            width: scroll.width - x - Theme.pagePadding - root.safeRight
            trackIndex: root.trackIndex
            clipIndex: root.clipIndex
            propKey: root.propKey
            propLabel: root.propLabel
            keySeconds: root.keySeconds
            mode: root.curveHost ? "curve" : "keyframe"
            currentSpec: root.curveHost ? root.curveHost.currentSpec : null
            currentStyle: root.curveHost ? root.curveHost.currentStyle : ""
            styles: root.curveHost ? EditorState.curveStyles(root.curveConfig.domain) : []
            allowFunctions: !!root.curveConfig.allowFunctions
            unitRange: !!root.curveConfig.unitRange
            onPicked: (spec) => { if (root.curveHost) root.curveHost.applyCurve(spec) }
            onStylePicked: (id) => { if (root.curveHost) root.curveHost.applyStyle(id) }
            onCloseRequested: root.dismiss()
        }
    }
}
