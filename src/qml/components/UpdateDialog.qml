import QtQuick
import QtQuick.Controls.Basic
import Drift

// Opened from the badge in EditorHeader, never by itself — a dialog over the project someone just
// launched to work on is an interruption, and the badge is already the notification.
//
// Three actions, and "Skip" belongs away from the two safe ones, so the buttons live in the
// content and ThemedDialog's two-button footer is off (same shape as UnsavedChangesDialog).
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
        Updates.openDownloadPage()
        close()
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
                    text: qsTr("Download")
                    tooltip: qsTr("Opens the release page in your browser")
                    onClicked: root.download()
                }
            }
        }
    }

    onOpened: downloadButton.forceActiveFocus()
}
