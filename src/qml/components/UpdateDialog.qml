import QtQuick
import QtQuick.Controls.Basic
import Drift

// Opened from the header badge, on its own the first time a build that installs its own updates
// finds a newer version, and again when a download the user started is ready. Nothing downloads
// or installs until Update is pressed, and Drift never quits unless "Restart and install" is.
// "Skip" belongs away from the safe actions, so the buttons live in the content and ThemedDialog's
// two-button footer is off (same shape as UnsavedChangesDialog).
ThemedDialog {
    id: root

    title: Updates.readyToInstall ? qsTr("Update ready") : qsTr("Update available")
    preferredWidth: Theme.dialogWidthMd
    showFooter: false
    acceptOnReturn: false

    Shortcut {
        sequences: ["Return", "Enter"]
        enabled: root.visible
        onActivated: if (actionButton.enabled) actionButton.clicked()
    }

    function download() {
        if (Updates.canInstall)
            Updates.downloadAndInstall(installOnCloseBox.checked)
        else
            Updates.openDownloadPage()
        close()
    }

    readonly property bool busy: Updates.downloading || Updates.preparing

    readonly property string progressText: {
        if (Updates.downloading)
            return qsTr("Downloading Drift %1…").arg(Updates.latestVersion)
        if (Updates.preparing)
            return qsTr("Preparing the update…")
        if (Updates.readyToInstall && Updates.installScheduled)
            return qsTr("Drift %1 will install when you close Drift.").arg(Updates.latestVersion)
        return Updates.error
    }

    contentItem: Column {
        spacing: Theme.spacingLg
        width: parent ? parent.width : Theme.dialogWidthMd

        ThemedLabel {
            width: parent.width
            size: "base"
            tone: "default"
            text: Updates.readyToInstall
                  ? qsTr("Drift %1 is downloaded and ready to install").arg(Updates.latestVersion)
                  : Updates.latestVersion.length > 0
                    ? qsTr("Drift %1 is available").arg(Updates.latestVersion)
                    : qsTr("A new Drift update is available")
        }

        ThemedLabel {
            width: parent.width
            text: qsTr("You have %1.").arg(Updates.currentVersion)
        }

        Rectangle {
            width: parent.width
            height: Theme.borderWidth
            color: Theme.panelBorder
            visible: notesFlick.visible
        }

        // The release body verbatim, including the install instructions the release workflow
        // appends. Scrolled rather than trimmed: matching on a heading to cut them off would
        // break silently the day that template changes.
        Flickable {
            id: notesFlick
            width: parent.width
            height: Math.min(notes.implicitHeight, 240)
            contentHeight: notes.implicitHeight
            clip: true
            visible: notes.text.length > 0
            ScrollBar.vertical: AppScrollBar { }

            ThemedLabel {
                id: notes
                width: notesFlick.width - Theme.spacingLg
                size: "sm"
                tone: "default"
                textFormat: Text.MarkdownText
                text: Updates.releaseNotes
                onLinkActivated: (link) => Qt.openUrlExternally(link)
            }
        }

        ThemedProgressBar {
            width: parent.width
            visible: root.busy
            value: Updates.progress
        }

        ThemedCheckBox {
            id: installOnCloseBox
            width: parent.width
            visible: Updates.canInstall && !root.busy && !Updates.readyToInstall
            checked: true
            text: qsTr("Install automatically when I close Drift")
            tooltip: qsTr("Downloads in the background and installs the next time you close Drift. "
                          + "Unchecked, you choose when to install once the download finishes.")
        }

        ThemedLabel {
            width: parent.width
            size: "sm"
            tone: "default"
            visible: root.progressText.length > 0
            text: root.progressText
        }

        Item {
            width: parent.width
            height: actionButton.height

            ThemedButton {
                anchors.left: parent.left
                variant: "ghost"
                text: qsTr("Skip")
                tooltip: qsTr("Don't mention %1 again. Later releases are still announced.")
                            .arg(Updates.latestVersion)
                onClicked: {
                    Updates.skipVersion()
                    root.close()
                }
            }

            Row {
                anchors.right: parent.right
                spacing: Theme.spacingLg

                ThemedButton {
                    variant: "secondary"
                    text: qsTr("Later")
                    onClicked: root.close()
                }

                ThemedButton {
                    variant: "secondary"
                    visible: Updates.readyToInstall && !Updates.installScheduled
                    text: qsTr("Install when I close Drift")
                    onClicked: {
                        Updates.scheduleInstallOnQuit()
                        root.close()
                    }
                }

                ThemedButton {
                    id: actionButton
                    variant: "primary"
                    glyph: Updates.readyToInstall ? Theme.icons.refresh : Theme.icons.download
                    enabled: !root.busy
                    text: Updates.readyToInstall ? qsTr("Restart and install")
                          : root.busy ? qsTr("Downloading…")
                          : qsTr("Update")
                    tooltip: Updates.readyToInstall
                             ? qsTr("Closes Drift, installs the update and opens Drift again")
                             : Updates.canInstall
                               ? qsTr("Downloads the update in the background")
                               : qsTr("Opens the release page in your browser")
                    onClicked: {
                        if (Updates.readyToInstall) {
                            root.close()
                            Updates.requestQuit()
                        } else {
                            root.download()
                        }
                    }
                }
            }
        }
    }

    onOpened: {
        Updates.markAnnounced()
        actionButton.forceActiveFocus()
    }
}
