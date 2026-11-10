import QtQuick
import Drift

// Phone transition picker: adds a transition at an empty cut, or swaps the kind of an existing one.
AndroidBottomSheet {
    id: root

    property int trackIndex: -1
    property int leftClipIndex: -1
    property string transitionId: ""
    property string currentKind: ""

    title: qsTr("Transitions")
    sheetHeightFraction: 0.6

    function openFor(track, leftClip, id, kind) {
        trackIndex = track
        leftClipIndex = leftClip
        transitionId = id
        currentKind = kind
        open()
    }

    TransitionPickerGrid {
        anchors.fill: parent
        trackIndex: root.trackIndex
        currentKind: root.currentKind
        onPicked: (kind) => {
            if (root.transitionId === "") {
                EditorState.addTransition(root.trackIndex, root.leftClipIndex, kind,
                                          EditorState.preferences.transitionDurationSeconds)
                EditorState.selectTransition(root.trackIndex, root.leftClipIndex)
            } else {
                EditorState.setTransitionKind(root.trackIndex, root.transitionId, kind)
            }
            root.dismiss()
        }
        onApplyAllRequested: (kind, selectionOnly) => {
            EditorState.applyTransitionToCuts(root.trackIndex, kind, selectionOnly)
            root.dismiss()
        }
    }
}
