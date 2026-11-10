import QtQuick
import QtQuick.Controls.Basic
import Drift

// Pinned ruler and bookmark strip. Stays under the playhead head so a drag on the
// head wins, and follows vertical scroll so it never leaves the viewport.
Item {
    id: seekStrip

    property var panel
    property Flickable view
    property bool playheadDragging: false
    property var renameDialog
    property var renameField
    property alias bookmarkRenameIndex: bookmarkRow.renameIndex

    width: parent.width
    y: view.contentY
    z: 2
    height: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight

    TimelineSeekStripFill { }
    TimelineSeekStripEdge { }

    MouseArea {
        id: rulerScrub
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        preventStealing: true
        // Leave room for the playhead head so its drag can start first.
        z: 1

        function scrubTo(x) {
            EditorState.playheadSeconds =
                EditorState.snapTime(Math.max(0, x) / seekStrip.panel.pxPerSecond)
        }

        // Seeking is not a selection change: scrubbing to look at a
        // different point used to drop the clip you were editing.
        onPressed: (mouse) => {
            seekStrip.panel.forceActiveFocus()
            seekStrip.panel.beginPlayheadSeek()
            scrubTo(mouse.x)
        }
        onPositionChanged: (mouse) => {
            if (pressed)
                scrubTo(mouse.x)
        }
        onReleased: seekStrip.panel.endPlayheadSeek()
        onCanceled: seekStrip.panel.endPlayheadSeek()
        // MouseArea would otherwise swallow wheel and block Ctrl-zoom.
        onWheel: (wheel) => seekStrip.panel.handleTimelineWheel(wheel)

        ThemedToolTip {
            text: qsTr("Click or drag to seek")
            visible: rulerScrub.containsMouse && !rulerScrub.pressed
                     && !seekStrip.playheadDragging
        }
    }

    TimelineRuler {
        panel: seekStrip.panel
        view: seekStrip.view
    }

    Item {
        id: bookmarkRow
        y: Theme.timelineRulerHeight
        width: parent.width
        height: Theme.timelineBookmarkRowHeight
        z: 2

        property int renameIndex: -1

        Repeater {
            model: EditorState.bookmarks
            delegate: TimelineBookmarkMarker {
                row: bookmarkRow
                panel: seekStrip.panel
                onContextMenu: (atPointer, anchor, ax, ay, bookmarkIndex, label, seconds) => {
                    bookmarkContextMenu.bookmarkIndex = bookmarkIndex
                    bookmarkContextMenu.bookmarkLabel = label
                    bookmarkContextMenu.bookmarkSeconds = seconds
                    if (atPointer)
                        bookmarkContextMenu.popup(anchor, ax, ay)
                    else
                        bookmarkContextMenu.popup()
                }
                onRenameRequested: (bookmarkIndex, label) => seekStrip.openRename(bookmarkIndex, label)
            }
        }

        TimelineBookmarkContextMenu {
            id: bookmarkContextMenu
            onRenameRequested: (index, label) => seekStrip.openRename(index, label)
        }
    }

    TimelineBakeStrip {
        y: parent.height - height - 1
        width: parent.width
        z: 3
        panel: seekStrip.panel
        onManageRequested: ShellBridge.navigate("dialog:bakes")
    }

    function openRename(index, label) {
        bookmarkRow.renameIndex = index
        renameField.text = label
        renameDialog.open()
    }

    TimelineBookmarkLaneHighlight {
        pxPerSecond: panel.pxPerSecond
    }
}
