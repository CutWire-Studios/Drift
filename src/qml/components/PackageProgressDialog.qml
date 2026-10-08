import QtQuick
import QtQuick.Controls.Basic
import Drift

// Packaging copies every byte of source media into the bundle, so it is modal: the timeline must
// not change under the writer.
ThemedDialog {
    id: root

    title: qsTr("Preparing shareable copy")
    preferredWidth: 360
    showAccept: false
    rejectText: qsTr("Cancel")
    rejectVariant: "destructive"
    closePolicy: Popup.CloseOnEscape

    onRejected: EditorState.projectFile.cancelPackage()

    Connections {
        target: EditorState.projectFile
        function onPackagingChanged() {
            if (EditorState.projectFile.packaging)
                root.open()
            else
                root.close()
        }
    }

    contentItem: Column {
        spacing: Theme.spacing2xl - Theme.spacingXs
        width: parent ? parent.width : 328

        LabelledProgressRing {
            width: parent.width
            value: EditorState.projectFile.packageProgress
            indeterminate: EditorState.projectFile.packageProgress <= 0
        }

        ThemedLabel {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            size: "xs"
            wrapMode: Text.WordWrap
            text: qsTr("Copying your media into one file so it opens on any computer.")
        }
    }
}
