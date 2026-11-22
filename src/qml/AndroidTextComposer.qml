import QtQuick
import QtQuick.Controls.Basic
import Drift
// The phone's subtitle entry: a bar pinned directly above the soft keyboard, used for one
// subtitle cue at a time. It is an Item rather than a Popup so the preview
// above it stays visible and live — AndroidEditor reads `coveredHeight` to keep the frame out
// from under it. The page anchors it; it only has to size itself.
//
// Whatever is typed is committed when the composer closes, when the selection moves to another
// clip, and when stepping between cues: losing typed text is worse than an undo step. Text clips
// are edited on the preview itself (InlineTextEditor).
Item {
    id: root

    property bool active: false
    property int trackIndex: -1
    property int clipIndex: -1
    property string clipId: ""
    property int cueIndex: -1

    // The part of the window this bar occupies, measured from the window bottom.
    readonly property real coveredHeight: active ? height + anchors.bottomMargin : 0

    readonly property real maxFieldHeight: fieldMetrics.lineSpacing * 4
                                           + Theme.spacingMd * 2
    readonly property real frameStep: 1.0 / Math.max(1, EditorState.projectFile.projectFps())

    readonly property var cue: {
        void EditorState.tracksRevision
        return root.active ? root.cueAt(root.cueIndex) : null
    }
    readonly property int cueCount: {
        void EditorState.tracksRevision
        return root.active ? root.cuesOf().length : 0
    }
    readonly property double localPlayhead: {
        void EditorState.inspectorPlayheadSeconds
        return root.active
            ? EditorState.subtitleLocalPlayheadSeconds(root.trackIndex, root.clipIndex) : -1
    }

    visible: active
    implicitHeight: column.implicitHeight + Theme.spacingMd * 2
    height: implicitHeight

    function targetClip() {
        const clip = EditorState.clipAt(root.trackIndex, root.clipIndex)
        return (clip && clip.id === root.clipId) ? clip : null
    }

    function cuesOf() {
        const clip = root.targetClip()
        return (clip && clip.subtitleCues) ? clip.subtitleCues : []
    }

    function cueAt(index) {
        const list = root.cuesOf()
        return (index >= 0 && index < list.length) ? list[index] : null
    }

    function formatCueTime(seconds) {
        const clamped = Math.max(0, seconds)
        const m = Math.floor(clamped / 60)
        const s = clamped - m * 60
        const whole = Math.floor(s)
        const frac = Math.floor((s - whole) * 1000)
        return String(m).padStart(2, "0") + ":" + String(whole).padStart(2, "0") + "."
               + String(frac).padStart(3, "0")
    }

    function begin() {
        EditorState.playing = false
        root.active = true
        Qt.callLater(function () {
            field.forceActiveFocus()
            Qt.inputMethod.show()
        })
    }

    // The cue is one of the selected subtitle clip's.
    function openForCue(index) {
        const track = EditorState.selectedTrack
        const clip = EditorState.selectedClip
        const data = EditorState.clipAt(track, clip)
        if (!data || data.kind !== "subtitle")
            return
        root.commit()
        root.trackIndex = track
        root.clipIndex = clip
        root.clipId = data.id
        root.showCue(index)
        root.begin()
    }

    function showCue(index) {
        const c = root.cueAt(index)
        if (!c)
            return
        root.cueIndex = index
        field.text = c.text || ""
        field.cursorPosition = field.length
        EditorState.selectedSubtitleCue = index
        EditorState.seekToSubtitleCue(root.trackIndex, root.clipIndex, index)
    }

    function goToCue(index) {
        if (index < 0 || index >= root.cueCount)
            return
        root.commit()
        root.showCue(index)
    }

    function commit() {
        if (!root.active)
            return
        const c = root.cueAt(root.cueIndex)
        if (c && field.text !== (c.text || ""))
            root.patchCue({ text: field.text })
    }

    function close() {
        if (!root.active)
            return
        root.commit()
        root.active = false
        field.focus = false
        Qt.inputMethod.hide()
    }

    function patchCue(patch) {
        const list = root.cuesOf()
        const next = []
        for (let i = 0; i < list.length; i++)
            next.push({ start: list[i].start, end: list[i].end, text: list[i].text })
        if (root.cueIndex < 0 || root.cueIndex >= next.length)
            return
        Object.assign(next[root.cueIndex], patch)
        EditorState.setSubtitleCues(root.trackIndex, root.clipIndex, next)
    }

    function nudge(edge, frames) {
        const c = root.cue
        if (!c)
            return
        const step = frames * root.frameStep
        const dur = root.targetClip().duration || 0
        if (edge === "start")
            root.patchCue({ start: Math.max(0, Math.min(c.start + step, c.end - root.frameStep)) })
        else
            root.patchCue({ end: Math.max(c.start + root.frameStep,
                                          dur > 0 ? Math.min(c.end + step, dur) : c.end + step) })
    }

    function setEdgeToPlayhead(edge) {
        const c = root.cue
        const t = root.localPlayhead
        if (!c || t < 0)
            return
        if (edge === "start" && t < c.end)
            root.patchCue({ start: t })
        else if (edge === "end" && t > c.start)
            root.patchCue({ end: t })
    }

    Connections {
        target: EditorState
        function onSelectionChanged() {
            if (root.active && (EditorState.selectedTrack !== root.trackIndex
                                || EditorState.selectedClip !== root.clipIndex))
                root.close()
        }
        function onSelectedSubtitleCueChanged() {
            const i = EditorState.selectedSubtitleCue
            if (root.active && i >= 0 && i !== root.cueIndex) {
                root.commit()
                root.cueIndex = i
                field.text = (root.cueAt(i) || {}).text || ""
            }
        }
    }

    FontMetrics {
        id: fieldMetrics
        font: field.font
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.c.androidTextComposer.background
    }

    Rectangle {
        anchors.top: parent.top
        width: parent.width
        height: Theme.c.androidTextComposer.borderWidth
        color: Theme.c.androidTextComposer.border
    }

    // Swallows touches that miss a control so they do not fall through to the timeline below.
    MouseArea {
        anchors.fill: parent
    }

    Column {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.spacingMd
        spacing: Theme.spacingSm

        Item {
            width: parent.width
            height: Theme.androidMinTouchTarget

            Row {
                id: navRow
                spacing: Theme.spacingSm

                IconButton {
                    buttonSize: Theme.androidMinTouchTarget
                    glyph: Theme.icons.chevronLeft
                    tooltip: qsTr("Previous caption")
                    enabled: root.cueIndex > 0
                    onClicked: root.goToCue(root.cueIndex - 1)
                }

                IconButton {
                    buttonSize: Theme.androidMinTouchTarget
                    glyph: Theme.icons.chevronRight
                    tooltip: qsTr("Next caption")
                    enabled: root.cueIndex < root.cueCount - 1
                    onClicked: root.goToCue(root.cueIndex + 1)
                }
            }

            Text {
                anchors.left: navRow.right
                anchors.leftMargin: Theme.spacingSm
                anchors.right: doneButton.left
                anchors.rightMargin: Theme.spacingSm
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Caption %1 / %2").arg(root.cueIndex + 1).arg(root.cueCount)
                color: Theme.c.androidTextComposer.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                elide: Text.ElideRight
            }

            ThemedButton {
                id: doneButton
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                variant: "primary"
                glyph: Theme.icons.check
                text: qsTr("Done")
                onClicked: root.close()
            }
        }

        Flickable {
            id: fieldFlick
            width: parent.width
            height: Math.min(contentHeight, root.maxFieldHeight)
            contentWidth: width
            contentHeight: Math.max(field.implicitHeight, fieldMetrics.lineSpacing + Theme.spacingMd * 2)
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            interactive: contentHeight > height
            ScrollBar.vertical: AppScrollBar { }

            ThemedTextArea {
                id: field
                width: fieldFlick.width
                height: Math.max(implicitHeight, fieldFlick.contentHeight)
                placeholderText: qsTr("Type subtitle…")
                inputMethodHints: Qt.ImhNone
                onCursorRectangleChanged: {
                    const r = cursorRectangle
                    if (r.y < fieldFlick.contentY)
                        fieldFlick.contentY = r.y
                    else if (r.y + r.height > fieldFlick.contentY + fieldFlick.height)
                        fieldFlick.contentY = r.y + r.height - fieldFlick.height
                }
            }
        }

        Row {
            id: timing
            width: parent.width

            Repeater {
                model: [
                    { edge: "start", label: qsTr("Start"), glyph: Theme.icons.setStart,
                      tip: qsTr("Set start to current time") },
                    { edge: "end", label: qsTr("End"), glyph: Theme.icons.setEnd,
                      tip: qsTr("Set end to current time") }
                ]

                delegate: AndroidComposerTimingEdge {
                    id: group
                    width: timing.width / 2
                    readout: root.cue ? root.formatCueTime(root.cue[group.modelData.edge]) : ""
                    canSetPlayhead: root.localPlayhead >= 0
                    onNudge: (edge, frames) => root.nudge(edge, frames)
                    onSetToPlayhead: (edge) => root.setEdgeToPlayhead(edge)
                }
            }
        }
    }
}
