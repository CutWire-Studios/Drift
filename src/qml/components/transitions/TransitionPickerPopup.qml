import QtQuick
import QtQuick.Controls.Basic
import Drift
// Desktop popover for choosing a transition at a cut: adds one when there is none, otherwise swaps its kind.
Popup {
    id: root

    property int trackIndex: -1
    property int leftClipIndex: -1
    property string transitionId: ""
    property string currentKind: ""
    readonly property real edgeMargin: Theme.spacingLg

    parent: Overlay.overlay
    width: 420
    height: Math.min(460, (Overlay.overlay ? Overlay.overlay.height : 460) - edgeMargin * 2)
    padding: Theme.spacingXs
    modal: false
    dim: false
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    // (x, y) is in anchorItem coordinates, or in window coordinates when anchorItem is null.
    function openFor(track, leftClip, id, kind, anchorItem, x, y) {
        trackIndex = track
        leftClipIndex = leftClip
        transitionId = id
        currentKind = kind
        const overlay = Overlay.overlay
        let pos = Qt.point(x, y)
        if (anchorItem && overlay)
            pos = anchorItem.mapToItem(overlay, x, y)
        if (overlay) {
            root.x = Math.max(edgeMargin, Math.min(overlay.width - width - edgeMargin, pos.x))
            root.y = Math.max(edgeMargin, Math.min(overlay.height - height - edgeMargin, pos.y))
        } else {
            root.x = pos.x
            root.y = pos.y
        }
        open()
    }

    onClosed: EditorState.endTransitionPreview()

    background: Rectangle {
        color: Theme.c.transitionPicker.background
        border.width: Theme.c.transitionPicker.borderWidth
        border.color: Theme.c.transitionPicker.border
        radius: Theme.c.transitionPicker.radius
    }

    enter: Transition {
        ParallelAnimation {
            NumberAnimation {
                property: "opacity"
                from: 0.0
                to: 1.0
                duration: Theme.durationBase
                easing.type: Theme.easing
            }
            NumberAnimation {
                property: "scale"
                from: 0.96
                to: 1.0
                duration: Theme.durationBase
                easing.type: Theme.easing
            }
        }
    }

    exit: Transition {
        ParallelAnimation {
            NumberAnimation {
                property: "opacity"
                from: 1.0
                to: 0.0
                duration: Theme.durationFast
                easing.type: Theme.easing
            }
            NumberAnimation {
                property: "scale"
                from: 1.0
                to: 0.96
                duration: Theme.durationFast
                easing.type: Theme.easing
            }
        }
    }

    contentItem: TransitionPickerGrid {
        trackIndex: root.trackIndex
        currentKind: root.currentKind
        previewLeftClip: root.opened ? root.leftClipIndex : -1
        onPicked: (kind) => {
            if (root.transitionId === "") {
                EditorState.addTransition(root.trackIndex, root.leftClipIndex, kind,
                                          EditorState.preferences.transitionDurationSeconds)
                EditorState.selectTransition(root.trackIndex, root.leftClipIndex)
            } else {
                EditorState.setTransitionKind(root.trackIndex, root.transitionId, kind)
            }
            root.close()
        }
        onApplyAllRequested: (kind, selectionOnly) => {
            EditorState.applyTransitionToCuts(root.trackIndex, kind, selectionOnly)
            root.close()
        }
    }
}
