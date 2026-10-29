import QtQuick
import QtQuick.Controls.Basic
import Drift

// The phone's home for a keyframe segment's easing. The desktop opens EasingPickerPopup beside
// the key; here the picker takes the sheet, so the curve can be watched on the preview while
// it is chosen, the same way the fade sheet works.
AndroidBottomSheet {
    id: root

    title: qsTr("Easing")
    doneText: qsTr("Done")
    blocking: false
    onDoneRequested: root.dismiss()

    property int trackIndex: -1
    property int clipIndex: -1
    property string propKey: ""
    property string propLabel: ""
    property real keySeconds: 0

    function openFor(track, clip, prop, label, seconds) {
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
            onCloseRequested: root.dismiss()
        }
    }
}
