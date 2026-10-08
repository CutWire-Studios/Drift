import QtQuick
import QtQuick.Controls.Basic
import Drift

// Shown on every launch while an autosave snapshot from the previous session
// exists (typically a crash — a confirmed close clears the snapshot). Restore
// reloads it; New session starts fresh and clears the snapshot.
ThemedDialog {
    id: root

    title: qsTr("Unsaved work from last session")
    acceptText: qsTr("Restore")
    rejectText: qsTr("New session")
    rejectVariant: "secondary"
    preferredWidth: 440
    // Deliberately not dismissable by clicking away — the choice matters — but
    // Enter now commits Restore (see ThemedDialog), so there is a keyboard path
    // to at least one option. Previously both Enter and Escape were inert,
    // leaving this startup-blocking modal mouse-only.
    closePolicy: Popup.NoAutoClose

    readonly property var info: EditorState.projectFile.recoveryInfo

    onAccepted: EditorState.projectFile.restoreAutosave()
    onRejected: EditorState.projectFile.discardAutosave()

    contentItem: Column {
        spacing: Theme.spacingXl
        width: parent ? parent.width : 400

        ThemedLabel {
            width: parent.width
            size: "sm"
            wrapMode: Text.WordWrap
            text: qsTr("Your last session had unsaved changes. You can restore them or start a new empty session.")
        }

        Rectangle {
            width: parent.width
            radius: Theme.radiusSm
            color: Theme.appBackground
            border.width: 1
            border.color: Theme.panelBorder
            height: infoColumn.implicitHeight + 20

            Column {
                id: infoColumn
                x: 12
                y: 10
                width: parent.width - 24
                spacing: 4

                ThemedLabel {
                    width: parent.width
                    tone: "default"
                    size: "base"
                    text: (root.info && root.info.projectName && root.info.projectName.length > 0)
                          ? root.info.projectName
                          : qsTr("Untitled project")
                }

                ThemedLabel {
                    width: parent.width
                    size: "sm"
                    tone: "muted"
                    visible: !!(root.info && root.info.savedAt && root.info.savedAt.length > 0)
                    text: qsTr("Auto-saved: %1").arg(root.info ? root.info.savedAt : "")
                }

                ThemedLabel {
                    id: originalPathLabel
                    width: parent.width
                    size: "sm"
                    tone: "muted"
                    wrapMode: Text.WordWrap
                    // Capped so a deep path cannot grow the dialog off-screen.
                    maximumLineCount: 2
                    elide: Text.ElideMiddle
                    visible: !!(root.info && root.info.originalPath && root.info.originalPath.length > 0)
                    text: (root.info && root.info.originalPath) || ""

                    HoverHandler { id: pathHover }

                    ThemedToolTip {
                        text: originalPathLabel.text
                        visible: pathHover.hovered && originalPathLabel.truncated
                    }
                }
            }
        }
    }
}
