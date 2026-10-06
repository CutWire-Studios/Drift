import QtQuick
import QtQuick.Controls.Basic
import Drift

// Opened from the header badge, and on its own the first time a Windows or macOS build finds a
// newer version. Three actions, and "Skip" belongs away from the two safe ones, so the buttons
// live in the content and ThemedDialog's two-button footer is off (same shape as UnsavedChangesDialog).
ThemedDialog {
    id: root

    title: qsTr("Update available")
    preferredWidth: Theme.dialogWidthMd
    showFooter: false
    acceptOnReturn: false

    Shortcut {
        sequences: ["Return", "Enter"]
        enabled: root.visible
        onActivated: root.download()
    }

    function download() {
        if (Updates.canInstall) {
            Updates.downloadAndInstall()
            return
        }
        Updates.openDownloadPage()
        close()
    }

    readonly property string progressText: {
        if (Updates.downloading)
            return qsTr("Downloading Drift %1…").arg(Updates.latestVersion)
        if (Updates.preparing)
            return qsTr("Preparing the update…")
        if (Updates.readyToInstall)
            return qsTr("Drift will quit and install %1.").arg(Updates.latestVersion)
        return Updates.error
    }

    contentItem: Column {
        spacing: Theme.spacingLg
        width: parent ? parent.width : Theme.dialogWidthMd

        ThemedLabel {
            width: parent.width
            size: "base"
            tone: "default"
            text: qsTr("Drift %1 is available").arg(Updates.latestVersion)
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
            visible: Updates.downloading || Updates.preparing
            value: Updates.progress
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
            height: downloadButton.height

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
                    id: downloadButton
                    variant: "primary"
                    glyph: Theme.icons.download
                    enabled: !Updates.downloading && !Updates.preparing
                    text: Updates.readyToInstall ? qsTr("Restart and install")
                          : (Updates.downloading || Updates.preparing) ? qsTr("Downloading…")
                          : qsTr("Download")
                    tooltip: Updates.canInstall
                             ? qsTr("Downloads the update and installs it")
                             : qsTr("Opens the release page in your browser")
                    onClicked: Updates.readyToInstall ? Updates.requestQuit() : root.download()
                }
            }
        }
    }

    onOpened: {
        Updates.markAnnounced()
        Updates.setInstallPromptOpen(true)
        downloadButton.forceActiveFocus()
    }
    onClosed: Updates.setInstallPromptOpen(false)
}
