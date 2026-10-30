import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
// Slim CapCut-style editor top bar.
Item {
    id: root

    signal backRequested()
    signal exportRequested()
    signal exportProgressRequested()
    // The project title is the affordance: a wide, self-labelling target in the middle of
    // the bar rather than an unlabelled glyph in the corner.
    signal projectMenuRequested()
    signal downloadsRequested()
    signal notificationsRequested()

    readonly property int downloadCount: {
        void Downloads.revision
        return Downloads.jobs.length
    }
    readonly property int activeDownloadCount: {
        void Downloads.revision
        return Downloads.activeCount
    }

    // Status bar / camera cutout. Without it the Back, Undo and Export buttons sat
    // underneath the system bar on every edge-to-edge device.
    readonly property real topInset: SafeArea.margins.top
    readonly property real leftInset: SafeArea.margins.left
    readonly property real rightInset: SafeArea.margins.right

    height: Theme.androidTopBarHeight + topInset
    width: parent ? parent.width : 0

    ThemeSlot {
        anchors.fill: parent
        slot: "AndroidTopBarBackground"
        control: root
        fallback: Component {
            Rectangle {
                anchors.fill: parent
                color: Theme.c.androidTopBar.background
            }
        }
    }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: Theme.c.androidTopBar.borderWidth
        color: Theme.c.androidTopBar.border
    }

    Item {
        id: barBody
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: root.leftInset
        anchors.rightMargin: root.rightInset
        height: Theme.androidTopBarHeight

        IconButton {
            id: backBtn
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingSm
            anchors.verticalCenter: parent.verticalCenter
            buttonSize: Theme.androidIconButtonSize
            iconSize: Theme.iconSizeLg
            glyph: Theme.icons.chevronLeft
            variant: "text"
            tooltip: qsTr("Back")
            onClicked: root.backRequested()
        }

        AndroidTopBarTitleButton {
            id: titleBtn
            anchors.left: backBtn.right
            anchors.verticalCenter: parent.verticalCenter
            maxTextWidth: Math.max(0, barBody.width - actionsRow.width - backBtn.width
                                      - Theme.iconSizeMd - Theme.spacing3xl)
            projectName: EditorState.projectFile.projectName
            unsaved: EditorState.projectFile.hasUnsavedChanges
            updateAvailable: Updates.updateAvailable
            onRequested: root.projectMenuRequested()
        }

        Row {
            id: actionsRow
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingSm
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingXs

            IconButton {
                buttonSize: Theme.androidIconButtonSize
                iconSize: Theme.iconSizeLg
                glyph: Theme.icons.bell
                variant: "text"
                tooltip: qsTr("Notifications")
                onClicked: root.notificationsRequested()

                AndroidTopBarCountBadge {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.spacingXs
                    visible: Toasts.unreadCount > 0
                    count: Toasts.unreadCount
                    cap: 99
                    grow: true
                }
            }

            // The way into the downloads sheet, and the only one: the sheet never raises itself,
            // because the per-tile ring is what lets browsing carry on while things download.
            IconButton {
                visible: root.downloadCount > 0
                buttonSize: Theme.androidIconButtonSize
                iconSize: Theme.iconSizeLg
                glyph: Theme.icons.download
                variant: "text"
                tooltip: qsTr("Downloads")
                onClicked: root.downloadsRequested()

                AndroidTopBarCountBadge {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.spacingXs
                    visible: root.activeDownloadCount > 0
                    count: root.activeDownloadCount
                }
            }

            IconButton {
                buttonSize: Theme.androidIconButtonSize
                iconSize: Theme.iconSizeLg
                glyph: Theme.icons.puzzle
                variant: "text"
                tooltip: qsTr("Extras")
                onClicked: ShellBridge.main.openExtras()

                AndroidAttentionDot {
                    visible: ShellBridge.main?.addonAttentionNeeded
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.spacingSm
                }
            }

            // Undo/Redo stay icon-only — the one documented exception, and long-pressable
            // for a label now that IconButton shows its tooltip on touch.
            IconButton {
                buttonSize: Theme.androidIconButtonSize
                iconSize: Theme.iconSizeLg
                glyph: Theme.icons.undo
                variant: "text"
                tooltip: qsTr("Undo")
                enabled: EditorState.undoAvailable
                onClicked: EditorState.undo()
            }

            IconButton {
                buttonSize: Theme.androidIconButtonSize
                iconSize: Theme.iconSizeLg
                glyph: Theme.icons.redo
                variant: "text"
                tooltip: qsTr("Redo")
                enabled: EditorState.redoAvailable
                onClicked: EditorState.redo()
            }

            // Doubles as the way back into a render whose progress sheet was dismissed:
            // without it a running export is unreachable and uncancellable, there being no
            // second window on a phone to leave it open in. The ring is drawn around the
            // glyph rather than replacing it, so the pill keeps its width and its label
            // while the render runs.
            AndroidTopBarExportButton {
                id: exportPill
                anchors.verticalCenter: parent.verticalCenter
                exportInProgress: EditorState.exportInProgress
                exportProgress: EditorState.exportProgress
                onExportRequested: root.exportRequested()
                onProgressRequested: root.exportProgressRequested()
            }
        }
    }
}
