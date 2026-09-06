import QtQuick
import QtQuick.Controls.Basic
import Drift

// Offers to spread a just-added transition across the rest of a multi-clip
// selection, instead of making the user drop it onto every boundary by hand.
// Fired by AppController.transitionSelectionApplyAvailable(); see addTransition().
Item {
    id: root

    property bool active: false
    property var selectionPairs: []
    property string kind: ""
    property double durationSeconds: 0

    // selectionPairs holds plain track/clip indices, which a structural edit (delete a clip,
    // undo, load another project...) can silently repoint at different clips. Rather than try
    // to revalidate them, just drop the offer the moment anything in the project could have
    // moved them — tracksChanged covers every edit (including undo/redo), projectReset covers
    // swapping the document itself.
    Connections {
        target: EditorState
        function onTracksChanged() { root.active = false }
        function onProjectReset() { root.active = false }
    }

    readonly property real maxWidth: parent ? Math.min(460, parent.width - Theme.spacing3xl * 2) : 460

    anchors.horizontalCenter: parent.horizontalCenter
    anchors.bottom: parent.bottom
    anchors.bottomMargin: Theme.spacing3xl
    width: banner.width
    height: banner.height
    visible: active
    z: 1000

    Connections {
        target: EditorState
        function onTransitionSelectionApplyAvailable(pairs, appliedKind, appliedDurationSeconds) {
            root.selectionPairs = pairs
            root.kind = appliedKind
            root.durationSeconds = appliedDurationSeconds
            root.active = true
            hideTimer.restart()
        }
    }

    Timer {
        id: hideTimer
        interval: 8000
        onTriggered: root.active = false
    }

    Rectangle {
        id: banner
        // Fixed at the viewport-clamped max rather than shrink-wrapped to content: content's own
        // width (and therefore implicitWidth) is derived from banner.width below, so sizing
        // banner.width from content.implicitWidth would be circular and settle on an arbitrary,
        // too-small fixed point instead of the message's real preferred width.
        width: root.maxWidth
        height: content.implicitHeight + Theme.spacingLg * 2
        radius: Theme.radiusMd
        color: Theme.panelBackground
        border.width: Theme.borderWidth
        border.color: Theme.panelBorder

        // Flow rather than Row: on a narrow/portrait viewport the message and the button pair
        // no longer fit on one line, so the buttons drop to a line of their own instead of
        // running off the edge of the screen.
        Flow {
            id: content
            x: Theme.spacingXl
            y: Theme.spacingLg
            width: banner.width - Theme.spacingXl * 2
            spacing: Theme.spacingLg

            Text {
                width: Math.min(implicitWidth, content.width)
                text: qsTr("Apply this transition to the other %n selected clip(s)?", "",
                          Math.max(0, root.selectionPairs.length - 1))
                color: Theme.panelForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                wrapMode: Text.WordWrap
            }

            Row {
                spacing: Theme.spacingLg

                ThemedButton {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Apply to All")
                    variant: "primary"
                    onClicked: {
                        const count = EditorState.applyTransitionToSelection(
                                root.selectionPairs, root.kind, root.durationSeconds)
                        if (count > 0)
                            root.active = false
                    }
                }

                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: Theme.icons.x
                    iconSize: Theme.iconSizeMd
                    variant: "text"
                    tooltip: qsTr("Dismiss")
                    onClicked: root.active = false
                }
            }
        }
    }
}
