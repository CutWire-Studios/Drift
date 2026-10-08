import QtQuick
import QtQuick.Controls.Basic
import Drift

// Confirm-then-progress for gathering the project's media into one folder. The footer is custom
// for the same reason ReverseProgressDialog's is: choosing Copy or Move has to leave the dialog
// open so it can become the progress readout, and Dialog.accept() closes.
ThemedDialog {
    id: root

    property url folder

    readonly property bool collecting: EditorState.projectFile.collectingMedia

    title: collecting ? qsTr("Collecting media") : qsTr("Collect media to folder")
    preferredWidth: 400
    showFooter: false
    showAccept: false
    showReject: false
    acceptOnReturn: false
    // The files are being copied under the timeline, so a stray click must not dismiss it.
    closePolicy: collecting ? Popup.CloseOnEscape
                            : (Popup.CloseOnEscape | Popup.CloseOnPressOutside)

    onClosed: {
        if (root.collecting)
            EditorState.projectFile.cancelCollectMedia()
    }

    function start(move) {
        EditorState.projectFile.collectMediaToFolder(root.folder, move)
        if (!EditorState.projectFile.collectingMedia)
            root.close()
    }

    Connections {
        target: EditorState.projectFile
        function onCollectingMediaChanged() {
            if (!EditorState.projectFile.collectingMedia)
                root.close()
        }
    }

    contentItem: Column {
        spacing: Theme.spacing2xl
        width: parent ? parent.width : 368

        LabelledProgressRing {
            width: parent.width
            visible: root.collecting
            value: EditorState.projectFile.collectMediaProgress
            indeterminate: EditorState.projectFile.collectMediaProgress <= 0
        }

        ThemedLabel {
            width: parent.width
            horizontalAlignment: root.collecting ? Text.AlignHCenter : Text.AlignLeft
            size: "sm"
            wrapMode: Text.WordWrap
            text: root.collecting
                  ? qsTr("Gathering your media into one folder.")
                  : qsTr("Every file this project uses goes into Video, Audio, Images, Derived "
                         + "and Other folders inside “%1”, and the project is relinked to them.")
                        .arg(decodeURIComponent(root.folder.toString()).split("/")
                             .filter(function(part) { return part.length > 0 }).pop() || "")
        }

        ThemedLabel {
            width: parent.width
            visible: !root.collecting
            size: "xs"
            wrapMode: Text.WordWrap
            text: qsTr("Copy leaves the originals where they are. Move deletes them once "
                       + "everything has landed, and clears undo history.")
        }

        Row {
            anchors.right: parent.right
            spacing: Theme.spacingLg

            ThemedButton {
                text: qsTr("Cancel")
                variant: root.collecting ? "destructive" : "secondary"
                onClicked: root.close()
            }

            ThemedButton {
                visible: !root.collecting
                text: qsTr("Move")
                variant: "secondary"
                onClicked: root.start(true)
            }

            ThemedButton {
                visible: !root.collecting
                text: qsTr("Copy")
                variant: "primary"
                onClicked: root.start(false)
            }
        }
    }
}
